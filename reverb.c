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

struct cbox_reverb_leg_params
{
    int delay_length;
    int allpass_units;
    int allpass_lengths[0];
};

struct cbox_reverb_leg
{
    struct cbox_reverb_leg_params *params;
    float (*allpass_storage)[ALLPASS_BUFFER];
    float delay_storage[DELAY_BUFFER];
    struct cbox_onepolef_state filter_state;
    float buffer[CBOX_BLOCK_SIZE];
};

static struct cbox_reverb_leg_params *leg_params_new(int delay_length, int allpasses, const int *lengths)
{
    struct cbox_reverb_leg_params *p = malloc(sizeof(struct cbox_reverb_leg_params) + sizeof(int) * allpasses);
    p->delay_length = delay_length;
    p->allpass_units = allpasses;
    
    if (lengths)
    {
        for (int i = 0; i < allpasses; i++)
            p->allpass_lengths[i] = lengths[i];
    }

    return p;
}

static void cbox_reverb_leg_reset(struct cbox_reverb_leg *leg)
{
    cbox_onepolef_reset(&leg->filter_state);
    int i;
    for (int a = 0; a < leg->params->allpass_units; a++)
        for (i = 0; i < ALLPASS_BUFFER; i++)
            leg->allpass_storage[a][i] = 0.f;
    for (i = 0; i < DELAY_BUFFER; i++)
        leg->delay_storage[i] = 0.f;
}

static void cbox_reverb_leg_init(struct cbox_reverb_leg *leg)
{
    leg->allpass_storage = malloc(leg->params->allpass_units * ALLPASS_BUFFER * sizeof(float));
    cbox_reverb_leg_reset(leg);
}

static void cbox_reverb_leg_cleanup(struct cbox_reverb_leg *leg)
{
    free(leg->params);
    free(leg->allpass_storage);
}

#define MODULE_PARAMS reverb_params

struct reverb_params
{
    float decay_time;
    float wetamt;
    float dryamt;
    float lowpass, highpass;
    float diffusion;
};

struct reverb_state
{
    struct cbox_reverb_leg *legs;
    int leg_count;
};

struct reverb_module
{
    struct cbox_module module;

    struct cbox_onepolef_coeffs filter_coeffs[2];
    struct reverb_params *params, *old_params;
    struct reverb_state state;
    float gain;
    int pos;
};

gboolean reverb_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct reverb_module *m = (struct reverb_module *)ct->user_data;
    
    EFFECT_PARAM("/wet_amt", "f", wetamt, double, dB2gain_simple, -100, 100) else
    EFFECT_PARAM("/dry_amt", "f", dryamt, double, dB2gain_simple, -100, 100) else
    EFFECT_PARAM("/decay_time", "f", decay_time, double, , 500, 5000) else
    EFFECT_PARAM("/diffusion", "f", diffusion, double, , 0, 1) else
    EFFECT_PARAM("/lowpass", "f", lowpass, double, , 30, 20000) else
    EFFECT_PARAM("/highpass", "f", highpass, double, , 30, 20000) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/wet_amt", "f", error, gain2dB_simple(m->params->wetamt)) &&
            cbox_execute_on(fb, NULL, "/dry_amt", "f", error, gain2dB_simple(m->params->dryamt)) &&
            cbox_execute_on(fb, NULL, "/decay_time", "f", error, m->params->decay_time) &&
            cbox_execute_on(fb, NULL, "/diffusion", "f", error, m->params->diffusion) &&
            cbox_execute_on(fb, NULL, "/lowpass", "f", error, m->params->lowpass) &&
            cbox_execute_on(fb, NULL, "/highpass", "f", error, m->params->highpass);
    }
    else
        return cbox_set_command_error(error, cmd);
    return TRUE;
}

void reverb_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct reverb_module *m = (struct reverb_module *)module;
}

static void cbox_reverb_process_leg(struct reverb_module *m, int u)
{
    int pos;
    int dv;
    float *storage;
    
    struct reverb_params *p = m->params;
    struct reverb_state *state = &m->state;
    struct cbox_reverb_leg *b = &state->legs[u];
    int uprev = u ? (u - 1) : (state->leg_count - 1);
    struct cbox_reverb_leg *bprev = &state->legs[uprev];    
    
    float gain = m->gain;
    pos = m->pos;
    storage = bprev->delay_storage;
    float *buf = b->buffer;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        buf[i] += cbox_onepolef_process_sample(&b->filter_state, &m->filter_coeffs[u&1], storage[pos & (DELAY_BUFFER - 1)] * gain);
        pos++;
    }

    float w = p->diffusion;
    int units = b->params->allpass_units;
    for (int a = 0; a < units; a++)
    {
        pos = m->pos;
        storage = b->allpass_storage[a];
        dv = b->params->allpass_lengths[a];
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            float dry = buf[i];
            float out = dry;
        
            float delayed = storage[pos & (ALLPASS_BUFFER - 1)];
            
            float feedback = sanef(out - w * delayed);
            
            buf[i] = sanef(feedback * w + delayed);
            
            storage[(pos + dv) & (ALLPASS_BUFFER - 1)] = feedback;
            pos++;
        }
    }
    pos = m->pos;
    storage = b->delay_storage;
    dv = b->params->delay_length;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        storage[(pos + dv) & (DELAY_BUFFER - 1)] = buf[i];
        pos++;
    }
}

