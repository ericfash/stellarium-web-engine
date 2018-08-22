/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

#include "eph-file.h"
#include "swe.h"
#include "zlib.h"

#include <assert.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>

/* The stars tile file format is as follow:
 *
 * 4 bytes magic string:    "EPHE"
 * 4 bytes file version:    <FILE_VERSION>
 * List of chuncks
 *
 * chunk:
 *  4 bytes: type
 *  4 bytes: data len
 *  4 bytes: data
 *  4 bytes: CRC
 *
 * If type starts with an uppercase letter, this means we got an healpix
 * tile chunck, with the following structure:
 *
 *  4 bytes: version
 *  8 bytes: nuniq hips tile pos
 *  4 bytes: data size
 *  4 bytes: compressed data size
 *  n bytes: compressed data
 *
 */

#define FILE_VERSION 2

typedef struct {
    char     type[4];
    char     type_padding_; // Ensure that type is null terminated.
    int      length;
    uint32_t crc;
    char     *buffer;   // Used when writing.

    int      pos;
} chunk_t;


#define CHUNK_BUFF_SIZE (1 << 20) // 1 MiB max buffer size!

#define READ(data, size, type) ({ \
        type v_; \
        CHECK(size >= sizeof(type)); \
        memcpy(&v_, data, sizeof(type)); \
        size -= sizeof(type); \
        data += sizeof(type); \
        v_; \
    })

static bool chunk_read_start(chunk_t *c, const void **data, int *data_size)
{
    memset(c, 0, sizeof(*c));
    if (*data_size == 0) return false;
    CHECK(*data_size >= 8);
    memcpy(c->type, *data, 4);
    *data += 4; *data_size -= 4;
    c->length = READ(*data, *data_size, int32_t);
    return true;
}

static void chunk_read_finish(chunk_t *c, const void **data, int *data_size)
{
    int crc;
    assert(c->pos == c->length);
    crc = READ(*data, *data_size, int32_t);
    (void)crc; // TODO: check crc.
}

static void chunk_read(chunk_t *c, const void **data, int *data_size,
                       char *buff, int size)
{
    c->pos += size;
    assert(c->pos <= c->length);
    CHECK(*data_size >= size);
    memcpy(buff, *data, size);
    *data += size;
    *data_size -= size;
}

#define CHUNK_READ(chunk, data, data_size, type) ({ \
        type v_; \
        chunk_read(chunk, data, data_size, (char*)&(v_), sizeof(v_)); \
        v_; \
    })

int eph_load(const void *data, int data_size, void *user,
             int (*callback)(const char type[4], int version,
                             int order, int pix,
                             int size, void *data, void *user))
{
    chunk_t c;
    int version, tile_version;
    int order, pix, comp_size;
    uint64_t nuniq;
    unsigned long size;
    void *chunk_data, *buf;

    assert(data);
    CHECK(data_size >= 4);
    CHECK(strncmp(data, "EPHE", 4) == 0);
    data += 4; data_size -= 4;
    version = READ(data, data_size, int32_t);
    CHECK(version == FILE_VERSION);
    while (chunk_read_start(&c, &data, &data_size)) {
        // Uppercase starting chunks are healpix tiles.
        if (c.type[0] >= 'A' && c.type[0] <= 'Z') {
            tile_version = CHUNK_READ(&c, &data, &data_size, int32_t);
            nuniq = CHUNK_READ(&c, &data, &data_size, uint64_t);
            order = log2(nuniq / 4) / 2;
            pix = nuniq - 4 * (1 << (2 * order));
            size = CHUNK_READ(&c, &data, &data_size, int32_t);
            comp_size = CHUNK_READ(&c, &data, &data_size, int32_t);
            chunk_data = malloc(size);
            buf = malloc(comp_size);
            chunk_read(&c, &data, &data_size, buf, comp_size);
            uncompress(chunk_data, &size, buf, comp_size);
            free(buf);
            callback(c.type, tile_version, order, pix, size, chunk_data, user);
            free(chunk_data);
        }
        chunk_read_finish(&c, &data, &data_size);
    }
    return 0;
}
