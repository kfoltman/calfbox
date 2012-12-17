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

#ifndef CBOX_ENVELOPE_H
#define CBOX_ENVELOPE_H

#include <glib.h>
#include <config-api.h>

struct cbox_envstage
{
    double end_value;
    int time;
    int next_if_pressed, next_if_released, keep_last_value, break_on_release;
};

#define MAX_ENV_STAGES 16

struct cbox_envelope_shape
{
    double start_value;
    struct cbox_envstage stages[MAX_ENV_STAGES];
};

struct cbox_envelope
{
    struct cbox_envelope_shape *shape;
    double stage_start_value, cur_value;
    int cur_stage, cur_time;
};

static inline void cbox_envelope_reset(struct cbox_envelope *env)
{
    env->cur_value = env->stage_start_value = env->shape->start_value;
    env->cur_stage = 0;
    env->cur_time = 0;
}

static inline void cbox_envelope_go_to(struct cbox_envelope *env, int stage)
{
    env->stage_start_value = env->cur_value;
    env->cur_stage = stage;
    env->cur_time = 0;
}

static inline float cbox_envelope_get_next(struct cbox_envelope *env, int released)
{
    if (env->cur_stage < 0)
    {
        return env->cur_value;
    }
    struct cbox_envstage *es = &env->shape->stages[env->cur_stage];
    env->cur_time++;
    double pos = es->time > 0 ? env->cur_time * 1.0 / es->time : 1;
    env->cur_value = env->stage_start_value + (es->end_value - env->stage_start_value) * pos;
    if (pos >= 1 || (es->break_on_release && released))
    {
        env->cur_stage = released ? es->next_if_released : es->next_if_pressed;
        if (!es->keep_last_value)
            env->stage_start_value = es->end_value;
        else
            env->stage_start_value = env->cur_value;
        env->cur_time = 0;
    }
    // printf("%d %d %d\n", env->cur_stage, env->cur_time, es->time);
    return env->cur_value;
}

struct cbox_adsr
{
    float attack;
    float decay;
    float sustain;
    float release;
};

static inline void cbox_envelope_init_adsr(struct cbox_envelope_shape *env, const struct cbox_adsr *adsr, int sr)
{
    env->start_value = 0;
    env->stages[0].end_value = 1;
    env->stages[0].time = adsr->attack * sr;
    env->stages[0].next_if_pressed = 1;
    env->stages[0].next_if_released = 1;
    env->stages[0].keep_last_value = 1;
    env->stages[0].break_on_release = 0;

    env->stages[1].end_value = adsr->sustain;
    env->stages[1].time = adsr->decay * sr;
    env->stages[1].next_if_pressed = 2;
    env->stages[1].next_if_released = 2;
    env->stages[1].keep_last_value = 0;
    env->stages[1].break_on_release = 0;

    env->stages[2].end_value = adsr->sustain;
    env->stages[2].time = 1 * sr;
    env->stages[2].next_if_pressed = 2;
    env->stages[2].next_if_released = 3;
    env->stages[2].keep_last_value = 0;
    env->stages[2].break_on_release = 1;

    env->stages[3].end_value = 0;
    env->stages[3].time = adsr->release * sr;
    env->stages[3].next_if_pressed = -1;
    env->stages[3].next_if_released = -1;
    env->stages[3].keep_last_value = 0;
    env->stages[3].break_on_release = 0;

    env->stages[15].end_value = 0;
    env->stages[15].time = 0.01 * sr;
    env->stages[15].next_if_pressed = -1;
    env->stages[15].next_if_released = -1;
    env->stages[15].keep_last_value = 0;
    env->stages[15].break_on_release = 0;
}

struct cbox_dahdsr
{
    float start;
    float delay;
    float attack;
    float hold;
    float decay;
    float sustain;
    float release;
};

static inline void cbox_dahdsr_init(struct cbox_dahdsr *dahdsr)
{
    dahdsr->start = 0.f;
    dahdsr->delay = 0.f;
    dahdsr->attack = 0.f;
    dahdsr->hold = 0.f;
    dahdsr->decay = 0.f;
    dahdsr->sustain = 1.f;
    dahdsr->release = 0.05f;
}

