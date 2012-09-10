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

static gboolean cbox_instrument_output_process_cmd(struct cbox_instrument *instr, struct cbox_instrument_output *output, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!(cbox_execute_on(fb, NULL, "/gain_linear", "f", error, output->gain) &&
            cbox_execute_on(fb, NULL, "/gain", "f", error, gain2dB_simple(output->gain)) &&
            cbox_execute_on(fb, NULL, "/output", "i", error, output->output_bus + 1)))
            return FALSE;
        return cbox_module_slot_process_cmd(&output->insert, fb, cmd, subcmd, instr->scene->rt, error);
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
    return cbox_module_slot_process_cmd(&output->insert, fb, cmd, subcmd, instr->scene->rt, error);
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
        return cbox_module_slot_process_cmd(&output->insert, fb, cmd, subcmd, instr->scene->rt, error);
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

void cbox_instrument_destroy_if_unused(struct cbox_instrument *instrument)
{
    if (instrument->refcount == 0)
        cbox_instrument_destroy(instrument);
}

void cbox_instrument_destroy(struct cbox_instrument *instrument)
{
    assert(instrument->refcount == 0);
    for (int i = 0; i < instrument->module->outputs / 2; i ++)
    {
        cbox_instrument_output_uninit(&instrument->outputs[i]);
    }
    free(instrument->outputs);
    for (int i = 0; i < instrument->aux_output_count; i++)
    {
        g_free(instrument->aux_output_names[i]);
    }
    free(instrument->aux_output_names);
    free(instrument->aux_outputs);
    cbox_module_destroy(instrument->module);
    free(instrument);
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