void reverb_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct reverb_module *m = (struct reverb_module *)module;
    struct reverb_params *p = m->params;
    
    float dryamt = p->dryamt;
    float wetamt = p->wetamt;
    struct reverb_state *s = &m->state;

    if (p != m->old_params)
    {
        float tpdsr = 2 * M_PI / m->module.srate;
        cbox_onepolef_set_lowpass(&m->filter_coeffs[0], p->lowpass * tpdsr);
        cbox_onepolef_set_highpass(&m->filter_coeffs[1], p->highpass * tpdsr);
        float rv = p->decay_time * m->module.srate / 1000;
        m->gain = pow(0.001, 3488.0 / rv);
        m->old_params = p;
    }

    int mid = s->leg_count >> 1;
    memcpy(s->legs[0].buffer, inputs[0], CBOX_BLOCK_SIZE * sizeof(float));
    memcpy(s->legs[mid].buffer, inputs[1], CBOX_BLOCK_SIZE * sizeof(float));
    for (int u = 1; u < mid; u++)
    {
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
            s->legs[u].buffer[i] = 0.f;
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
            s->legs[u + mid].buffer[i] = 0.f;
    }
        
    for (int u = 0; u < s->leg_count; u++)
        cbox_reverb_process_leg(m, u);

    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        outputs[0][i] = inputs[0][i] * dryamt + s->legs[mid - 1].buffer[i] * wetamt;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        outputs[1][i] = inputs[1][i] * dryamt + s->legs[s->leg_count - 1].buffer[i] * wetamt;
    m->pos += CBOX_BLOCK_SIZE;
}

static void reverb_destroyfunc(struct cbox_module *module_)
{
    struct reverb_module *m = (struct reverb_module *)module_;
    free(m->params);
    for (int i = 0; i < m->state.leg_count; i++)
        cbox_reverb_leg_cleanup(&m->state.legs[i]);
    free(m->state.legs);
}

static void init_reverb_state(struct reverb_state *state, int leg_count, ...)
{
    state->leg_count = 4;
    state->legs = malloc(state->leg_count * sizeof(struct cbox_reverb_leg));
    va_list va;
    va_start(va, leg_count);
    for (int u = 0; u < state->leg_count; u++)
    {
        int delay_length = va_arg(va, int);
        int allpasses = va_arg(va, int);
        state->legs[u].params = leg_params_new(delay_length, allpasses, NULL);
        for (int i = 0; i < allpasses; i++)
            state->legs[u].params->allpass_lengths[i] = va_arg(va, int);
    }
    va_end(va);
    for (int u = 0; u < state->leg_count; u++)
        cbox_reverb_leg_init(&state->legs[u]);
}

MODULE_CREATE_FUNCTION(reverb)
{
    static int inited = 0;
    int i;
    if (!inited)
    {
        inited = 1;
    }
    
    struct reverb_module *m = malloc(sizeof(struct reverb_module));
    CALL_MODULE_INIT(m, 2, 2, reverb);
    m->module.process_event = reverb_process_event;
    m->module.process_block = reverb_process_block;
    m->pos = 0;
    m->old_params = NULL;
    m->params = malloc(sizeof(struct reverb_params));
    m->params->decay_time = cbox_config_get_float(cfg_section, "reverb_time", 1000);
    m->params->dryamt = cbox_config_get_gain_db(cfg_section, "dry_gain", 0.f);
    m->params->wetamt = cbox_config_get_gain_db(cfg_section, "wet_gain", -6.f);
    
    init_reverb_state(&m->state, 4, 
        133, 3, 
            731, 873, 1215,
        461, 3, 
            1054, 1519, 973,
        251, 3, 
            617, 941, 1277, 
        379, 3,
            1119, 1477, 933);
    
    float tpdsr = 2 * M_PI / m->module.srate;
    
    m->params->diffusion = cbox_config_get_float(cfg_section, "diffusion", 0.45);
    m->params->lowpass = cbox_config_get_float(cfg_section, "lowpass", 8000.f);
    m->params->highpass = cbox_config_get_float(cfg_section, "highpass", 35.f);
    
    return &m->module;
}

struct cbox_module_keyrange_metadata reverb_keyranges[] = {
};

struct cbox_module_livecontroller_metadata reverb_controllers[] = {
};

DEFINE_MODULE(reverb, 2, 2)

