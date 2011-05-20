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

#define MAX_CHORUS_LENGTH 4096

static float sine_table[2049];

struct chorus_module
{
    struct cbox_module module;

    float storage[MAX_CHORUS_LENGTH][2];
    int pos;
    float lfo_freq;
    float min_delay;
    float mod_depth;
    float dryamt;
    float wetamt;
    float tp32dsr;
    uint32_t phase, sphase;
};

void chorus_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct chorus_module *m = (struct chorus_module *)module;
}

void chorus_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct chorus_module *m = (struct chorus_module *)module;
    
    float min_delay = m->min_delay;
    float mod_depth = m->mod_depth;
    float dryamt = m->dryamt;
    float wetamt = m->wetamt;
    int i, c;
    int mask = MAX_CHORUS_LENGTH - 1;
    uint32_t dphase = (uint32_t)(m->lfo_freq * m->tp32dsr);
    const int fracbits = 32 - 11;
    const int fracscale = 1 << fracbits;
    
    for (c = 0; c < 2; c++)
    {
        int pos = m->pos;
        uint32_t phase = m->phase + c * m->sphase;
        for (i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            float dry = inputs[c][i];
            float v0 = sine_table[phase >> fracbits];
            float v1 = sine_table[1 + (phase >> fracbits)];
            float lfo = v0 + (v1 - v0) * ((phase & (fracscale - 1)) * (1.0 / fracscale));

            m->storage[pos & mask][c] = dry;

            float dva = min_delay + mod_depth * lfo;
            int dv = (int)dva;
            float frac = dva - dv;
            float smp0 = m->storage[(pos - dv) & mask][c];
            float smp1 = m->storage[(pos - dv - 1) & mask][c];
            
            float smp = smp0 + (smp1 - smp0) * frac;
            
            outputs[c][i] = sanef(dry * dryamt + smp * wetamt);

            pos++;
            phase += dphase;
        }
    }
    
    m->phase += CBOX_BLOCK_SIZE * dphase;
    m->pos += CBOX_BLOCK_SIZE;
}

struct cbox_module *chorus_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    int i;
    if (!inited)
    {
        inited = 1;
        for (i = 0; i < 2049; i++)
            sine_table[i] = sin(i * M_PI / 1024);
    }
    
    struct chorus_module *m = malloc(sizeof(struct chorus_module));
    cbox_module_init(&m->module, m);
    m->module.process_event = chorus_process_event;
    m->module.process_block = chorus_process_block;
    m->pos = 0;
    m->phase = 0;
    m->tp32dsr = 65536.0 * 65536.0 / srate;
    m->lfo_freq = cbox_config_get_float(cfg_section, "lfo_freq", 1.f);
    m->min_delay = cbox_config_get_float(cfg_section, "min_delay", 20.f);
    m->mod_depth = cbox_config_get_float(cfg_section, "mod_depth", 15.f);
    m->dryamt = cbox_config_get_gain_db(cfg_section, "dry_gain", 0.f);
    m->wetamt = cbox_config_get_gain_db(cfg_section, "wet_gain", -6.f);
    for (i = 0; i < MAX_CHORUS_LENGTH; i++)
        m->storage[i][0] = m->storage[i][1] = 0.f;
    
    return &m->module;
}


struct cbox_module_keyrange_metadata chorus_keyranges[] = {
};

struct cbox_module_livecontroller_metadata chorus_controllers[] = {
};

DEFINE_MODULE(chorus, 2, 2)

