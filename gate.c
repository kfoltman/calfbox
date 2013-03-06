/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2012 Krzysztof Foltman

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

#define MODULE_PARAMS gate_params

struct gate_params
{
    float threshold;
    float ratio;
    float attack;
    float hold;
    float release;
};

struct gate_module
{
    struct cbox_module module;

    struct gate_params *params, *old_params;
    struct cbox_onepolef_coeffs attack_lp, release_lp, shifter_lp;
    struct cbox_onepolef_state shifter1, shifter2;
    struct cbox_onepolef_state tracker;
    int hold_time, hold_threshold;
};

gboolean gate_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct gate_module *m = (struct gate_module *)ct->user_data;
    
    EFFECT_PARAM("/threshold", "f", threshold, double, dB2gain_simple, -100, 100) else
    EFFECT_PARAM("/ratio", "f", ratio, double, , 1, 100) else
    EFFECT_PARAM("/attack", "f", attack, double, , 1, 1000) else
    EFFECT_PARAM("/hold", "f", hold, double, , 1, 1000) else
    EFFECT_PARAM("/release", "f", release, double, , 1, 1000) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/threshold", "f", error, gain2dB_simple(m->params->threshold))
            && cbox_execute_on(fb, NULL, "/ratio", "f", error, m->params->ratio)
            && cbox_execute_on(fb, NULL, "/attack", "f", error, m->params->attack)
            && cbox_execute_on(fb, NULL, "/hold", "f", error, m->params->hold)
            && cbox_execute_on(fb, NULL, "/release", "f", error, m->params->release)
            && CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error)
            ;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

void gate_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    // struct gate_module *m = module->user_data;
}

void gate_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct gate_module *m = module->user_data;
    
    if (m->params != m->old_params)
    {
        float scale = M_PI * 1000 / m->module.srate;
        cbox_onepolef_set_lowpass(&m->attack_lp, scale / m->params->attack);
        cbox_onepolef_set_lowpass(&m->release_lp, scale / m->params->release);
        cbox_onepolef_set_allpass(&m->shifter_lp, M_PI * 100 / m->module.srate);
        m->hold_threshold = (int)(m->module.srate * m->params->hold * 0.001);
        m->old_params = m->params;
    }
    
    float threshold = m->params->threshold;
    float threshold2 = threshold * threshold * 1.73;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float left = inputs[0][i], right = inputs[1][i];
        float sig = fabs(left) > fabs(right) ? fabs(left) : fabs(right);

        // Primitive envelope detector - may not work so well with more interesting stereo signals
        float shf1 = cbox_onepolef_process_sample(&m->shifter1, &m->shifter_lp, 0.5 * (left + right));
        float shf2 = cbox_onepolef_process_sample(&m->shifter2, &m->shifter_lp, shf1);
        sig = sig*sig + shf1*shf1 + shf2 * shf2;
        
        // attack - hold - release logic based on signal envelope
        int release = 1;
        float gain = 1.0;
        if (sig < threshold2)
        {
            // hold vs release
            if (m->hold_time >= m->hold_threshold)
            {
                gain = powf(sig / threshold2, 0.5 * (m->params->ratio - 1));
                // gain = powf(sqrt(sig) / threshold, (m->params->ratio - 1));
            }
            else
                m->hold_time++;
        }
        else
        {
            // attack - going to 1 using attack rate
            m->hold_time = 0;
            gain = 1.0;
            release = 0;
        }
        
        gain = cbox_onepolef_process_sample(&m->tracker, release ? &m->release_lp : &m->attack_lp, gain);
                
        outputs[0][i] = left * gain;
        outputs[1][i] = right * gain;
    }
}

MODULE_SIMPLE_DESTROY_FUNCTION(gate)

MODULE_CREATE_FUNCTION(gate)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct gate_module *m = malloc(sizeof(struct gate_module));
    CALL_MODULE_INIT(m, 2, 2, gate);
    m->module.process_event = gate_process_event;
    m->module.process_block = gate_process_block;
    m->hold_time = 0;
    m->hold_threshold = 0;
    
    struct gate_params *p = malloc(sizeof(struct gate_params));
    p->threshold = cbox_config_get_gain_db(cfg_section, "threshold", -28.0);
    p->ratio = cbox_config_get_float(cfg_section, "ratio", 3.0);
    p->attack = cbox_config_get_float(cfg_section, "attack", 3.0);
    p->hold = cbox_config_get_float(cfg_section, "hold", 100.0);
    p->release = cbox_config_get_float(cfg_section, "release", 100.0);
    m->params = p;
    m->old_params = NULL;
    
    cbox_onepolef_reset(&m->tracker);
    cbox_onepolef_reset(&m->shifter1);
    cbox_onepolef_reset(&m->shifter2);

    return &m->module;
}


struct cbox_module_keyrange_metadata gate_keyranges[] = {
};

struct cbox_module_livecontroller_metadata gate_controllers[] = {
};

DEFINE_MODULE(gate, 2, 2)

