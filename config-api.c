/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GKeyFile *config_keyfile;
static gchar *keyfile_name;
static GStringChunk *cfg_strings = NULL;
static GHashTable *config_sections_hash = NULL;

void cbox_config_init(const char *override_file)
{
    const gchar *keyfiledirs[3];
    const gchar *keyfilename = ".cboxrc";
    GKeyFileFlags flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
    GError *error = NULL;
    if (config_keyfile)
        return;

    config_sections_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    cfg_strings = g_string_chunk_new(100);
    config_keyfile = g_key_file_new();

    // Allow virtual (in-memory) config by passing empty string
    if (override_file && !*override_file)
    {
        keyfile_name = g_strdup("");
        return;
    }

    keyfiledirs[0] = getenv("HOME");
    keyfiledirs[1] = NULL;
    // XXXKF add proper error handling

    if (override_file)
    {
        if (!g_key_file_load_from_file(config_keyfile, override_file, flags, &error))
        {
            g_warning("Could not read user config: %s", error->message);
            g_error_free(error);
        }
        else
        {
            keyfile_name = g_strdup(override_file);
            g_message("User config pathname is %s", keyfile_name);
            return;
        }
    }
    
    if (!g_key_file_load_from_dirs(config_keyfile, keyfilename, keyfiledirs, &keyfile_name, flags, &error))
    {
        g_warning("Could not read cboxrc: %s, search dir = %s, filename = %s", error->message, keyfiledirs[0], keyfilename);
        g_error_free(error);
    }
    else
    {
        g_message("Config pathname is %s", keyfile_name);
    }
}

int cbox_config_has_section(const char *section)
{
    return section && g_key_file_has_group(config_keyfile, section);
}

char *cbox_config_get_string(const char *section, const char *key)
{
    return cbox_config_get_string_with_default(section, key, NULL);
}

void cbox_config_set_string(const char *section, const char *key, const char *value)
{
    g_key_file_set_string(config_keyfile, section, key, value);
}

char *cbox_config_permify(const char *temporary)
{
    return g_string_chunk_insert(cfg_strings, temporary);
}

char *cbox_config_get_string_with_default(const char *section, const char *key, char *def_value)
{
    if (section && key && g_key_file_has_key(config_keyfile, section, key, NULL))
    {
        gchar *tmp = g_key_file_get_string(config_keyfile, section, key, NULL);
        gchar *perm = g_string_chunk_insert(cfg_strings, tmp);
        g_free(tmp);
        return perm;
    }
    else
    {
        return def_value ? g_string_chunk_insert(cfg_strings, def_value) : NULL;
    }
}

int cbox_config_get_int(const char *section, const char *key, int def_value)
{
    GError *error = NULL;
    int result;
    
    if (!section || !key)
        return def_value;
    result = g_key_file_get_integer(config_keyfile, section, key, &error);
    if (error)
    {
        g_error_free(error);
        return def_value;
    }
    return result;
}

void cbox_config_set_int(const char *section, const char *key, int value)
{
    g_key_file_set_integer(config_keyfile, section, key, value);
}

float cbox_config_get_float(const char *section, const char *key, float def_value)
{
    GError *error = NULL;
    float result;
    
    if (!section || !key)
        return def_value;
    result = g_key_file_get_double(config_keyfile, section, key, &error);
    if (error)
    {
        g_error_free(error);
        return def_value;
    }
    return result;
}    

void cbox_config_set_float(const char *section, const char *key, double value)
{
    g_key_file_set_double(config_keyfile, section, key, value);
}

float cbox_config_get_gain(const char *section, const char *key, float def_value)
{
    GError *error = NULL;
    float result;
    
    if (!section || !key)
        return def_value;
    result = g_key_file_get_double(config_keyfile, section, key, &error);
    if (error)
    {
        g_error_free(error);
        return def_value;
    }
    return pow(2.0, result / 6.0);
}    

float cbox_config_get_gain_db(const char *section, const char *key, float def_value)
{
    return cbox_config_get_gain(section, key, pow(2.0, def_value / 6.0));
}

void cbox_config_foreach_section(void (*process)(void *user_data, const char *section), void *user_data)
{
    gsize i, length = 0;
    gchar **groups = g_key_file_get_groups (config_keyfile, &length);
    if (!groups)
        return;
    for (i = 0; i < length; i++)
    {
        process(user_data, groups[i]);
    }
    g_strfreev(groups);
}

void cbox_config_foreach_key(void (*process)(void *user_data, const char *key), const char *section, void *user_data)
{
    gsize i, length = 0;
    gchar **keys = g_key_file_get_keys (config_keyfile, section, &length, NULL);
    if (!keys)
        return;
    for (i = 0; i < length; i++)
    {
        process(user_data, keys[i]);
    }
    g_strfreev(keys);
}

