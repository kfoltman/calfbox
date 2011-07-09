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

#ifndef CBOX_BIQUAD_FLOAT_H
#define CBOX_BIQUAD_FLOAT_H

#include "dspmath.h"

struct cbox_biquadf_state
{
    float x1;
    float y1;
    float x2;
    float y2;
};

struct cbox_biquadf_coeffs
{
    float a0;
    float a1;
    float a2;
    float b1;
    float b2;
};

static inline void cbox_biquadf_reset(struct cbox_biquadf_state *state)
{
    state->x1 = state->y1 = state->x2 = state->y2 = 0.f;
}

// Based on filter coefficient equations by Robert Bristow-Johnson
static inline void cbox_biquadf_set_lp_rbj(struct cbox_biquadf_coeffs *coeffs, float fc, float q, float sr)
{
    float omega=(float)(2*M_PI*fc/sr);
    float sn=sin(omega);
    float cs=cos(omega);
    float alpha=(float)(sn/(2*q));
    float inv=(float)(1.0/(1.0+alpha));

    coeffs->a2 = coeffs->a0 = (float)(inv*(1 - cs)*0.5f);
    coeffs->a1 = coeffs->a0 + coeffs->a0;
    coeffs->b1 =  (float)(-2*cs*inv);
    coeffs->b2 =  (float)((1 - alpha)*inv);
}

// Based on filter coefficient equations by Robert Bristow-Johnson
static inline void cbox_biquadf_set_hp_rbj(struct cbox_biquadf_coeffs *coeffs, float fc, float q, float sr)
{
    float omega=(float)(2*M_PI*fc/sr);
    float sn=sin(omega);
    float cs=cos(omega);
    float alpha=(float)(sn/(2*q));
    float inv=(float)(1.0/(1.0+alpha));

    coeffs->a2 = coeffs->a0 = (float)(inv*(1 + cs)*0.5f);
    coeffs->a1 = -2 * coeffs->a0;
    coeffs->b1 =  (float)(-2*cs*inv);
    coeffs->b2 =  (float)((1 - alpha)*inv);
}

// Based on filter coefficient equations by Robert Bristow-Johnson
static inline void cbox_biquadf_set_bp_rbj(struct cbox_biquadf_coeffs *coeffs, float fc, float q, float sr)
{
    float omega=(float)(2*M_PI*fc/sr);
    float sn=sin(omega);
    float cs=cos(omega);
    float alpha=(float)(sn/(2*q));
    float inv=(float)(1.0/(1.0+alpha));

    coeffs->a0 = (float)(inv*alpha);
    coeffs->a1 = 0.f;
    coeffs->a2 = -coeffs->a0;
    coeffs->b1 =  (float)(-2*cs*inv);
    coeffs->b2 =  (float)((1 - alpha)*inv);
}


// Based on filter coefficient equations by Robert Bristow-Johnson
static inline void cbox_biquadf_set_peakeq_rbj(struct cbox_biquadf_coeffs *coeffs, float freq, float q, float peak, float sr)
{
    float A = sqrt(peak);
    float w0 = freq * 2 * M_PI * (1.0 / sr);
    float alpha = sin(w0) / (2 * q);
    float ib0 = 1.0 / (1 + alpha/A);
    coeffs->a1 = coeffs->b1 = -2*cos(w0) * ib0;
    coeffs->a0 = ib0 * (1 + alpha*A);
    coeffs->a2 = ib0 * (1 - alpha*A);
    coeffs->b2 = ib0 * (1 - alpha/A);
}

static inline void cbox_biquadf_process(struct cbox_biquadf_state *state, struct cbox_biquadf_coeffs *coeffs, float *buffer)
{
    int i;
    float a0 = coeffs->a0;
    float a1 = coeffs->a1;
    float a2 = coeffs->a2;
    float b1 = coeffs->b1;
    float b2 = coeffs->b2;
    double y1 = state->y1;
    double y2 = state->y2;
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float in = buffer[i];
        double out = a0 * in + a1 * state->x1 + a2 * state->x2 - b1 * y1 - b2 * y2;
        
        buffer[i] = out;
        state->x2 = state->x1;
        state->x1 = in;
        y2 = y1;
        y1 = out;
    }
    state->y2 = sanef(y2);
    state->y1 = sanef(y1);
}

static inline void cbox_biquadf_process_to(struct cbox_biquadf_state *state, struct cbox_biquadf_coeffs *coeffs, float *buffer_in, float *buffer_out)
{
    int i;
    float a0 = coeffs->a0;
    float a1 = coeffs->a1;
    float a2 = coeffs->a2;
    float b1 = coeffs->b1;
    float b2 = coeffs->b2;
    double y1 = state->y1;
    double y2 = state->y2;
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float in = buffer_in[i];
        double out = a0 * in + a1 * state->x1 + a2 * state->x2 - b1 * y1 - b2 * y2;
        
        buffer_out[i] = out;
        state->x2 = state->x1;
        state->x1 = in;
        y2 = y1;
        y1 = out;
    }
    state->y2 = sanef(y2);
    state->y1 = sanef(y1);
}

static inline void cbox_biquadf_process_adding(struct cbox_biquadf_state *state, struct cbox_biquadf_coeffs *coeffs, float *buffer_in, float *buffer_out)
{
    int i;
    float a0 = coeffs->a0;
    float a1 = coeffs->a1;
    float a2 = coeffs->a2;
    float b1 = coeffs->b1;
    float b2 = coeffs->b2;
    double y1 = state->y1;
    double y2 = state->y2;
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float in = buffer_in[i];
        double out = a0 * in + a1 * state->x1 + a2 * state->x2 - b1 * y1 - b2 * y2;
        
        buffer_out[i] += out;
        state->x2 = state->x1;
        state->x1 = in;
        y2 = y1;
        y1 = out;
    }
    state->y2 = sanef(y2);
    state->y1 = sanef(y1);
}

#endif
