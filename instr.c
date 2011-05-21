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
    // XXXKF needs to use 'full' version with g_free for key and value
    instruments.hash = g_hash_table_new(g_str_hash, g_str_equal);
    instruments.io = io;
}

extern struct cbox_instrument *cbox_instruments_get_by_name(const char *name)
{
    struct cbox_module_manifest *mptr = NULL;
    struct cbox_instrument *instr = NULL;
    struct cbox_module *module = NULL;
    struct cbox_module *effect = NULL;
    gchar *instr_section = NULL;
    gpointer value = g_hash_table_lookup(instruments.hash, name);
    const char *cv, *instr_engine;
    GError *errobj = NULL;
    
    if (value)
        return value;
    
    instr_section = g_strdup_printf("instrument:%s", name);
    
    instr_engine = cbox_config_get_string(instr_section, "engine");
    if (!instr_engine)
    {
        g_error("Engine not specified in instrument %s", name);
        goto error;
    }

    mptr = cbox_module_manifest_get_by_name(instr_engine);
    if (!mptr)
    {
        g_error("Cannot find engine %s", instr_engine);
        goto error;
    }
    
    // cbox_module_manifest_dump(mptr);
    
    module = cbox_module_manifest_create_module(mptr, instr_section, cbox_io_get_sample_rate(instruments.io), &errobj);
    if (!module)
    {
        g_error("Cannot create engine %s for instrument %s: %s", instr_engine, name, errobj ? errobj->message : "unknown error");
        if (errobj)
            g_error_free(errobj);
        goto error;
    }
    
    effect = NULL;
    cv = cbox_config_get_string(instr_section, "insert");
    if (cv)
    {
        effect = cbox_module_new_from_fx_preset(cv, &errobj);
        if (!effect)
        {
            g_error("%s", errobj ? errobj->message : "unknown error");
            if (errobj)
                g_error_free(errobj);
        }
    }

    free(instr_section);
    
    instr = malloc(sizeof(struct cbox_instrument));
    instr->module = module;
    instr->insert = effect;
    instr->engine_name = instr_engine;
    
    g_hash_table_insert(instruments.hash, g_strdup(name), instr);
    
    return instr;
    
error:
    free(instr_section);
    return NULL;
}

struct cbox_io *cbox_instruments_get_io()
{
    return instruments.io;
}

void cbox_instruments_close()
{
    g_hash_table_destroy(instruments.hash);
}
