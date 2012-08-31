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

struct jack_input_module
{
    struct cbox_module module;

    int inputs[2];
    int offset;
};

void jack_input_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct jack_input_module *m = module->user_data;
}

void jack_input_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct jack_input_module *m = module->user_data;
    
    for (int i = 0; i < 2; i++)
    {
        float *src = module->rt->io->input_buffers[m->inputs[i]] + m->offset;
        for (int j = 0; j < CBOX_BLOCK_SIZE; j++)
            outputs[i][j] = src[j];
    }
    m->offset = (m->offset + CBOX_BLOCK_SIZE) % app.io.buffer_size;
}

static gboolean validate_input_index(int input, const char *cfg_section, const char *type, GError **error)
{
    if (input < 1 || input > app.io.input_count)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_OUT_OF_RANGE, "%s: invalid value for %s (%d), allowed values are 1..%d", cfg_section, type, input, app.io.input_count);
        return FALSE;
    }
    return TRUE;
}

MODULE_CREATE_FUNCTION(jack_input_create)
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
    CALL_MODULE_INIT(m, 0, 2, NULL);
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

