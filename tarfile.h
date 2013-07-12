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
    GHashTable *items_byname;
    GHashTable *items_byname_nc;
};

extern struct cbox_tarfile *cbox_tarfile_open(const char *pathname, GError **error);

extern struct cbox_taritem *cbox_tarfile_get_item_by_name(struct cbox_tarfile *tarfile, const char *item_filename, gboolean ignore_case);
extern int cbox_tarfile_openitem(struct cbox_tarfile *tarfile, struct cbox_taritem *item);
extern void cbox_tarfile_closeitem(struct cbox_tarfile *tarfile, struct cbox_taritem *item, int fd);

extern void cbox_tarfile_destroy(struct cbox_tarfile *tf);

#endif
