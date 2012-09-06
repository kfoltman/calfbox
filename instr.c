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

#include "auxbus.h"
#include "config-api.h"
#include "instr.h"
#include "module.h"
#include "rt.h"
#include "scene.h"
#include <assert.h>
#include <glib.h>

struct cbox_instruments
{
    GHashTable *hash;
    struct cbox_rt *rt;
};

struct cbox_instruments *cbox_instruments_new(struct cbox_rt *rt)
{
    // XXXKF needs to use 'full' version with g_free for key and value
    struct cbox_instruments *res = malloc(sizeof(struct cbox_instruments));
    res->hash = g_hash_table_new(g_str_hash, g_str_equal);
    res->rt = rt;
    return res;
}

static gboolean cbox_instrument_output_process_cmd(struct cbox_instrument *instr, struct cbox_instrument_output *output, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!(cbox_execute_on(fb, NULL, "/gain_linear", "f", error, output->gain) &&
            cbox_execute_on(fb, NULL, "/gain", "f", error, gain2dB_simple(output->gain)) &&
            cbox_execute_on(fb, NULL, "/output", "i", error, output->output_bus + 1)))
            return FALSE;
        return cbox_module_slot_process_cmd(&output->insert, fb, cmd, subcmd, error);
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
    return cbox_module_slot_process_cmd(&output->insert, fb, cmd, subcmd, error);
}

static gboolean cbox_instrument_aux_process_cmd(struct cbox_instrument *instr, struct cbox_instrument_output *output, int id, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!(cbox_execute_on(fb, NULL, "/gain_linear", "f", error, output->gain) &&
            cbox_execute_on(fb, NULL, "/gain", "f", error, gain2dB_simple(output->gain)) &&
            cbox_execute_on(fb, NULL, "/bus", "s", error, instr->aux_output_names[id] ? instr->aux_output_names[id] : "")))
            return FALSE;
        return cbox_module_slot_process_cmd(&output->insert, fb, cmd, subcmd, error);
    }
    else if (!strcmp(subcmd, "/bus") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_scene *scene = instr->scene;
        if (!*(const char *)cmd->arg_values[0])
        {
            struct cbox_aux_bus *old_bus = cbox_rt_swap_pointers(instr->module->rt, (void **)&instr->aux_outputs[id], NULL);
            if (old_bus)
                cbox_aux_bus_unref(old_bus);
            return TRUE;            
        }
        for (int i = 0; i < scene->aux_bus_count; i++)
        {
            if (!scene->aux_buses[i])
                continue;
            if (!strcmp(scene->aux_buses[i]->name, (const char *)cmd->arg_values[0]))
            {
                g_free(instr->aux_output_names[id]);
                instr->aux_output_names[id] = g_strdup(scene->aux_buses[i]->name);
                cbox_aux_bus_ref(scene->aux_buses[i]);
                struct cbox_aux_bus *old_bus = cbox_rt_swap_pointers(instr->module->rt, (void **)&instr->aux_outputs[id], scene->aux_buses[i]);
                if (old_bus)
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
    const char *subcommand = NULL;
    int index = 0;
    int aux_offset = instr->module->aux_offset / 2;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/engine", "s", error, instr->module->engine_name))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/aux_offset", "i", error, instr->module->aux_offset / 2 + 1))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/outputs", "i", error, instr->module->outputs / 2))
            return FALSE;
        return TRUE;
    }
    else if (cbox_parse_path_part(cmd, "/output/", &subcommand, &index, 1, aux_offset, error))
    {
        if (!subcommand)
            return FALSE;
        return cbox_instrument_output_process_cmd(instr, &instr->outputs[index - 1], fb, cmd, subcommand, error);
    }
    else if (cbox_parse_path_part(cmd, "/aux/", &subcommand, &index, 1, instr->aux_output_count, error))
    {
        if (!subcommand)
            return FALSE;
        return cbox_instrument_aux_process_cmd(instr, &instr->outputs[aux_offset + index - 1], index - 1, fb, cmd, subcommand, error);
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


extern struct cbox_instrument *cbox_instruments_get_by_name(struct cbox_instruments *instruments, const char *name, gboolean load, GError **error)
{
    struct cbox_module_manifest *mptr = NULL;
    struct cbox_instrument *instr = NULL;
    struct cbox_module *module = NULL;
    gchar *instr_section = NULL;
    gpointer value = g_hash_table_lookup(instruments->hash, name);
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
    
    module = cbox_module_manifest_create_module(mptr, instr_section, instruments->rt, name, error);
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
        cbox_instrument_output_init(oobj, cbox_rt_get_buffer_size(module->rt));
        
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
            oobj->insert = cbox_module_new_from_fx_preset(cv, module->rt, error);
            if (!oobj->insert)
            {
                cbox_force_error(error);
                g_prefix_error(error, "Cannot instantiate effect preset '%s' for instrument '%s': ", cv, name);
            }
        }
    }

    int auxes = (module->outputs - module->aux_offset) / 2;
    instr = malloc(sizeof(struct cbox_instrument));
    instr->owner = instruments;
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
    
    g_hash_table_insert(instruments->hash, g_strdup(name), instr);
    
    // cbox_recording_source_attach(&instr->outputs[0].rec_dry, cbox_recorder_new_stream("output.wav"));
    
    return instr;
    
error:
    free(instr_section);
    return NULL;
}

struct cbox_rt *cbox_instruments_get_rt(struct cbox_instruments *instruments)
{
    return instruments->rt;
}

void cbox_instrument_destroy(struct cbox_instrument *instrument)
{
    assert(instrument->refcount == 0);
    g_hash_table_remove(instrument->owner->hash, instrument->module->instance_name);
    for (int i = 0; i < instrument->module->outputs / 2; i ++)
    {
        cbox_instrument_output_uninit(&instrument->outputs[i]);
    }
    for (int i = 0; i < instrument->aux_output_count; i++)
    {
        g_free(instrument->aux_output_names[i]);
    }
    cbox_module_destroy(instrument->module);
}

void cbox_instrument_unref_aux_buses(struct cbox_instrument *instrument)
{
    for (int j = 0; j < instrument->aux_output_count; j++)
    {
        if (instrument->aux_outputs[j])
            cbox_aux_bus_unref(instrument->aux_outputs[j]);
    }    
}

void cbox_instrument_disconnect_aux_bus(struct cbox_instrument *instrument, struct cbox_aux_bus *bus)
{
    for (int j = 0; j < instrument->aux_output_count; j++)
    {
        if (instrument->aux_outputs[j] == bus)
        {
            cbox_aux_bus_unref(instrument->aux_outputs[j]);
            instrument->aux_outputs[j] = NULL;
        }
    }    
}

void cbox_instruments_destroy(struct cbox_instruments *instruments)
{
    g_hash_table_destroy(instruments->hash);
    free(instruments);
}

void cbox_instrument_output_init(struct cbox_instrument_output *output, uint32_t max_numsamples)
{
    cbox_recording_source_init(&output->rec_dry, max_numsamples, 2);
    cbox_recording_source_init(&output->rec_wet, max_numsamples, 2);
    output->insert = NULL;
    output->output_bus = 0;
    output->gain = 1.0;
}


void cbox_instrument_output_uninit(struct cbox_instrument_output *output)
{
    cbox_recording_source_uninit(&output->rec_dry);
    cbox_recording_source_uninit(&output->rec_wet);
    if (output->insert)
        cbox_module_destroy(output->insert);
}
