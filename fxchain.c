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
#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "module.h"
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

struct fxchain_module
{
    struct cbox_module module;

    struct cbox_module **modules;
    int module_count;
};

static gboolean fxchain_module_process_cmd(struct fxchain_module *m, int modindex, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    struct cbox_module **psm = &m->modules[modindex];
    struct cbox_module *sm = *psm;
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!(cbox_execute_on(fb, NULL, "/insert_engine", "s", error, sm ? sm->engine_name : "") &&
            cbox_execute_on(fb, NULL, "/insert_preset", "s", error, sm ? sm->instance_name : "")))
            return FALSE;
        return TRUE;
    }
    if (!strcmp(subcmd, "/insert_preset") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_module *effect = cbox_module_new_from_fx_preset((const char *)cmd->arg_values[0], error);
        if (!effect)
            return FALSE;
        cbox_rt_swap_pointers(app.rt, (void **)psm, effect);
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
        cbox_rt_swap_pointers(app.rt, (void **)psm, effect);
        return TRUE;
    }
    if (!strncmp(subcmd, "/engine/", 8))
    {
        if (!sm)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No engine on module %d in path '%s'", 1 + modindex, cmd->command);
            return FALSE;
        }
        if (!sm->cmd_target.process_cmd)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "The engine %s has no command target defined", sm->engine_name);
            return FALSE;
        }
        return cbox_execute_sub(&sm->cmd_target, fb, cmd, subcmd + 7, error);
    }
    return cbox_set_command_error(error, cmd);
}

gboolean fxchain_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct fxchain_module *m = (struct fxchain_module *)ct->user_data;
    const char *subcommand = NULL;
    int index = 0;
    
    //EFFECT_PARAM("/module_count", "i", stages, int, , 1, 12) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (int i = 0; i < m->module_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/module", "ss", error, m->modules[i]->engine_name, m->modules[i]->instance_name))
                return FALSE;
        }
        return TRUE;
    }
    else if (cbox_parse_path_part(cmd, "/module/", &subcommand, &index, 1, m->module_count, error))
    {
        if (!subcommand)
            return FALSE;
        return fxchain_module_process_cmd(m, index - 1, fb, cmd, subcommand, error);
    }
    else
        return cbox_set_command_error(error, cmd);
    return TRUE;
}

void fxchain_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct fxchain_module *m = module->user_data;
}

void fxchain_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct fxchain_module *m = module->user_data;
    
    float bufs[2][2][CBOX_BLOCK_SIZE];
    int i;

    for (i = 0; i < m->module_count; i++)
    {
        float *input_bufs[2], *output_bufs[2];
        for (int c = 0; c < 2; c++)
        {
            input_bufs[c] = i == 0 ? inputs[c] : bufs[i & 1][c];
            output_bufs[c] = i == m->module_count - 1 ? outputs[c] : bufs[(i + 1) & 1][c];
        }
        m->modules[i]->process_block(m->modules[i]->user_data, input_bufs, output_bufs);
    }
     
}

struct cbox_module *fxchain_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    int i, fx_count = 0;
    for (i = 0; ; i++)
    {
        gchar *name = g_strdup_printf("effect%d", i + 1);
        const char *fx_name = cbox_config_get_string(cfg_section, name);
        g_free(name);
        if (!fx_name)
            break;
    }
    fx_count = i;
    if (cfg_section && !fx_count)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No effects defined");
        return NULL;
    }
    
    struct fxchain_module *m = malloc(sizeof(struct fxchain_module));
    cbox_module_init(&m->module, m, 2, 2, fxchain_process_cmd);
    m->module.process_event = fxchain_process_event;
    m->module.process_block = fxchain_process_block;
    m->modules = malloc(sizeof(struct cbox_module *) * fx_count);
    m->module_count = fx_count;

    for (i = 0; i < fx_count; i++)
        m->modules[i] = NULL;
            
    for (i = 0; i < fx_count; i++)
    {
        gchar *name = g_strdup_printf("effect%d", i + 1);
        const char *fx_preset_name = cbox_config_get_string(cfg_section, name);
        g_free(name);
        m->modules[i] = cbox_module_new_from_fx_preset(fx_preset_name, error);
        if (!m->modules[i])
            goto failed;
    }
    fx_count = i;
    
    return &m->module;

failed:
    m->module_count = i;
    cbox_module_destroy(&m->module);
    return NULL;
}


struct cbox_module_keyrange_metadata fxchain_keyranges[] = {
};

struct cbox_module_livecontroller_metadata fxchain_controllers[] = {
};

DEFINE_MODULE(fxchain, 0, 2)

