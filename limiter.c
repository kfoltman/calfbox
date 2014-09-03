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
#include "onepole-float.h"
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

#define MODULE_PARAMS limiter_params

struct limiter_params
{
    float threshold;
    float attack;
    float release;
};

struct limiter_module
{
    struct cbox_module module;

    struct limiter_params *params, *old_params;
    
    double cur_gain;
    double atk_coeff, rel_coeff;
};

gboolean limiter_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct limiter_module *m = (struct limiter_module *)ct->user_data;
    
    EFFECT_PARAM("/threshold", "f", threshold, double, , -100, 12) else
    EFFECT_PARAM("/attack", "f", attack, double, , 1, 1000) else
    EFFECT_PARAM("/release", "f", release, double, , 1, 5000) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return
            cbox_execute_on(fb, NULL, "/threshold", "f", error, m->params->threshold) &&
            cbox_execute_on(fb, NULL, "/attack", "f", error, m->params->attack) &&
            cbox_execute_on(fb, NULL, "/release", "f", error, m->params->release) &&
            CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error);
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

void limiter_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    // struct limiter_module *m = module->user_data;
}

void limiter_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct limiter_module *m = module->user_data;
    struct limiter_params *mp = m->params;
    
    if (m->params != m->old_params)
    {
        m->atk_coeff = 1 - exp(-1000.0 / (mp->attack * m->module.srate));
        m->rel_coeff = 1 - exp(-1000.0 / (mp->release * m->module.srate));
        // update calculated values
    }
    const double minval = pow(2.0, -110.0);
    for (int i = 0; i < CBOX_BLOCK_SIZE; ++i)
    {
        float left = inputs[0][i], right = inputs[1][i];
        
        float level = fabs(left);
        if (fabs(right) > level)
            level = fabs(right);
        if (level < minval)
            level = minval;
        level = log(level);
        
        float gain = 0.0;
        
        if (level > mp->threshold * 0.11552)
            gain = mp->threshold * 0.11552 - level;
        
        // instantaneous attack + slow release
        if (gain >= m->cur_gain)
            m->cur_gain += m->rel_coeff * (gain - m->cur_gain);
        else
            m->cur_gain += m->atk_coeff * (gain - m->cur_gain);
        
        gain = exp(m->cur_gain);
        //if (gain < 1)
        //    printf("level = %f gain = %f\n", m->cur_level, gain);
        
        outputs[0][i] = left * gain;
        outputs[1][i] = right * gain;
    }
}

MODULE_SIMPLE_DESTROY_FUNCTION(limiter)

MODULE_CREATE_FUNCTION(limiter)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct limiter_module *m = malloc(sizeof(struct limiter_module));
    CALL_MODULE_INIT(m, 2, 2, limiter);
    m->module.process_event = limiter_process_event;
    m->module.process_block = limiter_process_block;
    struct limiter_params *p = malloc(sizeof(struct limiter_params));
    p->threshold = -1;
    p->attack = 10.f;
    p->release = 2000.f;
    m->params = p;
    m->old_params = NULL;
    m->cur_gain = 0.f;
    
    return &m->module;
}


struct cbox_module_keyrange_metadata limiter_keyranges[] = {
};

struct cbox_module_livecontroller_metadata limiter_controllers[] = {
};

DEFINE_MODULE(limiter, 0, 2)

