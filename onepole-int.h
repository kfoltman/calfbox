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

#ifndef CBOX_ONEPOLE_INT_H
#define CBOX_ONEPOLE_INT_H

#include "dspmath.h"

struct cbox_onepole_state
{
    int32_t x1;
    int32_t y1;
};

struct cbox_onepole_coeffs
{
    int32_t a0;
    int32_t a1;
    int32_t b1;
    int shift;
};

static inline void cbox_onepole_reset(struct cbox_onepole_state *state)
{
    state->x1 = state->y1 = 0;
}

static inline void cbox_onepole_set_lowpass(struct cbox_onepole_coeffs *coeffs, float w)
{
    float x = tan (w);
    float q = 1 / (1 + x);
    float a01 = x*q;
    float b1 = a01 - q;
    int shift = 28;
    float scaler = (1 << shift);
    
    coeffs->a1 = coeffs->a0 = (int32_t)(a01 * scaler);
    coeffs->b1 = (int32_t)(b1  * scaler);
    coeffs->shift = shift;
}

static inline void cbox_onepole_set_highpass(struct cbox_onepole_coeffs *coeffs, float w)
{
    float x = tan (w);
    float q = 1 / (1 + x);
    float a01 = x*q;
    float b1 = a01 - q;
    int shift = 28;
    float scaler = (1 << shift)-1;
    
    coeffs->a0 = (int32_t)(a01 * scaler);
    coeffs->a1 = -coeffs->a0;
    coeffs->b1 = (int32_t)(b1  * scaler);
    coeffs->shift = shift;
}

static inline void cbox_onepole_process(struct cbox_onepole_state *state, struct cbox_onepole_coeffs *coeffs, int32_t *buffer)
{
    int i;
    int64_t a0 = coeffs->a0;
    int64_t a1 = coeffs->a1;
    int64_t b1 = coeffs->b1;
    int shift = coeffs->shift;
    int64_t maxint = ((int64_t)0x7FFFFFF) << shift;
    int32_t round = 1 << (shift - 1);
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        int32_t in = buffer[i];
        int64_t v = a0 * in + a1 * state->x1 - b1 * state->y1 + round;
        int32_t out = (llabs(v) >= maxint) ? (v > 0 ? 0x7FFFFFFF : -0x7FFFFFFF) : (v >> shift);
        
        buffer[i] = out;
        state->x1 = in;
        state->y1 = out;
    }
    if (state->y1 > 0 && state->y1 < round)
        state->y1--;
    if (state->y1 < 0 && state->y1 > -round)
        state->y1++;
}

static inline void cbox_onepole_process_to(struct cbox_onepole_state *state, struct cbox_onepole_coeffs *coeffs, int32_t *buffer_in, int32_t *buffer_out)
{
    int i;
    int64_t a0 = coeffs->a0;
    int64_t a1 = coeffs->a1;
    int64_t b1 = coeffs->b1;
    int shift = coeffs->shift;
    int64_t maxint = ((int64_t)0x7FFFFFF) << shift;
    int64_t round = 1 << (shift - 1);
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        int32_t in = buffer_in[i];
        int64_t v = a0 * in + a1 * state->x1 - b1 * state->y1 + round;
        int32_t out = (llabs(v) >= maxint) ? (v > 0 ? 0x7FFFFFFF : -0x7FFFFFFF) : (v >> shift);
        
        buffer_out[i] = out;
        state->x1 = in;
        state->y1 = out;
    }
}

#endif
