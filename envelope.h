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
    int next_if_pressed, next_if_released, keep_last_value, break_on_release, is_exp;
};

#define MAX_ENV_STAGES 16
#define EXP_NOISE_FLOOR (100.0 / 16384.0)

struct cbox_envelope_shape
{
    double start_value;
    struct cbox_envstage stages[MAX_ENV_STAGES];
};

struct cbox_envelope
{
    struct cbox_envelope_shape *shape;
    double stage_start_value, cur_value, exp_factor, inv_time, cur_time, orig_time, orig_target;
    int cur_stage;
};

static inline void cbox_envelope_init_stage(struct cbox_envelope *env)
{
    struct cbox_envstage *es = &env->shape->stages[env->cur_stage];
    env->orig_time = es->time;
    env->orig_target = es->end_value;
    env->inv_time = es->time > 0 ? 1.0 / es->time : 1e6;
    if (es->is_exp)
    {
        if (env->stage_start_value < EXP_NOISE_FLOOR)
            env->stage_start_value = EXP_NOISE_FLOOR;
        double ev = es->end_value;
        if (ev < EXP_NOISE_FLOOR)
            ev = EXP_NOISE_FLOOR;
        env->exp_factor = log(ev / env->stage_start_value);
    }
}

static inline void cbox_envelope_go_to(struct cbox_envelope *env, int stage)
{
    env->stage_start_value = env->cur_value;
    env->cur_stage = stage;
    env->cur_time = 0;
    cbox_envelope_init_stage(env);
}

static inline void cbox_envelope_reset(struct cbox_envelope *env)
{
    env->cur_value = 0;
    env->cur_stage = 0;
    env->cur_time = 0;
    cbox_envelope_init_stage(env);
}

static inline void cbox_envelope_update_shape(struct cbox_envelope *env, struct cbox_envelope_shape *shape)
{
    struct cbox_envelope_shape *old_shape = env->shape;
    env->shape = shape;
    if (env->cur_stage < 0)
        return;
    struct cbox_envstage *ns = &env->shape->stages[env->cur_stage];
    struct cbox_envstage *os = &old_shape->stages[env->cur_stage];
    if (os->time > 0)
        env->cur_time = env->cur_time * ns->time / os->time;
    if (env->cur_time > ns->time)
        env->cur_time = ns->time;
}

static inline float cbox_envelope_get_value(struct cbox_envelope *env, const struct cbox_envelope_shape *shape)
{
    if (env->cur_stage < 0)
        return env->cur_value;
    const struct cbox_envstage *es = &shape->stages[env->cur_stage];
    double pos = es->time > 0 ? env->cur_time * env->inv_time : 0;
    if (pos > 1)
        pos = 1;
    if (es->is_exp)
    {
        // instead of exp, may use 2**x which can be factored
        // into a shift and a table lookup
        env->cur_value = env->stage_start_value * expf(pos * env->exp_factor);
        if (env->cur_value <= EXP_NOISE_FLOOR)
            env->cur_value = 0;
    }
    else
        env->cur_value = env->stage_start_value + (es->end_value - env->stage_start_value) * pos;
    return env->cur_value;
}

#define DEBUG_UPDATE_SHAPE(...)

static inline void cbox_envelope_update_shape_after_modify(struct cbox_envelope *env, struct cbox_envelope_shape *shape, double sr)
{
    if (env->cur_stage < 0)
        return;
    struct cbox_envstage *es = &shape->stages[env->cur_stage];
    if (es->time != env->orig_time)
    {
        // Scale cur_time to reflect the same relative position within the stage
        env->cur_time = env->cur_time * es->time / (env->orig_time > 0 ? env->orig_time : 1);
        env->orig_time = es->time;
        env->inv_time = es->time > 0 ? 1.0 / es->time : 1e6;
    }
    if (es->end_value != env->orig_target)
    {
        // Adjust the start value to keep the current value intact given the change in the slope
        double pos = es->time > 0 ? env->cur_time * env->inv_time : 1;
        if (pos < 1)
        {
            if (es->is_exp)
                env->stage_start_value /= pow(es->end_value / (env->orig_target >= EXP_NOISE_FLOOR ? env->orig_target : EXP_NOISE_FLOOR), pos / (1 - pos)); // untested, likely never used
            else
                env->stage_start_value -= (es->end_value - env->orig_target) * pos / (1 - pos);
        }
        env->orig_target = es->end_value;
    }
}

