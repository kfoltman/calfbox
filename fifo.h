/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CBOX_FIFO_H
#define CBOX_FIFO_H

#include <assert.h>
#include <stdint.h>
#include <glib.h>
#include <string.h>

struct cbox_fifo
{
    uint8_t *data;
    uint32_t size;
    uint64_t pad; // ensure the write-related and read-related structs are on 64 bit boundary
    uint32_t write_count;
    uint32_t write_offset;
    uint32_t read_count;
    uint32_t read_offset;
};

extern struct cbox_fifo *cbox_fifo_new(uint32_t size);

static inline uint32_t cbox_fifo_readsize(struct cbox_fifo *fifo);
static inline uint32_t cbox_fifo_writespace(struct cbox_fifo *fifo);
static inline gboolean cbox_fifo_read_atomic(struct cbox_fifo *fifo, void *dest, uint32_t bytes);
static inline gboolean cbox_fifo_write_atomic(struct cbox_fifo *fifo, const void *src, uint32_t bytes);
static inline gboolean cbox_fifo_peek(struct cbox_fifo *fifo, void *dest, uint32_t bytes);
static inline gboolean cbox_fifo_consume(struct cbox_fifo *fifo, uint32_t bytes);

extern void cbox_fifo_destroy(struct cbox_fifo *fifo);


static inline uint32_t cbox_fifo_readsize(struct cbox_fifo *fifo)
{
    return fifo->write_count - fifo->read_count;
}

static inline uint32_t cbox_fifo_writespace(struct cbox_fifo *fifo)
{
    return fifo->size - (fifo->write_count - fifo->read_count);
}

static inline gboolean cbox_fifo_read_impl(struct cbox_fifo *fifo, void *dest, uint32_t bytes, gboolean advance)
{
    __sync_synchronize();
    if (fifo->write_count - fifo->read_count < bytes)
        return FALSE;
    
    if (dest)
    {
        uint32_t ofs = fifo->read_count - fifo->read_offset;
        assert(ofs >= 0 && ofs < fifo->size);
        if (ofs + bytes > fifo->size)
        {
            uint8_t *dstb = (uint8_t *)dest;
            uint32_t firstpart = fifo->size - ofs;
            memcpy(dstb, fifo->data + ofs, firstpart);
            memcpy(dstb + firstpart, fifo->data, bytes - firstpart);
        }
        else
            memcpy(dest, fifo->data + ofs, bytes);
    }

    if (advance)
    {
        __sync_synchronize();
        // Make sure data are copied before signalling that they can be overwritten
        fifo->read_count += bytes;
        if (fifo->read_count - fifo->read_offset >= fifo->size)
            fifo->read_offset += fifo->size;
    }
    __sync_synchronize();

    return TRUE;
}

static inline gboolean cbox_fifo_read_atomic(struct cbox_fifo *fifo, void *dest, uint32_t bytes)
{
    return cbox_fifo_read_impl(fifo, dest, bytes, TRUE);
}

static inline gboolean cbox_fifo_peek(struct cbox_fifo *fifo, void *dest, uint32_t bytes)
{
    return cbox_fifo_read_impl(fifo, dest, bytes, FALSE);
}

static inline gboolean cbox_fifo_consume(struct cbox_fifo *fifo, uint32_t bytes)
{
    return cbox_fifo_read_impl(fifo, NULL, bytes, TRUE);
}

static inline gboolean cbox_fifo_write_atomic(struct cbox_fifo *fifo, const void *src, uint32_t bytes)
{
    if (fifo->size - (fifo->write_count - fifo->read_count) < bytes)
        return FALSE;
    
    uint32_t ofs = fifo->write_count - fifo->write_offset;
    assert(ofs >= 0 && ofs < fifo->size);
    if (ofs + bytes > fifo->size)
    {
        const uint8_t *srcb = (const uint8_t *)src;
        uint32_t firstpart = fifo->size - ofs;
        memcpy(fifo->data + ofs, srcb, firstpart);
        memcpy(fifo->data, srcb + firstpart, bytes - firstpart);
    }
    else
        memcpy(fifo->data + ofs, src, bytes);

    // Make sure data are in the buffer before announcing the availability
    __sync_synchronize();
    fifo->write_count += bytes;
    if (fifo->write_count - fifo->write_offset >= fifo->size)
        fifo->write_offset += fifo->size;

    return TRUE;    
}


#endif
