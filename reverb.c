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

// The reverb structure is based on this article:
// http://www.spinsemi.com/knowledge_base/effects.html#Reverberation

#define DELAY_BUFFER 1024
#define ALLPASS_BUFFER 2048
#define REVERB_BLOCKS 4
#define ALLPASS_UNITS_PER_BLOCK 3

struct cbox_reverb_block
{
    float allpass_storage[ALLPASS_UNITS_PER_BLOCK][ALLPASS_BUFFER];
    float delay_storage[DELAY_BUFFER];
    struct cbox_onepolef_state filter_state;
};

struct reverb_module
{
    struct cbox_module module;

    struct cbox_reverb_block blocks[REVERB_BLOCKS];
    struct cbox_onepolef_coeffs filter_coeffs[2];
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

    float temp[REVERB_BLOCKS][CBOX_BLOCK_SIZE];
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        temp[0][i] = inputs[0][i];
        temp[1][i] = 0.f;
        temp[2][i] = inputs[1][i];
        temp[3][i] = 0;
    }
    
/*
    static int dvs[REVERB_BLOCKS][ALLPASS_UNITS_PER_BLOCK + 1] = {
        {931, 873, 715, 133},
        {1054, 1519, 973, 461},
        {617, 741, 777, 251},
        {1119, 1477, 933, 379},
    };
    */
    static int dvs[REVERB_BLOCKS][ALLPASS_UNITS_PER_BLOCK + 1] = {
        {731, 873, 1215, 133},
        {1054, 1519, 973, 461},
        {617, 941, 1277, 251},
        {1119, 1477, 933, 379},
    };
    
    float gain = pow(0.001, 3488.0 / rv);
    
    for (int u = 0; u < REVERB_BLOCKS; u++)
    {
        struct cbox_reverb_block *b = &m->blocks[u];
        int uprev = (u + REVERB_BLOCKS - 1) % REVERB_BLOCKS;
        struct cbox_reverb_block *bprev = &m->blocks[uprev];

        int pos;
        int dv;
        float *storage;
        
        pos = m->pos;
        storage = bprev->delay_storage;
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            temp[u][i] += cbox_onepolef_process_sample(&b->filter_state, &m->filter_coeffs[u&1], storage[pos & (DELAY_BUFFER - 1)]) * gain;
            pos++;
        }

        float w = 0.45;
        for (int a = 0; a < ALLPASS_UNITS_PER_BLOCK; a++)
        {
            pos = m->pos;
            storage = b->allpass_storage[a];
            dv = dvs[u][a];
            for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
            {
                float dry = temp[u][i];
                float out = dry;
            
                float delayed = storage[pos & (ALLPASS_BUFFER - 1)];
                
                float feedback = sanef(out - w * delayed);
                
                temp[u][i] = sanef(feedback * w + delayed);
                
                storage[(pos + dv) & (ALLPASS_BUFFER - 1)] = feedback;
                pos++;
            }
        }
        pos = m->pos;
        storage = b->delay_storage;
        dv = dvs[u][ALLPASS_UNITS_PER_BLOCK];
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            storage[(pos + dv) & (DELAY_BUFFER - 1)] = temp[u][i];
            pos++;
        }
    }
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        outputs[0][i] = inputs[0][i] * dryamt + temp[1][i] * wetamt;
        outputs[1][i] = inputs[1][i] * dryamt + temp[3][i] * wetamt;
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
    cbox_module_init(&m->module, m, 2, 2);
    m->module.process_event = reverb_process_event;
    m->module.process_block = reverb_process_block;
    m->pos = 0;
    m->length = cbox_config_get_float(cfg_section, "reverb_time", 1000) * srate / 1000;
    m->dryamt = cbox_config_get_gain_db(cfg_section, "dry_gain", 0.f);
    m->wetamt = cbox_config_get_gain_db(cfg_section, "wet_gain", -6.f);
    for (int u = 0; u < REVERB_BLOCKS; u++)
    {
        struct cbox_reverb_block *b = &m->blocks[u];
        cbox_onepolef_reset(&b->filter_state);
        for (int a = 0; a < ALLPASS_UNITS_PER_BLOCK; a++)
            for (i = 0; i < ALLPASS_BUFFER; i++)
                b->allpass_storage[a][i] = 0.f;
        for (i = 0; i < DELAY_BUFFER; i++)
            b->delay_storage[i] = 0.f;
    }
    
    float tpdsr = 2 * M_PI / srate;
    
    cbox_onepolef_set_lowpass(&m->filter_coeffs[0], cbox_config_get_float(cfg_section, "lowpass", 8000.f) * tpdsr);
    cbox_onepolef_set_highpass(&m->filter_coeffs[1], cbox_config_get_float(cfg_section, "highpass", 35.f) * tpdsr);
    
    return &m->module;
}

struct cbox_module_keyrange_metadata reverb_keyranges[] = {
};

struct cbox_module_livecontroller_metadata reverb_controllers[] = {
};

DEFINE_MODULE(reverb, 2, 2)

