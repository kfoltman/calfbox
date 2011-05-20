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

#define MAX_REVERB_LENGTH 1024
#define ALLPASS_UNITS 9

struct reverb_module
{
    struct cbox_module module;

    float storage[ALLPASS_UNITS][MAX_REVERB_LENGTH];
    int pos;
    int length;
    float wetamt, dryamt;
};

void reverb_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct reverb_module *m = (struct reverb_module *)module;
}

void reverb_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct reverb_module *m = (struct reverb_module *)module;
    
    int rv = m->length;
    float dryamt = m->dryamt;
    float wetamt = m->wetamt;

    static int dvs[] = { 3*23, 5*13, 7*17,    231, 383, 431, 523, 643, 670 };
    float temp[CBOX_BLOCK_SIZE];
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        temp[i] = 0.5 * (inputs[0][i] + inputs[1][i]);
    }
    
    for (int u = 0; u < ALLPASS_UNITS; u++)
    {
        int pos = m->pos;
        int dv = dvs[u];
        float w = pow(0.001, dv * 1.0 / rv);
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            float dry = temp[i];
            float out = dry;
        
            float delayed = m->storage[u][pos & (MAX_REVERB_LENGTH - 1)];
            
            float feedback = sanef(out - w * delayed);
            
            temp[i] = sanef(feedback * w + delayed);
            
            m->storage[u][(pos + dv) & (MAX_REVERB_LENGTH - 1)] = feedback;
            pos++;
        }
            
    }
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float out = temp[i];
        outputs[0][i] = inputs[0][i] * dryamt + out * wetamt;
        outputs[1][i] = inputs[1][i] * dryamt + out * wetamt;        
    }
    m->pos += CBOX_BLOCK_SIZE;
}

struct cbox_module *reverb_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    int i;
    if (!inited)
    {
        inited = 1;
    }
    
    struct reverb_module *m = malloc(sizeof(struct reverb_module));
    cbox_module_init(&m->module, m);
    m->module.process_event = reverb_process_event;
    m->module.process_block = reverb_process_block;
    m->pos = 0;
    m->length = cbox_config_get_float(cfg_section, "reverb_time", 1000) * srate / 1000;
    m->dryamt = cbox_config_get_gain_db(cfg_section, "dry_gain", 0.f);
    m->wetamt = cbox_config_get_gain_db(cfg_section, "wet_gain", -6.f);
    for (int u = 0; u < ALLPASS_UNITS; u++)
        for (i = 0; i < MAX_REVERB_LENGTH; i++)
            m->storage[u][i] = 0.f;
    
    return &m->module;
}

struct cbox_module_keyrange_metadata reverb_keyranges[] = {
};

struct cbox_module_livecontroller_metadata reverb_controllers[] = {
};

DEFINE_MODULE(reverb, 2, 2)