static inline void cbox_envelope_init_dahdsr(struct cbox_envelope_shape *env, const struct cbox_dahdsr *dahdsr, int sr, float top_value)
{
    env->start_value = 0;
    env->stages[0].end_value = 0;
    env->stages[0].time = dahdsr->delay * sr;
    env->stages[0].next_if_pressed = 1;
    env->stages[0].next_if_released = 1;
    env->stages[0].keep_last_value = 1;
    env->stages[0].break_on_release = 0;

    env->stages[1].end_value = top_value;
    env->stages[1].time = dahdsr->attack * sr;
    env->stages[1].next_if_pressed = 2;
    env->stages[1].next_if_released = 2;
    env->stages[1].keep_last_value = 1;
    env->stages[1].break_on_release = 0;

    env->stages[2].end_value = top_value;
    env->stages[2].time = dahdsr->hold * sr;
    env->stages[2].next_if_pressed = 3;
    env->stages[2].next_if_released = 3;
    env->stages[2].keep_last_value = 1;
    env->stages[2].break_on_release = 0;

    env->stages[3].end_value = dahdsr->sustain;
    env->stages[3].time = dahdsr->decay * sr;
    env->stages[3].next_if_pressed = 4;
    env->stages[3].next_if_released = 4;
    env->stages[3].keep_last_value = 0;
    env->stages[3].break_on_release = 0;

    env->stages[4].end_value = dahdsr->sustain;
    env->stages[4].time = 1 * sr;
    env->stages[4].next_if_pressed = 4;
    env->stages[4].next_if_released = 5;
    env->stages[4].keep_last_value = 0;
    env->stages[4].break_on_release = 1;

    env->stages[5].end_value = 0;
    env->stages[5].time = dahdsr->release * sr;
    env->stages[5].next_if_pressed = -1;
    env->stages[5].next_if_released = -1;
    env->stages[5].keep_last_value = 0;
    env->stages[5].break_on_release = 0;

    env->stages[15].end_value = 0;
    env->stages[15].time = 0.01 * sr;
    env->stages[15].next_if_pressed = -1;
    env->stages[15].next_if_released = -1;
    env->stages[15].keep_last_value = 0;
    env->stages[15].break_on_release = 0;
}

static inline void cbox_envelope_modify_dahdsr(struct cbox_envelope_shape *env, int part, float value, int sr)
{
    switch(part)
    {
        case 0: // delay
        case 1: // attack
        case 2: // hold
        case 3: // decay
        case 5: // release
            env->stages[part].time += value * sr;
            if (env->stages[part].time < 0)
                env->stages[part].time = 0;
            break;
        case 4: // sustain
            env->stages[3].end_value = value;
            env->stages[4].end_value = value;
            break;
    }
}

static void cbox_config_get_dahdsr(const char *cfg_section, const char *prefix, struct cbox_dahdsr *env)
{
    gchar *v;
    
    v = g_strdup_printf("%s_start", prefix);
    env->start = cbox_config_get_float(cfg_section, v, env->start);
    g_free(v);
    
    v = g_strdup_printf("%s_delay", prefix);
    env->delay = cbox_config_get_float(cfg_section, v, env->delay);
    g_free(v);
    
    v = g_strdup_printf("%s_attack", prefix);
    env->attack = cbox_config_get_float(cfg_section, v, env->attack);
    g_free(v);
    
    v = g_strdup_printf("%s_attack", prefix);
    env->hold = cbox_config_get_float(cfg_section, v, env->hold);
    g_free(v);
    
    v = g_strdup_printf("%s_decay", prefix);
    env->decay = cbox_config_get_float(cfg_section, v, env->decay);
    g_free(v);
    
    v = g_strdup_printf("%s_sustain", prefix);
    env->sustain = cbox_config_get_float(cfg_section, v, env->sustain);
    g_free(v);
    
    v = g_strdup_printf("%s_release", prefix);
    env->release = cbox_config_get_float(cfg_section, v, env->release);
    g_free(v);
}

#endif
