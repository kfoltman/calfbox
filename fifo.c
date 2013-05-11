#include "fifo.h"
#include <malloc.h>

struct cbox_fifo *cbox_fifo_new(uint32_t size)
{
    struct cbox_fifo *fifo = calloc(1, sizeof(struct cbox_fifo) + size);
    if (!fifo)
        return NULL;
    fifo->data = (uint8_t *)(fifo + 1);
    fifo->size = size;
    fifo->write_count = 0;
    fifo->write_offset= 0;
    fifo->read_count = 0;
    fifo->read_offset = 0;
    return fifo;
}

void cbox_fifo_destroy(struct cbox_fifo *fifo)
{
    free(fifo);
}