static inline void cbox_envelope_advance(struct cbox_envelope *env, int released, const struct cbox_envelope_shape *shape)
{
    if (env->cur_stage < 0)
        return;
    const struct cbox_envstage *es = &shape->stages[env->cur_stage];
    double pos = es->time > 0 ? env->cur_time * env->inv_time : 1;
    env->cur_time++;
    if (pos >= 1 || (es->break_on_release && released))
    {
        int next_stage = released ? es->next_if_released : es->next_if_pressed;
        if (!es->keep_last_value || pos >= 1 || (es->keep_last_value == 2 && !released) || next_stage == env->cur_stage)
            env->stage_start_value = es->end_value;
        else
            env->stage_start_value = env->cur_value;
        env->cur_stage = next_stage;
        env->cur_time = 0;
        cbox_envelope_init_stage(env);
    }
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
    env->stages[0].next_if_released = 3;
    env->stages[0].keep_last_value = 1;
    env->stages[0].break_on_release = 0;
    env->stages[0].is_exp = 0;

    env->stages[1].end_value = adsr->sustain;
    env->stages[1].time = adsr->decay * sr;
    env->stages[1].next_if_pressed = 2;
    env->stages[1].next_if_released = 3;
    env->stages[1].keep_last_value = 1;
    env->stages[1].break_on_release = 0;
    env->stages[1].is_exp = 0;

    env->stages[2].end_value = adsr->sustain;
    env->stages[2].time = 1 * sr;
    env->stages[2].next_if_pressed = 2;
    env->stages[2].next_if_released = 3;
    env->stages[2].keep_last_value = 0;
    env->stages[2].break_on_release = 1;
    env->stages[2].is_exp = 0;

    env->stages[3].end_value = 0;
    env->stages[3].time = adsr->release * sr;
    env->stages[3].next_if_pressed = -1;
    env->stages[3].next_if_released = -1;
    env->stages[3].keep_last_value = 0;
    env->stages[3].break_on_release = 0;
    env->stages[3].is_exp = 1;

    env->stages[15].end_value = 0;
    env->stages[15].time = 0.01 * sr;
    env->stages[15].next_if_pressed = -1;
    env->stages[15].next_if_released = -1;
    env->stages[15].keep_last_value = 0;
    env->stages[15].break_on_release = 0;
    env->stages[15].is_exp = 0;
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

static inline void cbox_dahdsr_init(struct cbox_dahdsr *dahdsr, float top_value)
{
    dahdsr->start = 0.f;
    dahdsr->delay = 0.f;
    dahdsr->attack = 0.f;
    dahdsr->hold = 0.f;
    dahdsr->decay = 0.f;
    dahdsr->sustain = top_value;
    dahdsr->release = 0.05f;
}

static inline void cbox_envelope_init_dahdsr(struct cbox_envelope_shape *env, const struct cbox_dahdsr *dahdsr, int sr, float top_value, gboolean is_release_exp)
{
    env->start_value = dahdsr->start;
    env->stages[0].end_value = dahdsr->start;
    env->stages[0].time = dahdsr->delay * sr;
    env->stages[0].next_if_pressed = 1;
    env->stages[0].next_if_released = 5;
    env->stages[0].keep_last_value = 1;
    env->stages[0].break_on_release = 0;
    env->stages[0].is_exp = 0;

    env->stages[1].end_value = top_value;
    env->stages[1].time = dahdsr->attack * sr;
    env->stages[1].next_if_pressed = 2;
    env->stages[1].next_if_released = 5;
    env->stages[1].keep_last_value = 2;
    env->stages[1].break_on_release = 1;
    env->stages[1].is_exp = 0;

    env->stages[2].end_value = top_value;
    env->stages[2].time = dahdsr->hold * sr;
    env->stages[2].next_if_pressed = 3;
    env->stages[2].next_if_released = 5;
    env->stages[2].keep_last_value = 2;
    env->stages[2].break_on_release = 1;
    env->stages[2].is_exp = 0;

    env->stages[3].end_value = dahdsr->sustain;
    env->stages[3].time = dahdsr->decay * sr;
    env->stages[3].next_if_pressed = 4;
    env->stages[3].next_if_released = 5;
    env->stages[3].keep_last_value = 1;
    env->stages[3].break_on_release = 1;
    env->stages[3].is_exp = 0;

    env->stages[4].end_value = dahdsr->sustain;
    env->stages[4].time = 1 * sr;
    env->stages[4].next_if_pressed = 4;
    env->stages[4].next_if_released = 5;
    env->stages[4].keep_last_value = 1;
    env->stages[4].break_on_release = 1;
    env->stages[4].is_exp = 0;

    env->stages[5].end_value = 0;
    env->stages[5].time = dahdsr->release * sr;
    env->stages[5].next_if_pressed = -1;
    env->stages[5].next_if_released = -1;
    env->stages[5].keep_last_value = 0;
    env->stages[5].break_on_release = 0;
    env->stages[5].is_exp = is_release_exp;

    env->stages[15].end_value = 0;
    env->stages[15].time = 0.01 * sr;
    env->stages[15].next_if_pressed = -1;
    env->stages[15].next_if_released = -1;
    env->stages[15].keep_last_value = 0;
    env->stages[15].break_on_release = 0;
    env->stages[15].is_exp = 0;
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
            // Allow negative times (deal with them in get_next) to make multiple signed modulations work correctly
            break;
        case 4: // sustain
            env->stages[3].end_value += value;
            env->stages[4].end_value += value;
            env->stages[4].time = 0.02 * sr; // more rapid transition
            break;
        case 6: // start
            env->stages[0].end_value += value;
            env->start_value += value;
            break;
    }
}


#endif
