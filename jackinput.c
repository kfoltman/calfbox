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

#if USE_JACK

struct jack_input_module
{
    struct cbox_module module;

    int inputs[2];
    int offset;
};

void jack_input_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    // struct jack_input_module *m = module->user_data;
}

void jack_input_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct jack_input_module *m = module->user_data;
    
    for (int i = 0; i < 2; i++)
    {
        if (m->inputs[i] < 0)
        {
            for (int j = 0; j < CBOX_BLOCK_SIZE; j++)
                outputs[i][j] = 0;
        }
        else
        {
            float *src = module->rt->io->input_buffers[m->inputs[i]] + m->offset;
            for (int j = 0; j < CBOX_BLOCK_SIZE; j++)
                outputs[i][j] = src[j];
        }
    }
    m->offset = (m->offset + CBOX_BLOCK_SIZE) % app.io.io_env.buffer_size;
}

static gboolean validate_input_index(int input, const char *cfg_section, const char *type, GError **error)
{
    if ((input < 1 || input > (int)app.io.io_env.input_count) && input != -1)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_OUT_OF_RANGE, "%s: invalid value for %s (%d), allowed values are 1..%d or -1 for unconnected", cfg_section, type, input, app.io.io_env.input_count);
        return FALSE;
    }
    return TRUE;
}

static void jack_input_destroyfunc(struct cbox_module *module)
{
}

static int to_base1(int val)
{
    if (val < 0)
        return val;
    return 1 + val;
}

gboolean jack_input_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct jack_input_module *m = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/inputs", "ii", error, to_base1(m->inputs[0]), to_base1(m->inputs[1])))
            return FALSE;
        return CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error);
    }
    else if (!strcmp(cmd->command, "/inputs") && !strcmp(cmd->arg_types, "ii"))
    {
        int left_input = CBOX_ARG_I(cmd, 0);
        int right_input = CBOX_ARG_I(cmd, 1);
        if (!validate_input_index(left_input, "script", "left input", error))
            return FALSE;
        if (!validate_input_index(right_input, "script", "right input", error))
            return FALSE;
        m->inputs[0] = left_input < 0 ? -1 : left_input - 1;
        m->inputs[1] = right_input < 0 ? -1 : right_input - 1;
        return TRUE;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

MODULE_CREATE_FUNCTION(jack_input)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    int left_input = cbox_config_get_int(cfg_section, "left_input", 1);
    int right_input = cbox_config_get_int(cfg_section, "right_input", 2);
    if (!validate_input_index(left_input, cfg_section, "left_input", error))
        return NULL;
    if (!validate_input_index(right_input, cfg_section, "right_input", error))
        return NULL;
    
    struct jack_input_module *m = malloc(sizeof(struct jack_input_module));
    CALL_MODULE_INIT(m, 0, 2, jack_input);
    m->module.process_event = jack_input_process_event;
    m->module.process_block = jack_input_process_block;
    
    m->inputs[0] = left_input - 1;
    m->inputs[1] = right_input - 1;
    m->offset = 0;
    
    return &m->module;
}


struct cbox_module_keyrange_metadata jack_input_keyranges[] = {
};

struct cbox_module_livecontroller_metadata jack_input_controllers[] = {
};

DEFINE_MODULE(jack_input, 0, 2)

#endif
