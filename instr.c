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

#include "app.h"
#include "auxbus.h"
#include "config-api.h"
#include "instr.h"
#include "io.h"
#include "module.h"
#include "procmain.h"
#include "scene.h"
#include <assert.h>
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

static gboolean cbox_instrument_output_process_cmd(struct cbox_instrument *instr, struct cbox_instrument_output *output, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!(cbox_execute_on(fb, NULL, "/gain_linear", "f", error, output->gain) &&
            cbox_execute_on(fb, NULL, "/gain", "f", error, gain2dB_simple(output->gain)) &&
            cbox_execute_on(fb, NULL, "/output", "i", error, output->output_bus + 1) &&
            cbox_execute_on(fb, NULL, "/insert_engine", "s", error, output->insert ? output->insert->engine_name : "") &&
            cbox_execute_on(fb, NULL, "/insert_preset", "s", error, output->insert ? output->insert->instance_name : "")))
            return FALSE;
        return TRUE;
    }
    if (!strcmp(subcmd, "/gain") && !strcmp(cmd->arg_types, "f"))
    {
        output->gain = dB2gain_simple(*(double *)cmd->arg_values[0]);
        return TRUE;
    }
    if (!strcmp(subcmd, "/output") && !strcmp(cmd->arg_types, "i"))
    {
        int obus = *(int *)cmd->arg_values[0];
        // XXXKF add error checking
        output->output_bus = obus - 1;
        return TRUE;
    }
    if (!strcmp(subcmd, "/insert_preset") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_module *effect = cbox_module_new_from_fx_preset((const char *)cmd->arg_values[0], error);
        if (!effect)
            return FALSE;
        cbox_rt_swap_pointers(app.rt, (void **)&output->insert, effect);
        return TRUE;
    }
    if (!strcmp(subcmd, "/insert_engine") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_module *effect = NULL;
        if (*(const char *)cmd->arg_values[0])
        {
            struct cbox_module_manifest *manifest = cbox_module_manifest_get_by_name((const char *)cmd->arg_values[0]);
            if (!manifest)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No effect engine '%s'", (const char *)cmd->arg_values[0]);
                return FALSE;
            }
            effect = cbox_module_manifest_create_module(manifest, NULL, cbox_io_get_sample_rate(&app.io), "unnamed", error);
            if (!effect)
                return FALSE;
        }
        cbox_rt_swap_pointers(app.rt, (void **)&output->insert, effect);
        return TRUE;
    }
    if (!strncmp(subcmd, "/engine/", 8))
    {
        if (!output->insert)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "The instrument %s has no insert effect on this outut", instr->module->instance_name);
            return FALSE;
        }
        if (!output->insert->cmd_target.process_cmd)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "The engine %s has no command target defined", output->insert->engine_name);
            return FALSE;
        }
        return cbox_execute_sub(&output->insert->cmd_target, fb, cmd, subcmd + 7, error);
    }
    return cbox_set_command_error(error, cmd);            
}

static gboolean cbox_instrument_aux_process_cmd(struct cbox_instrument *instr, struct cbox_instrument_output *output, int id, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!(cbox_execute_on(fb, NULL, "/gain_linear", "f", error, output->gain) &&
            cbox_execute_on(fb, NULL, "/gain", "f", error, gain2dB_simple(output->gain)) &&
            cbox_execute_on(fb, NULL, "/bus", "s", error, instr->aux_output_names[id] ? instr->aux_output_names[id] : "") &&
            cbox_execute_on(fb, NULL, "/insert_engine", "s", error, output->insert ? output->insert->engine_name : "") &&
            cbox_execute_on(fb, NULL, "/insert_preset", "s", error, output->insert ? output->insert->instance_name : "")))
            return FALSE;
        return TRUE;
    }
    else if (!strcmp(subcmd, "/bus") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_scene *scene = instr->scene;
        for (int i = 0; i < scene->aux_bus_count; i++)
        {
            if (!scene->aux_buses[i])
                continue;
            if (!strcmp(scene->aux_buses[i]->name, (const char *)cmd->arg_values[0]))
            {
                g_free(instr->aux_output_names[id]);
                instr->aux_output_names[id] = g_strdup(scene->aux_buses[i]->name);
                cbox_aux_bus_ref(scene->aux_buses[i]);
                struct cbox_aux_bus *old_bus = cbox_rt_swap_pointers(app.rt, (void **)&instr->aux_outputs[id], scene->aux_buses[i]);
                cbox_aux_bus_unref(old_bus);
                return TRUE;
            }
        }
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown aux bus: %s", (const char *)cmd->arg_values[0]);
        return FALSE;
    }
    else if (!strcmp(subcmd, "/output") && !strcmp(cmd->arg_types, "i")) // not supported
    {
        cbox_set_command_error(error, cmd);
        return FALSE;
    }
    else // otherwise, treat just like an command on normal (non-aux) output
        return cbox_instrument_output_process_cmd(instr, output, fb, cmd, subcmd, error);
}

