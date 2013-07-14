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
#include "tarfile.h"
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct cbox_blob *cbox_blob_new(size_t size)
{
    struct cbox_blob *p = malloc(sizeof(struct cbox_blob));
    if (!p)
        return NULL;
    p->data = size ? malloc(size) : NULL;
    p->size = size;
    return p;
}

struct cbox_blob *cbox_blob_new_copy_data(const void *data, size_t size)
{
    struct cbox_blob *p = cbox_blob_new(size);
    if (!p)
        return NULL;
    memcpy(p, data, size);
    return p;
}

struct cbox_blob *cbox_blob_new_acquire_data(void *data, size_t size)
{
    struct cbox_blob *p = malloc(sizeof(struct cbox_blob));
    if (!p)
        return NULL;
    p->data = data;
    p->size = size;
    return p;
}

static struct cbox_blob *read_from_fd(const char *context_name, const char *pathname, int fd, size_t size, GError **error)
{
    struct cbox_blob *blob = cbox_blob_new(size + 1);
    if (!blob)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: cannot allocate memory for file '%s'", context_name, pathname);
        return NULL;
    }
    uint8_t *data = blob->data;
    data[size] = 0;
    size_t nread = 0;
    do {
        size_t chunk = size - nread;
        if (chunk > 131072)
            chunk = 131072;
        size_t nv = read(fd, data + nread, chunk);
        if (nv == (size_t)-1)
        {
            if (errno == EINTR)
                continue;
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: cannot read '%s': %s", context_name, pathname, strerror(errno));
            cbox_blob_destroy(blob);
            return NULL;
        }
        nread += nv;
    } while(nread < size);
    return blob;
}

struct cbox_blob *cbox_blob_new_from_file(const char *context_name, struct cbox_tarfile *tarfile, const char *path, const char *name, size_t max_size, GError **error)
{
    gchar *fullpath = g_build_filename(path, name, NULL);
    struct cbox_blob *blob = NULL;
    if (tarfile)
    {
        struct cbox_taritem *item = cbox_tarfile_get_item_by_name(tarfile, fullpath, TRUE);
        if (item)
        {
            int fd = cbox_tarfile_openitem(tarfile, item);
            if (fd >= 0)
            {
                blob = read_from_fd(context_name, fullpath, fd, item->size, error);
                cbox_tarfile_closeitem(tarfile, item, fd);
            }
        }
    }
    else
    {
        int fd = open(fullpath, O_RDONLY | O_LARGEFILE);
        if (fd >= 0)
        {
            uint64_t size = lseek64(fd, 0, SEEK_END);
            if (size <= max_size)
                blob = read_from_fd(context_name, fullpath, fd, size, error);
            else
                g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: file '%s' too large (%llu while max size is %u)", context_name, fullpath, (unsigned long long)size, (unsigned)max_size);
            close(fd);
        }
        else
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: cannot open '%s': %s", context_name, fullpath, strerror(errno));
    }
    g_free(fullpath);
    return blob;
}

void cbox_blob_destroy(struct cbox_blob *blob)
{
    free(blob->data);
    free(blob);
}
