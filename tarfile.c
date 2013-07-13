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

#include "config-api.h"
#include "errors.h"
#include "tarfile.h"
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>

struct tar_record
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char ustar[6];
    char ustarver[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};

static void remove_item_if(gpointer p);

struct cbox_tarfile *cbox_tarfile_open(const char *pathname, GError **error)
{
    gboolean debug = cbox_config_get_int("debug", "tarfile", 0);
    gchar *canonical = realpath(pathname, NULL);
    if (!canonical)
    {
        if (error)
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "cannot determine canonical name of '%s'", pathname);
        return NULL;
    }
    int fd = open(canonical, O_RDONLY | O_LARGEFILE);
    if (fd < 0)
    {
        free(canonical);
        if (error)
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "cannot open '%s'", pathname);
        return NULL;
    }
    GHashTable *byname = NULL, *byname_nc = NULL;
    
    byname = g_hash_table_new(g_str_hash, g_str_equal);
    byname_nc = g_hash_table_new(g_str_hash, g_str_equal);
    if (!byname || !byname_nc)
        goto error;
    
    struct cbox_tarfile *tf = calloc(1, sizeof(struct cbox_tarfile));
    if (!tf)
        goto error;
    tf->fd = fd;
    tf->items_byname = byname;
    tf->items_byname_nc = byname_nc;
    tf->refs = 1;
    tf->file_pathname = canonical;
    while(1)
    {
        struct tar_record rec;
        int nbytes = read(fd, &rec, sizeof(rec));
        if (nbytes != sizeof(rec))
            break;

        int len = sizeof(rec.name);
        while(len > 0 && (rec.name[len - 1] == ' ' || rec.name[len - 1] == '\0'))
            len--;
        
        char sizetext[13];
        memcpy(sizetext, rec.size, 12);
        sizetext[12] = '\0';
        unsigned long long size = strtoll(sizetext, NULL, 8);
        
        // skip block if name is empty
        if (!len)
            goto skipitem;
        struct cbox_taritem *ti = calloc(1, sizeof(struct cbox_taritem));
        if (ti)
        {
            ti->filename = g_strndup(rec.name, len);
            ti->filename_nc = g_utf8_casefold(rec.name, len);
            if (!ti->filename || !ti->filename_nc)
                goto itemerror;
            ti->offset = lseek64(fd, 0, SEEK_CUR);
            ti->size = size;
            ti->refs = 2;
            
            // Overwrite old items by the same name and/or same case-folded name
            remove_item_if(g_hash_table_lookup(tf->items_byname, ti->filename));
            remove_item_if(g_hash_table_lookup(tf->items_byname_nc, ti->filename_nc));
            
            g_hash_table_insert(tf->items_byname, ti->filename, ti);
            g_hash_table_insert(tf->items_byname_nc, ti->filename_nc, ti);
            if (debug)
                printf("name = %s len = %d offset = %d readsize = %d\n", ti->filename, len, (int)ti->offset, (int)size);
            
            goto skipitem;
        }
    itemerror:
        rec.name[99] = '\0';
        g_warning("Could not allocate memory for tar item %s", rec.name);
        if (ti)
        {
            if (ti->filename_nc)
                g_free(ti->filename_nc);
            if (ti->filename)
                g_free(ti->filename);
            free(ti);
        }
    skipitem:
        lseek64(fd, (size + 511) &~ 511, SEEK_CUR);
    }
    return tf;

error:
    if (byname)
        g_hash_table_destroy(byname);
    if (byname_nc)
        g_hash_table_destroy(byname_nc);
    free(canonical);
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot allocate memory for tarfile data");
    return NULL;
}

void remove_item_if(gpointer p)
{
    if (!p)
        return;
    
    struct cbox_taritem *ti = p;
    // If all references (by name and by case-folded name) gone, remove the item
    if (!--ti->refs)
    {
        g_free(ti->filename);
        g_free(ti->filename_nc);
        free(ti);
    }
}

struct cbox_taritem *cbox_tarfile_get_item_by_name(struct cbox_tarfile *tarfile, const char *item_filename, gboolean ignore_case)
{
    if (ignore_case)
    {
        gchar *folded = g_utf8_casefold(item_filename, -1);
        struct cbox_taritem *item = g_hash_table_lookup(tarfile->items_byname_nc, folded);
        g_free(folded);
        return item;
    }
    else
        return g_hash_table_lookup(tarfile->items_byname, item_filename);
}

int cbox_tarfile_openitem(struct cbox_tarfile *tarfile, struct cbox_taritem *item)
{
    int fd = open(tarfile->file_pathname, O_RDONLY | O_LARGEFILE);
    if (fd >= 0)
        lseek64(fd, item->offset, SEEK_SET);
    return fd;
}

void cbox_tarfile_closeitem(struct cbox_tarfile *tarfile, struct cbox_taritem *item, int fd)
{
    if (fd >= 0)
        close(fd);
}

static void delete_foreach_func(gpointer key, gpointer value, gpointer user_data)
{
    struct cbox_taritem *ti = value;
    if (!--ti->refs)
    {
        g_free(ti->filename);
        g_free(ti->filename_nc);
        free(ti);
    }
}

void cbox_tarfile_destroy(struct cbox_tarfile *tf)
{
    g_hash_table_foreach(tf->items_byname, delete_foreach_func, NULL);
    g_hash_table_foreach(tf->items_byname_nc, delete_foreach_func, NULL);
    close(tf->fd);
    g_hash_table_destroy(tf->items_byname);
    g_hash_table_destroy(tf->items_byname_nc);
    free(tf->file_pathname);
    free(tf);
}

////////////////////////////////////////////////////////////////////////////////

struct cbox_tarpool *cbox_tarpool_new()
{
    struct cbox_tarpool *pool = calloc(1, sizeof(struct cbox_tarpool));
    pool->files = g_hash_table_new(g_str_hash, g_str_equal);
    return pool;
}

struct cbox_tarfile *cbox_tarpool_get_tarfile(struct cbox_tarpool *pool, const char *name, GError **error)
{
    //gchar *c = realpath(name, NULL);
    gchar *c = g_strdup(name);
    struct cbox_tarfile *tf = g_hash_table_lookup(pool->files, c);
    if (tf)
        tf->refs++;
    else
    {
        tf = cbox_tarfile_open(c, error);
        if (!tf)
        {
            g_free(c);
            return NULL;
        }
        g_hash_table_insert(pool->files, c, tf);
    }
    g_free(c);
    return tf;
}

void cbox_tarpool_release_tarfile(struct cbox_tarpool *pool, struct cbox_tarfile *file)
{
    if (!--file->refs)
        cbox_tarfile_destroy(file);
}

void cbox_tarpool_destroy(struct cbox_tarpool *pool)
{
    g_hash_table_destroy(pool->files);
    free(pool);
}