int cbox_config_remove_section(const char *section)
{
    return 0 != g_key_file_remove_group(config_keyfile, section, NULL);
}

int cbox_config_remove_key(const char *section, const char *key)
{
    return 0 != g_key_file_remove_key(config_keyfile, section, key, NULL);
}

gboolean cbox_config_save(const char *filename, GError **error)
{
    gsize len = 0;
    gchar *data = g_key_file_to_data(config_keyfile, &len, error);
    if (!data)
        return FALSE;
    
    if (filename == NULL)
        filename = keyfile_name;

    gboolean ok = g_file_set_contents(filename, data, len, error);
    g_free(data);
    return ok;
}

struct cbox_cfgfile
{
    gchar *libname;
    gchar *filename;
    GKeyFile *keyfile;
};

struct cbox_cfgfile *cbox_cfgfile_get_by_libname(const char *name)
{
    // XXXKF implement
    return NULL;
}

struct cbox_sectref
{
    struct cbox_cfgfile *cfgfile;
    gchar *section;
};

static struct cbox_sectref *cbox_sectref_lookup(const char *name)
{
    struct cbox_sectref *sr = g_hash_table_lookup(config_sections_hash, name);
    return sr;
}

static void cbox_sectref_keep(struct cbox_sectref *sect)
{
    gchar *tmp = g_strdup_printf("%s@%s", sect->section, sect->cfgfile->libname);
    g_hash_table_insert(config_sections_hash, tmp, sect);
    g_free(tmp);
}

static void cbox_sectref_set_from_string(struct cbox_sectref *sr, const gchar *refname, struct cbox_cfgfile *default_lib)
{
    const gchar *p = strchr(refname, '@');
    if (p)
    {
        sr->section = g_strndup(refname, p - refname);
        sr->cfgfile = cbox_cfgfile_get_by_libname(p + 1);
    }
    else
    {
        sr->section = g_strndup(refname, p - refname);
        sr->cfgfile = default_lib;
    }
}

// find the section 'prefix+refname.section' in the config file - either the one referenced by sect, or refname.file
struct cbox_sectref *cbox_config_sectref(struct cbox_sectref *sect, const char *prefix, const char *refname)
{
    if (!prefix)
        prefix = "";
    gchar *tmpsect = NULL;
    const char *p = strchr(refname, '@');
    if (p)
        tmpsect = g_strdup_printf("%s%s", prefix, refname);
    else
        tmpsect = g_strdup_printf("%s%s@%s", prefix, refname, sect->cfgfile->libname);
    struct cbox_sectref *sr = cbox_sectref_lookup(tmpsect);
    if (sr)
    {
        g_free(tmpsect);
        return sr;
    }
    sr = malloc(sizeof(struct cbox_sectref));
    cbox_sectref_set_from_string(sr, tmpsect, sect ? sect->cfgfile : NULL);
    g_free(tmpsect);
    cbox_sectref_keep(sr);
    return sr;
}

struct cbox_sectref *cbox_config_get_sectref(struct cbox_sectref *sect, const char *prefix, const char *key)
{
    if (!key || !sect)
        return NULL;
    //const char *sectname = cbox_config_get_string(sect, key);
    const char *sectname = cbox_config_get_string(sect->section, key);
    if (!sectname)
        return NULL;
    return cbox_config_sectref(sect, prefix, sectname);
}

struct cbox_sectref *cbox_config_get_sectref_n(struct cbox_sectref *sect, const char *prefix, const char *key, int index)
{
    if (!key || !sect)
        return NULL;
    gchar *tmp = g_strdup_printf("%s%d", key, index);
    struct cbox_sectref *sr = cbox_config_get_sectref(sect, prefix, tmp);
    g_free(tmp);
    return sr;
}

struct cbox_sectref *cbox_config_get_sectref_suffix(struct cbox_sectref *sect, const char *prefix, const char *key, const char *suffix)
{
    if (!key || !sect)
        return NULL;
    gchar *tmp = g_strdup_printf("%s%s", key, suffix ? suffix : "");
    struct cbox_sectref *sr = cbox_config_get_sectref(sect, prefix, tmp);
    g_free(tmp);
    return sr;
}

void cbox_config_close()
{
    if (config_sections_hash)
    {
        g_hash_table_destroy(config_sections_hash);
        config_sections_hash = NULL;
    }
    if (config_keyfile)
    {
        g_key_file_free(config_keyfile);
        config_keyfile = NULL;
    }
    if (cfg_strings)
    {
        g_string_chunk_free(cfg_strings);
        cfg_strings = NULL;
    }
    if (keyfile_name)
    {
        g_free(keyfile_name);
        keyfile_name = NULL;
    }
}

