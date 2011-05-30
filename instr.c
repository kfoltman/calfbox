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

gboolean verify_output_idx(struct cbox_instrument *instr, int output, GError **error)
{
    if (output < 1 || output > instr->module->outputs / 2)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Output number %d out of range (1-%d)'", output, instr->module->outputs / 2);
        return FALSE;
    }
    return TRUE;
}

gboolean cbox_instrument_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_instrument *instr = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_execute_on(fb, NULL, "/engine", "s", error, instr->engine_name))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/aux_offset", "i", error, instr->module->aux_offset))
            return FALSE;

        for (int i = 0; i < instr->module->outputs / 2; i++)
        {
            if (!(cbox_execute_on(fb, NULL, "/gain_linear", "if", error, i, instr->outputs[i].gain) &&
                cbox_execute_on(fb, NULL, "/gain", "if", error, i, gain2dB_simple(instr->outputs[i].gain)) &&
                cbox_execute_on(fb, NULL, "/output", "ii", error, i, instr->outputs[i].output_bus + 1)))
                return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/set_gain") && !strcmp(cmd->arg_types, "if"))
    {
        int output = *(int *)cmd->arg_values[0];
        if (!verify_output_idx(instr, output, error))
            return FALSE;
        double gain = *(double *)cmd->arg_values[1];
        if (gain < -96)
            gain = 0;
        else
            gain = dB2gain(gain);
        instr->outputs[output - 1].gain = gain;
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/set_output") && !strcmp(cmd->arg_types, "ii"))
    {
        int output = *(int *)cmd->arg_values[0];
        if (!verify_output_idx(instr, output, error))
            return FALSE;
        int obus = *(int *)cmd->arg_values[1];
        // XXXKF add error checking
        instr->outputs[output - 1].output_bus = obus - 1;
        return TRUE;
    }
    else
    if (!strncmp(cmd->command, "/engine/",8))
    {
        if (!instr->module->cmd_target.process_cmd)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "The engine %s has no command target defined", instr->engine_name);
            return FALSE;
        }
        return cbox_execute_sub(&instr->module->cmd_target, fb, cmd, cmd->command + 7, error);
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}


extern struct cbox_instrument *cbox_instruments_get_by_name(const char *name, gboolean load, GError **error)
{
    struct cbox_module_manifest *mptr = NULL;
    struct cbox_instrument *instr = NULL;
    struct cbox_module *module = NULL;
    gchar *instr_section = NULL;
    gpointer value = g_hash_table_lookup(instruments.hash, name);
    const char *cv, *instr_engine;
    
    if (value)
        return value;
    if (!load)
        return NULL;
    
    instr_section = g_strdup_printf("instrument:%s", name);
    
    if (!cbox_config_has_section(instr_section))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No config section for instrument '%s'", name);
        goto error;
    }
    
    instr_engine = cbox_config_get_string(instr_section, "engine");
    if (!instr_engine)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Engine not specified in instrument '%s'", name);
        goto error;
    }

    mptr = cbox_module_manifest_get_by_name(instr_engine);
    if (!mptr)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No engine called '%s'", instr_engine);
        goto error;
    }
    
    // cbox_module_manifest_dump(mptr);
    
    module = cbox_module_manifest_create_module(mptr, instr_section, cbox_io_get_sample_rate(instruments.io), name, error);
    if (!module)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Cannot create engine '%s' for instrument '%s': ", instr_engine, name);
        goto error;
    }
    
    struct cbox_instrument_output *outputs = malloc(sizeof(struct cbox_instrument_output) * module->outputs / 2);
    for (int i = 0; i < module->outputs / 2; i ++)
    {
        struct cbox_instrument_output *oobj = outputs + i;
        cbox_instrument_output_init(oobj);
        
        gchar *key = i == 0 ? g_strdup("output_bus") : g_strdup_printf("output%d_bus", 1 + i);
        oobj->output_bus = cbox_config_get_int(instr_section, key, 1) - 1;
        g_free(key);
        key = i == 0 ? g_strdup("gain") : g_strdup_printf("gain%d", 1 + i);
        oobj->gain = cbox_config_get_gain_db(instr_section, key, 0);
        g_free(key);
        
        oobj->insert = NULL;

        key = i == 0 ? g_strdup("insert") : g_strdup_printf("insert%d", 1 + i);
        cv = cbox_config_get_string(instr_section, key);
        g_free(key);
        
        if (cv)
        {
            oobj->insert = cbox_module_new_from_fx_preset(cv, error);
            if (!oobj->insert)
            {
                cbox_force_error(error);
                g_prefix_error(error, "Cannot instantiate effect preset '%s' for instrument '%s': ", cv, name);
            }
        }
    }

    free(instr_section);
    
    instr = malloc(sizeof(struct cbox_instrument));
    instr->module = module;
    instr->outputs = outputs;
    instr->engine_name = instr_engine;
    instr->cmd_target.user_data = instr;
    instr->cmd_target.process_cmd = cbox_instrument_process_cmd;
    
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

void cbox_instrument_output_init(struct cbox_instrument_output *output)
{
    output->insert = NULL;
    output->output_bus = 0;
    output->gain = 1.0;
}

