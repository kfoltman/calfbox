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

#include "onepole-float.h"
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

#define NO_STAGES 12

struct phaser_module
{
    struct cbox_module module;

    struct cbox_onepolef_state state[NO_STAGES][2];
    struct cbox_onepolef_coeffs coeffs[2];
    float fb[2];
    float tpdsr;
};

void phaser_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct phaser_module *m = (struct phaser_module *)module;
}

void phaser_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct phaser_module *m = (struct phaser_module *)module;
    int s, c, i;
    int stages = NO_STAGES;
    float center = 600;
    float mdepth = 580;
    float fb_amt = 0;
    float dphase = M_PI / 2;
    static float phase = 0;

    cbox_onepolef_set_allpass(&m->coeffs[0], m->tpdsr * (center + mdepth * sin(phase)));
    cbox_onepolef_set_allpass(&m->coeffs[1], m->tpdsr * (center + mdepth * sin(phase + dphase)));
    phase += 0.002;
    
    for (c = 0; c < 2; c++)
    {
        for (i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            float dry = inputs[c][i];
            float wet = dry - m->fb[c] * fb_amt;
            for (s = 0; s < stages; s++)
                wet = cbox_onepolef_process_sample(&m->state[s][c], &m->coeffs[c], wet);
            m->fb[c] = wet;
            outputs[c][i] = dry + wet;
        }
    }
}

struct cbox_module *phaser_create(void *user_data, const char *cfg_section, int srate)
{
    int b, c;
    
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct phaser_module *m = malloc(sizeof(struct phaser_module));
    m->module.user_data = m;
    m->module.process_event = phaser_process_event;
    m->module.process_block = phaser_process_block;
    m->tpdsr = 2.0 * M_PI / srate;
    
    for (b = 0; b < NO_STAGES; b++)
        for (c = 0; c < 2; c++)
            cbox_onepolef_reset(&m->state[b][c]);
    
    return &m->module;
}


struct cbox_module_keyrange_metadata phaser_keyranges[] = {
};

struct cbox_module_livecontroller_metadata phaser_controllers[] = {
};

DEFINE_MODULE(phaser, 2, 2)

