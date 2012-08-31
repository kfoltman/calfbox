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
#include "eq.h"
#include "module.h"
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

#define MODULE_PARAMS parametric_eq_params
#define MAX_EQ_BANDS 4

struct parametric_eq_params
{
    struct eq_band bands[MAX_EQ_BANDS];
};

struct parametric_eq_module
{
    struct cbox_module module;

    struct parametric_eq_params *params, *old_params;

    struct cbox_biquadf_state state[MAX_EQ_BANDS][2];
    struct cbox_biquadf_coeffs coeffs[MAX_EQ_BANDS];
};

static void redo_filters(struct parametric_eq_module *m)
{
    for (int i = 0; i < MAX_EQ_BANDS; i++)
    {
        struct eq_band *band = &m->params->bands[i];
        if (band->active)
        {
            cbox_biquadf_set_peakeq_rbj(&m->coeffs[i], band->center, band->q, band->gain, m->module.srate);
        }
    }
    m->old_params = m->params;
}

gboolean parametric_eq_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct parametric_eq_module *m = (struct parametric_eq_module *)ct->user_data;
    
    EFFECT_PARAM_ARRAY("/active", "i", bands, active, int, , 0, 1) else
    EFFECT_PARAM_ARRAY("/center", "f", bands, center, double, , 10, 20000) else
    EFFECT_PARAM_ARRAY("/q", "f", bands, q, double, , 0.01, 100) else
    EFFECT_PARAM_ARRAY("/gain", "f", bands, gain, double, dB2gain_simple, -100, 100) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        for (int i = 0; i < MAX_EQ_BANDS; i++)
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

void parametric_eq_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct parametric_eq_module *m = (struct parametric_eq_module *)module;
}

void parametric_eq_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct parametric_eq_module *m = (struct parametric_eq_module *)module;
    int b, c;
    
    if (m->params != m->old_params)
        redo_filters(m);
    
    for (int c = 0; c < 2; c++)
    {
        gboolean first = TRUE;
        for (int i = 0; i < MAX_EQ_BANDS; i++)
        {
            if (!m->params->bands[i].active)
                continue;
            if (first)
            {
                cbox_biquadf_process_to(&m->state[i][c], &m->coeffs[i], inputs[c], outputs[c]);
                first = FALSE;
            }
            else
            {
                cbox_biquadf_process(&m->state[i][c], &m->coeffs[i], outputs[c]);
            }
        }
        if (first)
            memcpy(outputs[c], inputs[c], sizeof(float) * CBOX_BLOCK_SIZE);
    }
}

float cbox_eq_get_band_param(const char *cfg_section, int band, const char *param, float defvalue)
{
    gchar *s = g_strdup_printf("band%d_%s", band + 1, param);
    float v = cbox_config_get_float(cfg_section, s, defvalue);
    g_free(s);
    
    return v;
}

float cbox_eq_get_band_param_db(const char *cfg_section, int band, const char *param, float defvalue)
{
    gchar *s = g_strdup_printf("band%d_%s", band + 1, param);
    float v = cbox_config_get_gain_db(cfg_section, s, defvalue);
    g_free(s);
    
    return v;
}

void cbox_eq_reset_bands(struct cbox_biquadf_state state[1][2], int bands)
{
    for (int b = 0; b < MAX_EQ_BANDS; b++)
        for (int c = 0; c < 2; c++)
            cbox_biquadf_reset(&state[b][c]);
}

MODULE_CREATE_FUNCTION(parametric_eq_create)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct parametric_eq_module *m = malloc(sizeof(struct parametric_eq_module));
    CALL_MODULE_INIT(m, 2, 2, parametric_eq_process_cmd);
    m->module.process_event = parametric_eq_process_event;
    m->module.process_block = parametric_eq_process_block;
    struct parametric_eq_params *p = malloc(sizeof(struct parametric_eq_params));
    m->params = p;
    m->old_params = NULL;
    
    for (int b = 0; b < MAX_EQ_BANDS; b++)
    {
        p->bands[b].active = cbox_eq_get_band_param(cfg_section, b, "active", 0) > 0;
        p->bands[b].center = cbox_eq_get_band_param(cfg_section, b, "center", 50 * pow(4.0, b));
        p->bands[b].q = cbox_eq_get_band_param(cfg_section, b, "q", 0.707);
        p->bands[b].gain = cbox_eq_get_band_param_db(cfg_section, b, "gain", 0);
    }
    
    cbox_eq_reset_bands(m->state, MAX_EQ_BANDS);
    
    return &m->module;
}


struct cbox_module_keyrange_metadata parametric_eq_keyranges[] = {
};

struct cbox_module_livecontroller_metadata parametric_eq_controllers[] = {
};

DEFINE_MODULE(parametric_eq, 2, 2)

