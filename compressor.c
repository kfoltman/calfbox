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

#define MODULE_PARAMS compressor_params

struct compressor_params
{
    float threshold;
    float ratio;
    float attack;
    float release;
    float makeup;
};

struct compressor_module
{
    struct cbox_module module;

    struct compressor_params *params, *old_params;
    struct cbox_onepolef_coeffs attack_lp, release_lp, fast_attack_lp;
    struct cbox_onepolef_state tracker;
    struct cbox_onepolef_state tracker2;
};

MODULE_PROCESSCMD_FUNCTION(compressor)
{
    struct compressor_module *m = (struct compressor_module *)ct->user_data;
    
    EFFECT_PARAM("/makeup", "f", makeup, double, dB2gain_simple, -100, 100) else
    EFFECT_PARAM("/threshold", "f", threshold, double, dB2gain_simple, -100, 100) else
    EFFECT_PARAM("/ratio", "f", ratio, double, , 1, 100) else
    EFFECT_PARAM("/attack", "f", attack, double, , 1, 1000) else
    EFFECT_PARAM("/release", "f", release, double, , 1, 1000) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/makeup", "f", error, gain2dB_simple(m->params->makeup))
            && cbox_execute_on(fb, NULL, "/threshold", "f", error, gain2dB_simple(m->params->threshold))
            && cbox_execute_on(fb, NULL, "/ratio", "f", error, m->params->ratio)
            && cbox_execute_on(fb, NULL, "/attack", "f", error, m->params->attack)
            && cbox_execute_on(fb, NULL, "/release", "f", error, m->params->release)
            && CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error)
            ;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

void compressor_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    // struct compressor_module *m = module->user_data;
}

void compressor_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct compressor_module *m = module->user_data;
    
    if (m->params != m->old_params)
    {
        float scale = M_PI * 1000 / m->module.srate;
        cbox_onepolef_set_lowpass(&m->fast_attack_lp, 2 * scale / m->params->attack);
        cbox_onepolef_set_lowpass(&m->attack_lp, scale / m->params->attack);
        cbox_onepolef_set_lowpass(&m->release_lp, scale / m->params->release);
        m->old_params = m->params;
    }
    
    float threshold = m->params->threshold, invratio = 1.0 / m->params->ratio;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float left = inputs[0][i], right = inputs[1][i];
        float sig = 0.5 * (fabs(left) > fabs(right) ? fabs(left) : fabs(right));
        
        int falling = sig < m->tracker.y1 && sig < m->tracker.x1;
        int rising_fast = sig > 4 * m->tracker.y1 && sig > 4 * m->tracker.x1;
        sig = cbox_onepolef_process_sample(&m->tracker, falling ? &m->release_lp : (rising_fast * m->tracker.y1 ? &m->fast_attack_lp : &m->attack_lp), sig);
        sig = cbox_onepolef_process_sample(&m->tracker2, falling ? &m->release_lp : (rising_fast * m->tracker2.y1 ? &m->fast_attack_lp : &m->attack_lp), sig);
        float gain = 1.0;
        if (sig > threshold)
            gain = threshold * powf(sig / threshold, invratio) / sig;
        gain *= m->params->makeup;
                
        outputs[0][i] = left * gain;
        outputs[1][i] = right * gain;
    }
}

MODULE_SIMPLE_DESTROY_FUNCTION(compressor)

MODULE_CREATE_FUNCTION(compressor)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct compressor_module *m = malloc(sizeof(struct compressor_module));
    CALL_MODULE_INIT(m, 2, 2, compressor);
    m->module.process_event = compressor_process_event;
    m->module.process_block = compressor_process_block;
    
    struct compressor_params *p = malloc(sizeof(struct compressor_params));
    p->threshold = cbox_config_get_gain_db(cfg_section, "threshold", -12.0);
    p->ratio = cbox_config_get_float(cfg_section, "ratio", 2.0);
    p->attack = cbox_config_get_float(cfg_section, "attack", 5.0);
    p->release = cbox_config_get_float(cfg_section, "release", 100.0);
    p->makeup = cbox_config_get_gain_db(cfg_section, "makeup", 6.0);
    m->params = p;
    m->old_params = NULL;
    
    cbox_onepolef_reset(&m->tracker);
    cbox_onepolef_reset(&m->tracker2);

    return &m->module;
}


struct cbox_module_keyrange_metadata compressor_keyranges[] = {
};

struct cbox_module_livecontroller_metadata compressor_controllers[] = {
};

DEFINE_MODULE(compressor, 2, 2)

