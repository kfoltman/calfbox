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

#define MAX_DELAY_LENGTH 65536

struct delay_params
{
    float time;
    float wet_dry, fb_amt;
};

struct delay_module
{
    struct cbox_module module;

    float storage[MAX_DELAY_LENGTH][2];
    struct delay_params *params;
    int pos;
};

#define MODULE_PARAMS delay_params

MODULE_PROCESSCMD_FUNCTION(delay)
{
    struct delay_module *m = (struct delay_module *)ct->user_data;
    
    EFFECT_PARAM("/time", "f", time, double, , 1, 1000) else
    EFFECT_PARAM("/fb_amt", "f", fb_amt, double, , 0, 1) else
    EFFECT_PARAM("/wet_dry", "f", wet_dry, double, , 0, 1) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/time", "f", error, m->params->time) &&
            cbox_execute_on(fb, NULL, "/fb_amt", "f", error, m->params->fb_amt) &&
            cbox_execute_on(fb, NULL, "/wet_dry", "f", error, m->params->wet_dry);
    }
    else
        return cbox_set_command_error(error, cmd);
    return TRUE;
}

void delay_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct delay_module *m = (struct delay_module *)module;
}

void delay_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct delay_module *m = (struct delay_module *)module;
    
    int pos = m->pos;
    int dv = m->params->time * m->module.srate / 1000.0;
    float dryamt = 1 - m->params->wet_dry;
    float wetamt = m->params->wet_dry;
    float fbamt = m->params->fb_amt;
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float dry[2] = { inputs[0][i], inputs[1][i] };
        float *delayed = &m->storage[pos & (MAX_DELAY_LENGTH - 1)][0];
        
        float wet[2] = { dryamt * dry[0] + wetamt * delayed[0], dryamt * dry[1] + wetamt * delayed[1] };
        float fb[2] = { dry[0] + fbamt * delayed[0], dry[1] + fbamt * delayed[1] };
        outputs[0][i] = sanef(wet[0]);
        outputs[1][i] = sanef(wet[1]);
        
        float *wcell = &m->storage[(pos + dv) & (MAX_DELAY_LENGTH - 1)][0];
        wcell[0] = sanef(fb[0]);
        wcell[1] = sanef(fb[1]);
        pos++;
    }
    m->pos = pos;
}

MODULE_SIMPLE_DESTROY_FUNCTION(delay)

MODULE_CREATE_FUNCTION(delay)
{
    static int inited = 0;
    int i;
    if (!inited)
    {
        inited = 1;
    }
    
    struct delay_module *m = malloc(sizeof(struct delay_module));
    CALL_MODULE_INIT(m, 2, 2, delay);
    struct delay_params *p = malloc(sizeof(struct delay_params));
    m->params = p;
    m->module.process_event = delay_process_event;
    m->module.process_block = delay_process_block;
    m->pos = 0;
    p->time = cbox_config_get_float(cfg_section, "delay", 250);
    p->wet_dry = cbox_config_get_float(cfg_section, "wet_dry", 0.3);
    p->fb_amt = cbox_config_get_gain_db(cfg_section, "feedback_gain", -12.f);
    for (i = 0; i < MAX_DELAY_LENGTH; i++)
        m->storage[i][0] = m->storage[i][1] = 0.f;
    
    return &m->module;
}

struct cbox_module_keyrange_metadata delay_keyranges[] = {
};

struct cbox_module_livecontroller_metadata delay_controllers[] = {
};

DEFINE_MODULE(delay, 2, 2)

