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

void fxchain_move(struct fxchain_module *m, int oldpos, int newpos)
{
    if (oldpos == newpos)
        return;
    struct cbox_module **modules = malloc(sizeof(struct cbox_module *) * m->module_count);
    for (int i = 0; i < m->module_count; i++)
    {
        int s;
        if (i == newpos)
            s = oldpos;
        else
        {
            if (oldpos < newpos)
                s = (i < oldpos || i > newpos) ? i : i + 1;
            else
                s = (i < newpos || i > oldpos) ? i : i - 1;
        }
        modules[i] = m->modules[s];
    }
    free(cbox_rt_swap_pointers(app.rt, (void **)&m->modules, modules));
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
            gboolean res = FALSE;
            if (m->modules[i])
                res = cbox_execute_on(fb, NULL, "/module", "ss", error, m->modules[i]->engine_name, m->modules[i]->instance_name);
            else
                res = cbox_execute_on(fb, NULL, "/module", "ss", error, "", "");
            if (!res)
                return FALSE;
            res = cbox_execute_on(fb, NULL, "/bypass", "ii", error, i + 1, m->modules[i] ? m->modules[i]->bypass : 0);
        }
        return TRUE;
    }
    else if (cbox_parse_path_part(cmd, "/module/", &subcommand, &index, 1, m->module_count, error))
    {
        if (!subcommand)
            return FALSE;
        return cbox_module_slot_process_cmd(&m->modules[index - 1], fb, cmd, subcommand, error);
    }
    else if (!strcmp(cmd->command, "/insert") && !strcmp(cmd->arg_types, "i"))
    {
        int pos = (*(int *)cmd->arg_values[0]) - 1;
        struct cbox_module **new_modules = malloc((m->module_count + 1) * sizeof(struct cbox_module *));
        memcpy(new_modules, m->modules, pos * sizeof(struct cbox_module *));
        new_modules[pos] = NULL;
        memcpy(new_modules + pos + 1, m->modules + pos, (m->module_count - pos) * sizeof(struct cbox_module *));
        void *old_modules = cbox_rt_swap_pointers_and_update_count(app.rt, (void **)&m->modules, new_modules, &m->module_count, m->module_count + 1);
        free(old_modules);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/delete") && !strcmp(cmd->arg_types, "i"))
    {
        int pos = (*(int *)cmd->arg_values[0]) - 1;
        struct cbox_module **new_modules = malloc((m->module_count + 1) * sizeof(struct cbox_module *));
        memcpy(new_modules, m->modules, pos * sizeof(struct cbox_module *));
        memcpy(new_modules + pos, m->modules + pos + 1, (m->module_count - pos - 1) * sizeof(struct cbox_module *));
        struct cbox_module *deleted_module = m->modules[pos];
        void *old_modules = cbox_rt_swap_pointers_and_update_count(app.rt, (void **)&m->modules, new_modules, &m->module_count, m->module_count - 1);
        free(old_modules);
        if (deleted_module)
            cbox_module_destroy(deleted_module);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/move") && !strcmp(cmd->arg_types, "ii"))
    {
        int oldpos = (*(int *)cmd->arg_values[0]) - 1;
        int newpos = (*(int *)cmd->arg_values[1]) - 1;
        fxchain_move(m, oldpos, newpos);
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
        if (m->modules[i] && !m->modules[i]->bypass)
            m->modules[i]->process_block(m->modules[i]->user_data, input_bufs, output_bufs);
        else
        {
            // this is not eficient at all, but empty modules aren't likely to be used except
            // when setting up a chain.
            for (int c = 0; c < 2; c++)
                memcpy(output_bufs[c], input_bufs[c], CBOX_BLOCK_SIZE * sizeof(float));
        }
    }
     
}

struct cbox_module *fxchain_create(void *user_data, const char *cfg_section, struct cbox_rt *rt, GError **error)
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
    cbox_module_init(&m->module, rt, m, 2, 2, fxchain_process_cmd);
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
        m->modules[i] = cbox_module_new_from_fx_preset(fx_preset_name, rt, error);
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

