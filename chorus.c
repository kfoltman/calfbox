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

struct chorus_module
{
    struct cbox_module module;

    float storage[MAX_CHORUS_LENGTH][2];
    int pos;
    int length;
    int phase, dphase;
};

void chorus_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct chorus_module *m = (struct chorus_module *)module;
}

void chorus_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct chorus_module *m = (struct chorus_module *)module;
    
    int dv = m->length;
    float dry_amt = 0.5f;
    float wet_amt = 0.5f;
    int i, c;
    int mask = MAX_CHORUS_LENGTH - 1;
    
    for (c = 0; c < 2; c++)
    {
        int pos = m->pos;
        for (i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            float dry = inputs[c][i];
            float dva = 21 + (c ? -20 : 20) * sin(2 * M_PI / (65536.0 * 65536.0) * (m->phase + i * m->dphase));
            int dv = (int)dva;
            float frac = dva - dv;
            float smp0 = m->storage[(pos - dv - 1) & mask][c];
            float smp1 = m->storage[(pos - dv) & mask][c];
            
            float smp = smp0 + (smp1 - smp0) * frac;
            
            outputs[c][i] = sanef(dry * dry_amt + smp * wet_amt);

            m->storage[pos & mask][c] = dry;
            pos++;
        }
    }
    
    m->phase += CBOX_BLOCK_SIZE * m->dphase;
    m->pos += CBOX_BLOCK_SIZE;
}

struct cbox_module *chorus_create(void *user_data, const char *cfg_section, int srate)
{
    static int inited = 0;
    int i;
    if (!inited)
    {
        inited = 1;
    }
    
    struct chorus_module *m = malloc(sizeof(struct chorus_module));
    m->module.user_data = m;
    m->module.process_event = chorus_process_event;
    m->module.process_block = chorus_process_block;
    m->pos = 0;
    m->length = srate / 4;
    m->phase = 0;
    m->dphase = (uint32_t)(65536.0 * 65536.0 / srate);
    for (i = 0; i < MAX_CHORUS_LENGTH; i++)
        m->storage[i][0] = m->storage[i][1] = 0.f;
    
    return &m->module;
}


struct cbox_module_keyrange_metadata chorus_keyranges[] = {
};

struct cbox_module_livecontroller_metadata chorus_controllers[] = {
};

DEFINE_MODULE(chorus, 2, 2)

