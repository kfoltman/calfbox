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
#ifndef CBOX_DSPMATH_H
#define CBOX_DSPMATH_H

#define CBOX_BLOCK_SIZE 16

#include <complex.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <memory.h>

#ifndef M_PI
#include <glib.h>
#define M_PI G_PI
#endif

typedef float cbox_sample_t;

struct cbox_sincos
{
    float sine;
    float cosine;
    float prewarp;
};

static inline float hz2w(float hz, float sr)
{
    return M_PI * hz / (2 * sr);
}

static inline float cerp_naive(float v0, float v1, float v2, float v3, float f)
{
    float x0 = -1;
    float x1 = 0;
    float x2 = 1;
    float x3 = 2;
    
    float l0 = ((f - x1) * (f - x2) * (f - x3)) / (            (x0 - x1) * (x0 - x2) * (x0 - x3));
    float l1 = ((f - x0) * (f - x2) * (f - x3)) / ((x1 - x0)             * (x1 - x2) * (x1 - x3));
    float l2 = ((f - x0) * (f - x1) * (f - x3)) / ((x2 - x0) * (x2 - x1)             * (x2 - x3));
    float l3 = ((f - x0) * (f - x1) * (f - x2)) / ((x3 - x0) * (x3 - x1) * (x3 - x2)            );
    
    return v0 * l0 + v1 * l1 + v2 * l2 + v3 * l3;
}

static inline float cerp(float v0, float v1, float v2, float v3, float f)
{
    f += 1;
    
    float d0 = (f - 0);
    float d1 = (f - 1);
    float d2 = (f - 2);
    float d3 = (f - 3);
    
    float d03 = (d0 * d3) * (1.0 / 2.0);
    float d12 = (d03 + 1) * (1.0 / 3.0);

    float l0 = -d12 * d3;
    float l1 = d03 * d2;
    float l2 = -d03 * d1;
    float l3 = d12 * d0;
    
    float y = v0 * l0 + v1 * l1 + v2 * l2 + v3 * l3;
    // printf("%f\n", y - cerp_naive(v0, v1, v2, v3, f - 1));
    return y;
}

static inline float sanef(float v)
{
    if (fabs(v) < (1.0 / (65536.0 * 65536.0)))
        return 0;
    return v;
}

static inline void sanebf(float *buf)
{
    int i;
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
        buf[i] = sanef(buf[i]);
}

static inline void copybf(float *to, float *from)
{
    memcpy(to, from, sizeof(float) * CBOX_BLOCK_SIZE);
}

static inline float cent2factor(float cent)
{
    return powf(2.0, cent * (1.f / 1200.f)); // I think this may be optimised using exp()
}

static inline float dB2gain(float dB)
{
    return powf(2.f, dB * (1.f / 6.f));
}

static inline float dB2gain_simple(float dB)
{
    if (dB <= -96)
        return 0;
    return powf(2.f, dB * (1.f / 6.f));
}

static inline float gain2dB_simple(float gain)
{
    static const float sixoverlog2 = 8.656170245333781; // 6.0 / logf(2.f);
    if (gain < (1.f / 65536.f))
        return -96.f;
    return sixoverlog2 * logf(gain);
}

static inline float deg2rad(float deg)
{
    return deg * (float)(M_PI / 180.f);
}

static inline float rad2deg(float rad)
{
    return rad * (float)(180.f / M_PI);
}

// Do a butterfly operation:
// dst1 = src1 + e^iw_1*src2
// dst2 = src1 + e^iw_2*src2 (w = phase * 2pi / ANALYSIS_BUFFER_SIZE)
static inline void butterfly(complex float *dst1, complex float *dst2, complex float src1, complex float src2, complex float eiw1, complex float eiw2)
{
    *dst1 = src1 + eiw1 * src2;
    *dst2 = src1 + eiw2 * src2;
}

struct cbox_gain
{
    float db_gain;
    float lin_gain;
    float old_lin_gain;
    float pos;
    float delta;
};

static inline void cbox_gain_init(struct cbox_gain *gain)
{
    gain->db_gain = 0;
    gain->lin_gain = 1;
    gain->old_lin_gain = 1;
    gain->pos = 1;
    gain->delta = 1 / (44100 * 0.1); // XXXKF ballpark
}

static inline void cbox_gain_set_db(struct cbox_gain *gain, float db)
{
    if (gain->db_gain == db)
        return;
    gain->db_gain = db;
    gain->old_lin_gain = gain->old_lin_gain + (gain->lin_gain - gain->old_lin_gain) * gain->pos;
    gain->lin_gain = dB2gain(db);
    gain->pos = 0;
}

#define CBOX_GAIN_APPLY_LOOP(gain, nsamples, code) \
{ \
    double pos = (gain)->pos; \
    double span = (gain)->lin_gain - (gain)->old_lin_gain; \
    double start = (gain)->old_lin_gain; \
    double step = (gain)->delta; \
    if (pos >= 1) { \
        double tgain = gain->lin_gain; \
        for (uint32_t i = 0; i < (nsamples); ++i) { \
            code(i, tgain) \
        } \
    } else { \
        if (pos + (nsamples) * step < 1.0) { \
            for (uint32_t i = 0; i < (nsamples); ++i) { \
                double tgain = start + (pos + i * step) * span; \
                code(i, tgain) \
            } \
            gain->pos += (nsamples) * step; \
        } \
        else { \
            for (uint32_t i = 0; i < (nsamples); ++i) { \
                code(i, (start + pos * span)) \
                pos = (pos + step < 1.0 ? pos + step : 1.0); \
            } \
            gain->pos = 1.0; \
        } \
    } \
}

#define CBOX_GAIN_ADD_MONO(i, gain) \
    dest1[i] += src1[i] * gain;

static inline void cbox_gain_add_mono(struct cbox_gain *gain, float *dest1, const float *src1, uint32_t nsamples)
{
    CBOX_GAIN_APPLY_LOOP(gain, nsamples, CBOX_GAIN_ADD_MONO);
}

#define CBOX_GAIN_ADD_STEREO(i, gain) \
    dest1[i] += src1[i] * gain, dest2[i] += src2[i] * gain;

static inline void cbox_gain_add_stereo(struct cbox_gain *gain, float *dest1, const float *src1, float *dest2, const float *src2, uint32_t nsamples)
{
    CBOX_GAIN_APPLY_LOOP(gain, nsamples, CBOX_GAIN_ADD_STEREO);
}

#define CBOX_GAIN_COPY_MONO(i, gain) \
    dest1[i] = src1[i] * gain;

static inline void cbox_gain_copy_mono(struct cbox_gain *gain, float *dest1, const float *src1, uint32_t nsamples)
{
    CBOX_GAIN_APPLY_LOOP(gain, nsamples, CBOX_GAIN_COPY_MONO);
}

#define CBOX_GAIN_COPY_STEREO(i, gain) \
    dest1[i] = src1[i] * gain, dest2[i] = src2[i] * gain;

static inline void cbox_gain_copy_stereo(struct cbox_gain *gain, float *dest1, const float *src1, float *dest2, const float *src2, uint32_t nsamples)
{
    CBOX_GAIN_APPLY_LOOP(gain, nsamples, CBOX_GAIN_COPY_STEREO);
}

#endif