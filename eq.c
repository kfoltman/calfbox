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

void parametric_eq_process_event(void *user_data, const uint8_t *data, uint32_t len)
{
    struct parametric_eq_module *m = user_data;
}

void parametric_eq_process_block(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct parametric_eq_module *m = user_data;
    int b, i, c;
    
    float data[2][CBOX_BLOCK_SIZE];
    for (c = 0; c < 2; c++)
        for (i = 0; i < CBOX_BLOCK_SIZE; i++)
            data[c][i] = inputs[c][i];
    
    for (b = 0; b < NO_BANDS; b++)
    {
        for (c = 0; c < 2; c++)
        {
            cbox_biquadf_process(&m->state[b][c], &m->coeffs[b], &data[c][0]);
        }
    }

    for (c = 0; c < 2; c++)
        for (i = 0; i < CBOX_BLOCK_SIZE; i++)
            outputs[c][i] = data[c][i];
}

struct cbox_module *parametric_eq_create(void *user_data, const char *cfg_section)
{
    int b, c;
    
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct parametric_eq_module *m = malloc(sizeof(struct parametric_eq_module));
    m->module.user_data = m;
    m->module.process_event = parametric_eq_process_event;
    m->module.process_block = parametric_eq_process_block;
    
    cbox_biquadf_set_peakeq_rbj(&m->coeffs[0], 150, 0.5, 3, 44100);
    cbox_biquadf_set_peakeq_rbj(&m->coeffs[1], 400, 0.7, 0.125, 44100);
    cbox_biquadf_set_peakeq_rbj(&m->coeffs[2], 1600, 2, 4, 44100);
    cbox_biquadf_set_peakeq_rbj(&m->coeffs[3], 6400, 1, 4, 44100);
    
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

