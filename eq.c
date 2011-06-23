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

#define NO_BANDS 4

struct parametric_eq_module
{
    struct cbox_module module;

    struct cbox_biquadf_state state[NO_BANDS][2];
    struct cbox_biquadf_coeffs coeffs[NO_BANDS];
};

void parametric_eq_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct parametric_eq_module *m = (struct parametric_eq_module *)module;
}

void parametric_eq_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct parametric_eq_module *m = (struct parametric_eq_module *)module;
    int b, c;
    
    for (c = 0; c < 2; c++)
    {
        cbox_biquadf_process_to(&m->state[0][c], &m->coeffs[0], inputs[c], outputs[c]);
    
        for (b = 1; b < NO_BANDS; b++)
        {
            cbox_biquadf_process(&m->state[b][c], &m->coeffs[b], outputs[c]);
        }
    }
}

static float get_band_param(const char *cfg_section, int band, const char *param_name, float def_value)
{
    gchar *key = g_strdup_printf("band%d_%s", band, param_name);
    float value = cbox_config_get_float(cfg_section, key, def_value);
    g_free(key);
    return value;
}

struct cbox_module *parametric_eq_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    int b, c;
    static const float freqs[] = { 150, 400, 1600, 6400 };
    static const float qs[] = { 0.7, 0.7, 0.7, 0.7 };
    static const float gains[] = { 12, -12, 6, 6 };
    
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct parametric_eq_module *m = malloc(sizeof(struct parametric_eq_module));
    cbox_module_init(&m->module, m, 2, 2, NULL);
    m->module.process_event = parametric_eq_process_event;
    m->module.process_block = parametric_eq_process_block;
    
    for (b = 0; b < NO_BANDS; b++)
    {
        cbox_biquadf_set_peakeq_rbj(&m->coeffs[b], 
            get_band_param(cfg_section, b, "freq", freqs[b]),
            get_band_param(cfg_section, b, "q", qs[b]),
            pow(2.0, get_band_param(cfg_section, b, "gain", gains[b]) / 6.0),
            srate);
    }
    
    for (b = 0; b < NO_BANDS; b++)
        for (c = 0; c < 2; c++)
            cbox_biquadf_reset(&m->state[b][c]);
    
    return &m->module;
}


struct cbox_module_keyrange_metadata parametric_eq_keyranges[] = {
};

struct cbox_module_livecontroller_metadata parametric_eq_controllers[] = {
};

DEFINE_MODULE(parametric_eq, 2, 2)

