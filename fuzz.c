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

#include "biquad-float.h"
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

#define MODULE_PARAMS fuzz_params

struct fuzz_params
{
    float drive;
    float wet_dry;
    float rectify;
    float band;
    float bandwidth;
    float band2;
    float bandwidth2;
};

struct fuzz_module
{
    struct cbox_module module;

    struct fuzz_params *params, *old_params;
    
    struct cbox_biquadf_coeffs split_coeffs;
    struct cbox_biquadf_coeffs post_coeffs;
    struct cbox_biquadf_state split_state[2];
    struct cbox_biquadf_state post_state[2];
};

gboolean fuzz_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct fuzz_module *m = (struct fuzz_module *)ct->user_data;
    
    EFFECT_PARAM("/drive", "f", drive, double, dB2gain_simple, -36, 36) else
    EFFECT_PARAM("/wet_dry", "f", wet_dry, double, , 0, 1) else
    EFFECT_PARAM("/rectify", "f", rectify, double, , 0, 1) else
    EFFECT_PARAM("/band", "f", band, double, , 100, 5000) else
    EFFECT_PARAM("/bandwidth", "f", bandwidth, double, , 0.25, 4) else
    EFFECT_PARAM("/band2", "f", band2, double, , 100, 5000) else
    EFFECT_PARAM("/bandwidth2", "f", bandwidth2, double, , 0.25, 4) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/drive", "f", error, gain2dB_simple(m->params->drive))
            && cbox_execute_on(fb, NULL, "/wet_dry", "f", error, m->params->wet_dry)
            && cbox_execute_on(fb, NULL, "/rectify", "f", error, m->params->rectify)
            && cbox_execute_on(fb, NULL, "/band", "f", error, m->params->band)
            && cbox_execute_on(fb, NULL, "/bandwidth", "f", error, m->params->bandwidth)
            && cbox_execute_on(fb, NULL, "/band2", "f", error, m->params->band2)
            && cbox_execute_on(fb, NULL, "/bandwidth2", "f", error, m->params->bandwidth2)
            && CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error)
        ;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

void fuzz_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    // struct fuzz_module *m = module->user_data;
}

void fuzz_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct fuzz_module *m = module->user_data;
    
    if (m->params != m->old_params)
    {
        // update calculated values
    }
    
    cbox_biquadf_set_bp_rbj(&m->split_coeffs, m->params->band, 0.7 / m->params->bandwidth, m->module.srate);
    cbox_biquadf_set_bp_rbj(&m->post_coeffs, m->params->band2, 0.7 / m->params->bandwidth2, m->module.srate);
    
    float splitbuf[2][CBOX_BLOCK_SIZE];
    float drive = m->params->drive;
    float sdrive = pow(drive, -0.7);
    for (int c = 0; c < 2; c++)
    {
        cbox_biquadf_process_to(&m->split_state[c], &m->split_coeffs, inputs[c], splitbuf[c]);
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            float in = inputs[c][i];
            
            float val = splitbuf[c][i];
            
            val *= drive;
            
            val += m->params->rectify;
            if (fabs(val) > 1.0)
                val = (val > 0) ? 1 : -1;
            else
                val = val * (3 - val * val) * 0.5;
            
            val *= sdrive;
            
            val = cbox_biquadf_process_sample(&m->post_state[c], &m->post_coeffs, val);
            
            outputs[c][i] = in + (val - in) * m->params->wet_dry;
        }
    }
}

MODULE_SIMPLE_DESTROY_FUNCTION(fuzz)

MODULE_CREATE_FUNCTION(fuzz)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct fuzz_module *m = malloc(sizeof(struct fuzz_module));
    CALL_MODULE_INIT(m, 2, 2, fuzz);
    m->module.process_event = fuzz_process_event;
    m->module.process_block = fuzz_process_block;
    struct fuzz_params *p = malloc(sizeof(struct fuzz_params));
    p->drive = cbox_config_get_gain_db(cfg_section, "drive", 0.f);
    p->wet_dry = cbox_config_get_float(cfg_section, "wet_dry", 0.5f);
    p->rectify = cbox_config_get_float(cfg_section, "rectify", 0.5f);
    p->band = cbox_config_get_float(cfg_section, "band", 1000.f);
    p->bandwidth = cbox_config_get_float(cfg_section, "bandwidth", 1);
    p->band2 = cbox_config_get_float(cfg_section, "band2", 2000.f);
    p->bandwidth2 = cbox_config_get_float(cfg_section, "bandwidth2", 1);
    m->params = p;
    m->old_params = NULL;
    cbox_biquadf_reset(&m->split_state[0]);
    cbox_biquadf_reset(&m->split_state[1]);
    cbox_biquadf_reset(&m->post_state[0]);
    cbox_biquadf_reset(&m->post_state[1]);
    
    return &m->module;
}


struct cbox_module_keyrange_metadata fuzz_keyranges[] = {
};

struct cbox_module_livecontroller_metadata fuzz_controllers[] = {
};

DEFINE_MODULE(fuzz, 2, 2)

