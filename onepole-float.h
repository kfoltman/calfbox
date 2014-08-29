/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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

#ifndef CBOX_ONEPOLE_FLOAT_H
#define CBOX_ONEPOLE_FLOAT_H

#include "dspmath.h"

struct cbox_onepolef_state
{
    float x1;
    float y1;
};

struct cbox_onepolef_coeffs
{
    float a0;
    float a1;
    float b1;
};

static inline void cbox_onepolef_reset(struct cbox_onepolef_state *state)
{
    state->x1 = state->y1 = 0.f;
}

static inline void cbox_onepolef_set_lowpass(struct cbox_onepolef_coeffs *coeffs, float w)
{
    float x = tan (w * 0.5f);
    float q = 1 / (1 + x);
    float a01 = x*q;
    float b1 = a01 - q;
    
    coeffs->a0 = a01;
    coeffs->a1 = a01;
    coeffs->b1 = b1;
}

static inline void cbox_onepolef_set_highpass(struct cbox_onepolef_coeffs *coeffs, float w)
{
    float x = tan (w * 0.5f);
    float q = 1 / (1 + x);
    float a01 = x*q;
    float b1 = a01 - q;
    
    coeffs->a0 = q;
    coeffs->a1 = -q;
    coeffs->b1 = b1;
}

static inline void cbox_onepolef_set_highshelf_tonectl(struct cbox_onepolef_coeffs *coeffs, float w, float g0)
{
    float x = tan (w * 0.5f);
    float q = 1 / (1 + x);
    float b1 = x * q - q;
    
    coeffs->a0 = 0.5 * (1 + b1 + g0 - b1 * g0);
    coeffs->a1 = 0.5 * (1 + b1 - g0 + b1 * g0);
    coeffs->b1 = b1;
}

static inline void cbox_onepolef_set_highshelf_setgain(struct cbox_onepolef_coeffs *coeffs, float g0)
{
    coeffs->a0 = 0.5 * (1 + coeffs->b1 + g0 - coeffs->b1 * g0);
    coeffs->a1 = 0.5 * (1 + coeffs->b1 - g0 + coeffs->b1 * g0);
}

static inline void cbox_onepolef_set_allpass(struct cbox_onepolef_coeffs *coeffs, float w)
{
    float x = tan (w * 0.5f);
    float q = 1 / (1 + x);
    float a01 = x*q;
    float b1 = a01 - q;
    
    coeffs->a0 = b1;
    coeffs->a1 = 1;
    coeffs->b1 = b1;
}

static inline float cbox_onepolef_process_sample(struct cbox_onepolef_state *state, struct cbox_onepolef_coeffs *coeffs, float in)
{
    float out = sanef(coeffs->a0 * in + coeffs->a1 * state->x1 - coeffs->b1 * state->y1);
    
    state->x1 = in;
    state->y1 = out;
    return out;
}

static inline void cbox_onepolef_process(struct cbox_onepolef_state *state, struct cbox_onepolef_coeffs *coeffs, float *buffer)
{
    int i;
    float a0 = coeffs->a0;
    float a1 = coeffs->a1;
    float b1 = coeffs->b1;
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float in = buffer[i];
        double out = a0 * in + a1 * state->x1 - b1 * state->y1;
        
        buffer[i] = out;
        state->x1 = in;
        state->y1 = out;
    }
    state->y1 = sanef(state->y1);
}

static inline void cbox_onepolef_process_to(struct cbox_onepolef_state *state, struct cbox_onepolef_coeffs *coeffs, float *buffer_in, float *buffer_out)
{
    int i;
    float a0 = coeffs->a0;
    float a1 = coeffs->a1;
    float b1 = coeffs->b1;
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float in = buffer_in[i];
        double out = a0 * in + a1 * state->x1 - b1 * state->y1;
        
        buffer_out[i] = out;
        state->x1 = in;
        state->y1 = out;
    }
    state->y1 = sanef(state->y1);
}

#endif
