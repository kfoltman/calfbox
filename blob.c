/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2011 Krzysztof Foltman

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

#include "blob.h"
#include <stdlib.h>
#include <string.h>

struct cbox_blob *cbox_blob_new(size_t size)
{
    struct cbox_blob *p = malloc(sizeof(struct cbox_blob));
    p->data = size ? malloc(size) : NULL;
    p->size = size;
    return p;
}

struct cbox_blob *cbox_blob_new_copy_data(const void *data, size_t size)
{
    struct cbox_blob *p = cbox_blob_new(size);
    memcpy(p, data, size);
    return p;
}

struct cbox_blob *cbox_blob_new_acquire_data(void *data, size_t size)
{
    struct cbox_blob *p = malloc(sizeof(struct cbox_blob));
    p->data = data;
    p->size = size;
    return p;
}

void cbox_blob_destroy(struct cbox_blob *blob)
{
    free(blob->data);
    free(blob);
}
