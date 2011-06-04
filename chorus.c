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

#define MODULE_PARAMS chorus_params

static float sine_table[2049];

struct chorus_params
{
    float lfo_freq;
    float min_delay;
    float mod_depth;
    float wet_dry;
    float sphase;
};

struct chorus_module
{
    struct cbox_module module;

    float storage[MAX_CHORUS_LENGTH][2];
    struct chorus_params *params;
    int pos;
    float tp32dsr;
    uint32_t phase;
};

gboolean chorus_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct chorus_module *m = (struct chorus_module *)ct->user_data;
    
    EFFECT_PARAM("/min_delay", "f", min_delay, double, , 1, 20) else
    EFFECT_PARAM("/mod_depth", "f", mod_depth, double, , 1, 20) else
    EFFECT_PARAM("/lfo_freq", "f", lfo_freq, double, , 0, 20) else
    EFFECT_PARAM("/stereo_phase", "f", sphase, double, , 0, 360) else
    EFFECT_PARAM("/wet_dry", "f", wet_dry, double, , 0, 1) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/min_delay", "f", error, m->params->min_delay) &&
            cbox_execute_on(fb, NULL, "/mod_depth", "f", error, m->params->mod_depth) &&
            cbox_execute_on(fb, NULL, "/lfo_freq", "f", error, m->params->lfo_freq) &&
            cbox_execute_on(fb, NULL, "/stereo_phase", "f", error, m->params->sphase) &&
            cbox_execute_on(fb, NULL, "/wet_dry", "f", error, m->params->wet_dry);
    }
    else
        return cbox_set_command_error(error, cmd);
    return TRUE;
}

void chorus_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct chorus_module *m = (struct chorus_module *)module;
}

void chorus_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct chorus_module *m = (struct chorus_module *)module;
    struct chorus_params *p = m->params;
    
    float min_delay = p->min_delay;
    float mod_depth = p->mod_depth;
    float wet_dry = p->wet_dry;
    int i, c;
    int mask = MAX_CHORUS_LENGTH - 1;
    uint32_t sphase = (uint32_t)(p->sphase * 65536.0 * 65536.0 / 360);
    uint32_t dphase = (uint32_t)(p->lfo_freq * m->tp32dsr);
    const int fracbits = 32 - 11;
    const int fracscale = 1 << fracbits;
    
    for (c = 0; c < 2; c++)
    {
        int pos = m->pos;
        uint32_t phase = m->phase + c * sphase;
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
            
            outputs[c][i] = sanef(dry + (smp - dry) * wet_dry);

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
            sine_table[i] = 1 + sin(i * M_PI / 1024);
    }
    
    struct chorus_module *m = malloc(sizeof(struct chorus_module));
    cbox_module_init(&m->module, m, 2, 2);
    m->module.process_event = chorus_process_event;
    m->module.process_block = chorus_process_block;
    m->module.cmd_target.process_cmd = chorus_process_cmd;
    m->pos = 0;
    m->phase = 0;
    m->tp32dsr = 65536.0 * 65536.0 / srate;
    struct chorus_params *p = malloc(sizeof(struct chorus_params));
    m->params = p;
    p->sphase = cbox_config_get_float(cfg_section, "stereo_phase", 90.f);
    p->lfo_freq = cbox_config_get_float(cfg_section, "lfo_freq", 1.f);
    p->min_delay = cbox_config_get_float(cfg_section, "min_delay", 20.f);
    p->mod_depth = cbox_config_get_float(cfg_section, "mod_depth", 15.f);
    p->wet_dry = cbox_config_get_float(cfg_section, "wet_dry", 0.5f);
    for (i = 0; i < MAX_CHORUS_LENGTH; i++)
        m->storage[i][0] = m->storage[i][1] = 0.f;
    
    return &m->module;
}


struct cbox_module_keyrange_metadata chorus_keyranges[] = {
};

struct cbox_module_livecontroller_metadata chorus_controllers[] = {
};

DEFINE_MODULE(chorus, 2, 2)

