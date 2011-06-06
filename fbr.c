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

#define MODULE_PARAMS feedback_reducer_params

#define MAX_FBR_BANDS 16

struct fbr_band
{
    gboolean active;
    float center;
    float q;
    float gain;
};

struct feedback_reducer_params
{
    struct fbr_band bands[MAX_FBR_BANDS];
};

struct feedback_reducer_module
{
    struct cbox_module module;
    
    struct feedback_reducer_params *params;

    struct cbox_biquadf_coeffs coeffs[MAX_FBR_BANDS];
    struct cbox_biquadf_state state[2][MAX_FBR_BANDS];
    
    int srate;
};

static void redo_filters(struct feedback_reducer_module *m)
{
    for (int i = 0; i < MAX_FBR_BANDS; i++)
    {
        struct fbr_band *band = &m->params->bands[i];
        if (band->active)
            cbox_biquadf_set_peakeq_rbj(&m->coeffs[i], band->center, band->q, band->gain, m->srate);
    }
}

gboolean feedback_reducer_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct feedback_reducer_module *m = (struct feedback_reducer_module *)ct->user_data;
    
    EFFECT_PARAM_ARRAY("/active", "i", bands, active, int, , 0, 1) else
    EFFECT_PARAM_ARRAY("/center", "f", bands, center, double, , 10, 20000) else
    EFFECT_PARAM_ARRAY("/q", "f", bands, q, double, , 0.01, 100) else
    EFFECT_PARAM_ARRAY("/gain", "f", bands, gain, double, dB2gain_simple, -100, 100) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (int i = 0; i < MAX_FBR_BANDS; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/active", "ii", error, i, (int)m->params->bands[i].active))
                return FALSE;
            if (!cbox_execute_on(fb, NULL, "/center", "if", error, i, m->params->bands[i].center))
                return FALSE;
            if (!cbox_execute_on(fb, NULL, "/q", "if", error, i, m->params->bands[i].q))
                return FALSE;
            if (!cbox_execute_on(fb, NULL, "/gain", "if", error, i, gain2dB_simple(m->params->bands[i].gain)))
                return FALSE;
        }
        // return cbox_execute_on(fb, NULL, "/wet_dry", "f", error, m->params->wet_dry);
        return TRUE;
    }
    else
        return cbox_set_command_error(error, cmd);
    return TRUE;
}

void feedback_reducer_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct feedback_reducer_module *m = module->user_data;
}

void feedback_reducer_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct feedback_reducer_module *m = module->user_data;
    
    for (int c = 0; c < 2; c++)
    {
        gboolean first = TRUE;
        for (int i = 0; i < MAX_FBR_BANDS; i++)
        {
            if (!m->params->bands[i].active)
                continue;
            if (first)
            {
                cbox_biquadf_process_to(&m->state[c][i], &m->coeffs[i], inputs[c], outputs[c]);
                first = FALSE;
            }
            else
            {
                cbox_biquadf_process(&m->state[c][i], &m->coeffs[i], outputs[c]);
            }
        }
        if (first)
            memcpy(outputs[c], inputs[c], sizeof(float) * CBOX_BLOCK_SIZE);
    }
}

struct cbox_module *feedback_reducer_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct feedback_reducer_module *m = malloc(sizeof(struct feedback_reducer_module));
    cbox_module_init(&m->module, m, 2, 2);
    m->module.process_event = feedback_reducer_process_event;
    m->module.process_block = feedback_reducer_process_block;
    m->module.cmd_target.process_cmd = feedback_reducer_process_cmd;
    m->srate = srate;
    struct feedback_reducer_params *p = malloc(sizeof(struct feedback_reducer_params));
    m->params = p;
    
    for (int i = 0; i < MAX_FBR_BANDS; i++)
    {
        p->bands[i].active = TRUE;
        p->bands[i].center = 500 + 700 * i;
        p->bands[i].q = 16;
        p->bands[i].gain = 0.25;
    }
    redo_filters(m);
    
    return &m->module;
}


struct cbox_module_keyrange_metadata feedback_reducer_keyranges[] = {
};

struct cbox_module_livecontroller_metadata feedback_reducer_controllers[] = {
};

DEFINE_MODULE(feedback_reducer, 2, 2)

