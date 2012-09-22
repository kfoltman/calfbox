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

#define MODULE_PARAMS distortion_params

struct distortion_params
{
    float drive;
    float shape;
};

struct distortion_module
{
    struct cbox_module module;

    struct distortion_params *params, *old_params;
};

gboolean distortion_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct distortion_module *m = (struct distortion_module *)ct->user_data;
    
    EFFECT_PARAM("/drive", "f", drive, double, dB2gain_simple, -36, 36) else
    EFFECT_PARAM("/shape", "f", shape, double, , -1, 2) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/drive", "f", error, gain2dB_simple(m->params->drive))
            && cbox_execute_on(fb, NULL, "/shape", "f", error, m->params->shape)
            && CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error)
        ;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

void distortion_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct distortion_module *m = module->user_data;
}

void distortion_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct distortion_module *m = module->user_data;
    
    if (m->params != m->old_params)
    {
        // update calculated values
    }
    
    float drive = m->params->drive;
    float shape = m->params->shape;
    
    float a0 = shape;
    float a1 = -2 * shape - 0.5;
    float a2 = 1.5 + shape;
    
    float post = pow(drive, -0.7);
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        for (int c = 0; c < 2; c++)
        {
            float val = inputs[c][i];
            
            val *= drive;
            
            if (fabs(val) > 1.0)
                val = (val > 0) ? 1 : -1;
            else
                val = a0 * val * val * val * val * val + a1 * val * val * val + a2 * val;
            
            outputs[c][i] = val * post;
        }
    }
}

MODULE_SIMPLE_DESTROY_FUNCTION(distortion)

MODULE_CREATE_FUNCTION(distortion)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct distortion_module *m = malloc(sizeof(struct distortion_module));
    CALL_MODULE_INIT(m, 2, 2, distortion);
    m->module.process_event = distortion_process_event;
    m->module.process_block = distortion_process_block;
    struct distortion_params *p = malloc(sizeof(struct distortion_params));
    p->drive = cbox_config_get_gain_db(cfg_section, "drive", 0.f);
    p->shape = cbox_config_get_gain_db(cfg_section, "shape", 0.f);
    m->params = p;
    m->old_params = NULL;
    
    return &m->module;
}


struct cbox_module_keyrange_metadata distortion_keyranges[] = {
};

struct cbox_module_livecontroller_metadata distortion_controllers[] = {
};

DEFINE_MODULE(distortion, 2, 2)

