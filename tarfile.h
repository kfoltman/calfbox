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

#ifndef CBOX_TARFILE_H
#define CBOX_TARFILE_H

#include <glib.h>
#include <stdint.h>
#include <sndfile.h>

struct cbox_taritem
{
    gchar *filename;
    gchar *filename_nc;
    uint64_t offset;
    uint64_t size;
    int refs;
};

struct cbox_tarfile
{
    int fd;
    int refs;
    GHashTable *items_byname;
    GHashTable *items_byname_nc;
    char *file_pathname; //full path to the .tar file with filename.ext
};

struct cbox_tarpool
{
    GHashTable *files;
};

struct cbox_tarfile_sndstream
{
    struct cbox_tarfile *file;
    struct cbox_taritem *item;
    uint64_t filepos;
};

extern struct SF_VIRTUAL_IO cbox_taritem_virtual_io;

extern struct cbox_tarfile *cbox_tarfile_open(const char *pathname, GError **error);

extern struct cbox_taritem *cbox_tarfile_get_item_by_name(struct cbox_tarfile *tarfile, const char *item_filename, gboolean ignore_case);
extern int cbox_tarfile_openitem(struct cbox_tarfile *tarfile, struct cbox_taritem *item);
extern void cbox_tarfile_closeitem(struct cbox_tarfile *tarfile, struct cbox_taritem *item, int fd);

extern SNDFILE *cbox_tarfile_opensndfile(struct cbox_tarfile *tarfile, struct cbox_taritem *item, struct cbox_tarfile_sndstream *stream, SF_INFO *sfinfo);
// No need to close - it reuses the cbox_tarfile file descriptor

extern void cbox_tarfile_destroy(struct cbox_tarfile *tf);

extern struct cbox_tarpool *cbox_tarpool_new(void);
extern struct cbox_tarfile *cbox_tarpool_get_tarfile(struct cbox_tarpool *pool, const char *name, GError **error);
extern void cbox_tarpool_release_tarfile(struct cbox_tarpool *pool, struct cbox_tarfile *file);
extern void cbox_tarpool_destroy(struct cbox_tarpool *pool);

#endif
