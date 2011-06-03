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
#include "onepole-float.h"
#include "procmain.h"
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

#define NO_STAGES 12

struct phaser_params
{
    float center;
    float mdepth;
    float fb_amt;
    float lfo_freq;
    float sphase;
    int stages;
};

struct phaser_module
{
    struct cbox_module module;

    struct cbox_onepolef_state state[NO_STAGES][2];
    struct cbox_onepolef_coeffs coeffs[2];
    float fb[2];
    float tpdsr;
    struct phaser_params *params;
    
    float phase;
};

static struct phaser_params *copy_params(struct phaser_module *m)
{
    struct phaser_params *res = malloc(sizeof(struct phaser_params));
    memcpy(res, m->params, sizeof(struct phaser_params));
    return res;
}

#define EFFECT_PARAM(path, type, field, ctype, expr) \
    if (!strcmp(cmd->command, path) && !strcmp(cmd->arg_types, type)) \
    { \
        struct phaser_params *pp = copy_params(m); \
        pp->field = expr(*(ctype *)cmd->arg_values[0]); \
        free(cbox_rt_swap_pointers(app.rt, (void **)&m->params, pp)); \
    } \

static inline float deg2rad(float deg)
{
    return deg * M_PI / 180;
}

gboolean phaser_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct phaser_module *m = (struct phaser_module *)ct->user_data;
    
    EFFECT_PARAM("/center_freq", "f", center, double, ) else
    EFFECT_PARAM("/mod_depth", "f", mdepth, double, ) else
    EFFECT_PARAM("/fb_amt", "f", fb_amt, double, ) else
    EFFECT_PARAM("/lfo_freq", "f", lfo_freq, double, ) else
    EFFECT_PARAM("/stereo_phase", "f", sphase, double, deg2rad) else
    EFFECT_PARAM("/stages", "i", stages, int, ) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/center_freq", "f", error, m->params->center) &&
            cbox_execute_on(fb, NULL, "/mod_depth", "f", error, m->params->mdepth) &&
            cbox_execute_on(fb, NULL, "/fb_amt", "f", error, m->params->fb_amt) &&
            cbox_execute_on(fb, NULL, "/lfo_freq", "f", error, m->params->lfo_freq) &&
            cbox_execute_on(fb, NULL, "/stereo_phase", "f", error, m->params->sphase * 180 / M_PI) &&
            cbox_execute_on(fb, NULL, "/stages", "i", error, m->params->stages);
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown command '%s'", cmd->command);
        return FALSE;
    }
    return TRUE;
}

void phaser_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct phaser_module *m = (struct phaser_module *)module;
}

void phaser_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct phaser_module *m = (struct phaser_module *)module;
    struct phaser_params *p = m->params;
    int s, c, i;
    int stages = p->stages;
    float fb_amt = p->fb_amt;
    float phase = 0;
    if (stages < 0 || stages > NO_STAGES)
        stages = 0;

    cbox_onepolef_set_allpass(&m->coeffs[0], m->tpdsr * (p->center + p->mdepth * sin(m->phase)));
    cbox_onepolef_set_allpass(&m->coeffs[1], m->tpdsr * (p->center + p->mdepth * sin(m->phase + p->sphase)));
    m->phase += p->lfo_freq * CBOX_BLOCK_SIZE * m->tpdsr;
    
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

struct cbox_module *phaser_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    int b, c;
    
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct phaser_module *m = malloc(sizeof(struct phaser_module));
    cbox_module_init(&m->module, m, 2, 2);
    m->module.process_event = phaser_process_event;
    m->module.process_block = phaser_process_block;
    m->module.cmd_target.process_cmd = phaser_process_cmd;
    m->tpdsr = 2.0 * M_PI / srate;
    m->phase = 0;
    struct phaser_params *p = malloc(sizeof(struct phaser_params));
    m->params = p;
    p->sphase = cbox_config_get_float(cfg_section, "stereo_phase", 90.f) * 2 * M_PI / 360;
    p->lfo_freq = cbox_config_get_float(cfg_section, "lfo_freq", 1.f);
    p->center = cbox_config_get_float(cfg_section, "center_freq", 1500.f);
    p->mdepth = cbox_config_get_float(cfg_section, "mod_depth", 500.f);
    p->fb_amt = cbox_config_get_float(cfg_section, "feedback", 0.f);
    p->stages = cbox_config_get_int(cfg_section, "stages", NO_STAGES);
    
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

