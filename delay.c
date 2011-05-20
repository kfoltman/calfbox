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

#define MAX_DELAY_LENGTH 65536

struct delay_module
{
    struct cbox_module module;

    float storage[MAX_DELAY_LENGTH][2];
    int pos;
    int length;
    float wetamt, dryamt, fbamt;
};

void delay_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct delay_module *m = (struct delay_module *)module;
}

void delay_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct delay_module *m = (struct delay_module *)module;
    
    int pos = m->pos ;
    int dv = m->length;
    float dryamt = m->dryamt;
    float wetamt = m->wetamt;
    float fbamt = m->fbamt;
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float dry[2] = { inputs[0][i], inputs[1][i] };
        float *delayed = &m->storage[pos & (MAX_DELAY_LENGTH - 1)][0];
        
        float wet[2] = { dryamt * dry[0] + wetamt * delayed[0], dryamt * dry[1] + wetamt * delayed[1] };
        float fb[2] = { dry[0] + fbamt * delayed[0], dry[1] + fbamt * delayed[1] };
        outputs[0][i] = sanef(wet[0]);
        outputs[1][i] = sanef(wet[1]);
        
        float *wcell = &m->storage[(pos + dv) & (MAX_DELAY_LENGTH - 1)][0];
        wcell[0] = sanef(fb[0]);
        wcell[1] = sanef(fb[1]);
        pos++;
    }
    m->pos = pos;
}

struct cbox_module *delay_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    int i;
    if (!inited)
    {
        inited = 1;
    }
    
    struct delay_module *m = malloc(sizeof(struct delay_module));
    cbox_module_init(&m->module, m);
    m->module.process_event = delay_process_event;
    m->module.process_block = delay_process_block;
    m->pos = 0;
    m->length = cbox_config_get_float(cfg_section, "delay", 250) * srate / 1000;
    m->dryamt = cbox_config_get_gain_db(cfg_section, "dry_gain", 1.f);
    m->wetamt = cbox_config_get_gain_db(cfg_section, "wet_gain", -6.f);
    m->fbamt = cbox_config_get_gain_db(cfg_section, "feedback_gain", -12.f);
    for (i = 0; i < MAX_DELAY_LENGTH; i++)
        m->storage[i][0] = m->storage[i][1] = 0.f;
    
    return &m->module;
}

struct cbox_module_keyrange_metadata delay_keyranges[] = {
};

struct cbox_module_livecontroller_metadata delay_controllers[] = {
};

DEFINE_MODULE(delay, 2, 2)

