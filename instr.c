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
#include "instr.h"
#include "io.h"
#include "module.h"
#include <glib.h>

struct cbox_instruments
{
    GHashTable *hash;
    struct cbox_io *io;
};

static struct cbox_instruments instruments;

void cbox_instruments_init(struct cbox_io *io)
{
    // XXXKF needs to use 'full' version with g_free for key and NULL for value
    instruments.hash = g_hash_table_new(g_str_hash, g_str_equal);
    instruments.io = io;
}

extern struct cbox_module *cbox_instruments_get_by_name(const char *name)
{
    struct cbox_module_manifest *mptr = NULL;
    struct cbox_module *module = NULL;
    gchar *instr_section = NULL;
    gpointer value = g_hash_table_lookup(instruments.hash, name);
    const char *cv;
    
    if (value)
        return value;
    
    instr_section = g_strdup_printf("instrument:%s", name);
    
    cv = cbox_config_get_string_with_default(instr_section, "engine", "tonewheel_organ");
    if (!cv)
    {
        g_error("Engine not specified in instrument %s", name);
        goto error;
    }

    mptr = cbox_module_manifest_get_by_name(cv);
    if (!mptr)
    {
        g_error("Cannot find engine %s", cv);
        goto error;
    }
    
    // cbox_module_manifest_dump(mptr);
    
    module = cbox_module_manifest_create_module(mptr, instr_section, cbox_io_get_sample_rate(instruments.io));
    if (!module)
    {
        g_error("Cannot create engine %s for instrument %s", cv, name);
        goto error;
    }
    
    free(instr_section);
    
    g_hash_table_insert(instruments.hash, g_strdup(name), module);
    
    return module;
    
error:
    free(instr_section);
    return NULL;
}

void cbox_instruments_close()
{
    g_hash_table_destroy(instruments.hash);
}