gboolean cbox_instrument_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_instrument *instr = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/engine", "s", error, instr->module->engine_name))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/aux_offset", "i", error, instr->module->aux_offset))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/outputs", "i", error, instr->module->outputs / 2))
            return FALSE;
        return TRUE;
    }
    else if (!strncmp(cmd->command, "/output/", 8))
    {
        const char *num = cmd->command + 8;
        const char *slash = strchr(num, '/');
        if (!slash)
            return cbox_set_command_error(error, cmd);            
        
        gchar *numcopy = g_strndup(num, slash-num);
        int output_num = atoi(numcopy);
        if (output_num < 1 || output_num > instr->module->aux_offset / 2)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid output index %s for command %s", numcopy, cmd->command);
            g_free(numcopy);
            return FALSE;
        }
        g_free(numcopy);
        return cbox_instrument_output_process_cmd(instr, &instr->outputs[output_num - 1], fb, cmd, slash, error);
    }
    else if (!strncmp(cmd->command, "/aux/", 5))
    {
        int aux_offset = instr->module->aux_offset / 2;
        const char *num = cmd->command + 5;
        const char *slash = strchr(num, '/');
        if (!slash)
            return cbox_set_command_error(error, cmd);            
        
        gchar *numcopy = g_strndup(num, slash-num);
        int aux_num = atoi(numcopy);
        if (aux_num < 1 || aux_num > instr->aux_output_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid aux bus index %s for command %s", numcopy, cmd->command);
            g_free(numcopy);
            return FALSE;
        }
        g_free(numcopy);
        return cbox_instrument_aux_process_cmd(instr, &instr->outputs[aux_offset + aux_num - 1], aux_num - 1, fb, cmd, slash, error);
    }
    else
    if (!strncmp(cmd->command, "/engine/",8))
    {
        if (!instr->module->cmd_target.process_cmd)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "The engine %s has no command target defined", instr->module->engine_name);
            return FALSE;
        }
        return cbox_execute_sub(&instr->module->cmd_target, fb, cmd, cmd->command + 7, error);
    }
    else
    {
        cbox_set_command_error(error, cmd);
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

    int auxes = (module->outputs - module->aux_offset) / 2;
    instr = malloc(sizeof(struct cbox_instrument));
    instr->scene = NULL;
    instr->module = module;
    instr->outputs = outputs;
    instr->refcount = 0;
    instr->aux_outputs = malloc(sizeof(struct cbox_aux_bus *) * auxes);
    instr->aux_output_names = malloc(sizeof(char *) * auxes);
    instr->aux_output_count = auxes;
    for (int i = 0; i < auxes; i++)
    {
        instr->aux_outputs[i] = NULL;
        
        gchar *key = g_strdup_printf("aux%d", 1 + i);
        gchar *value = cbox_config_get_string(instr_section, key);
        instr->aux_output_names[i] = value ? g_strdup(value) : NULL;
        g_free(key);
        
    }
    cbox_command_target_init(&instr->cmd_target, cbox_instrument_process_cmd, instr);
    
    free(instr_section);
    
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

void cbox_instrument_destroy(struct cbox_instrument *instrument)
{
    assert(instrument->refcount == 0);
    g_hash_table_remove(instruments.hash, instrument->module->instance_name);
    for (int i = 0; i < instrument->module->outputs / 2; i ++)
    {
        if (instrument->outputs[i].insert)
            cbox_module_destroy(instrument->outputs[i].insert);
    }
    for (int i = 0; i < instrument->aux_output_count; i++)
    {
        g_free(instrument->aux_output_names[i]);
    }
    cbox_module_destroy(instrument->module);
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

