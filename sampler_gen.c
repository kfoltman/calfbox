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

#include "config-api.h"
#include "dspmath.h"
#include "errors.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "sampler.h"
#include "sfzloader.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>

#define LOW_QUALITY_INTERPOLATION 0

#define PREPARE_LOOP \
    uint32_t __attribute__((unused)) loop_end = v->loop_end; \
    int16_t *sample_data = v->sample_data; \
    int16_t __attribute__((unused)) *sample_data_loop = v->sample_data_loop;

#define IS_LOOP_FINISHED \
    (v->loop_start == (uint32_t)-1)


#if LOW_QUALITY_INTERPOLATION

static uint32_t process_voice_mono_lerp(struct sampler_gen *v, float **output)
{
    float lgain = v->last_lgain;
    float rgain = v->last_rgain;
    float lgain_delta = (v->lgain - v->last_lgain) / CBOX_BLOCK_SIZE;
    float rgain_delta = (v->rgain - v->last_rgain) / CBOX_BLOCK_SIZE;
    PREPARE_LOOP
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        if (v->pos >= loop_end)
        {
            if (IS_LOOP_FINISHED)
            {
                v->mode = spt_inactive;
                return i;
            }
            v->pos = v->pos - loop_end + v->loop_start;
            sample_data = sample_data_loop;
        }
        
        float fr = (v->frac_pos >> 8) * (1.0 / (256.0 * 65536.0));
        uint32_t nextsample = v->pos + 1;
        if (nextsample >= loop_end && v->loop_start != (uint32_t)-1)
            nextsample -= v->loop_start;
        
        float sample = fr * sample_data[nextsample] + (1 - fr) * sample_data[v->pos];
        
        if (v->frac_pos > ~v->frac_delta)
            v->pos++;
        v->frac_pos += v->frac_delta;
        v->pos += v->delta;
        
        output[0][i] = sample * lgain;
        output[1][i] = sample * rgain;
        lgain += lgain_delta;
        rgain += rgain_delta;
    }
    return CBOX_BLOCK_SIZE;
}

#else

static uint32_t process_voice_mono(struct sampler_gen *v, float **output)
{
    float lgain = v->last_lgain;
    float rgain = v->last_rgain;
    float lgain_delta = (v->lgain - v->last_lgain) / CBOX_BLOCK_SIZE;
    float rgain_delta = (v->rgain - v->last_rgain) / CBOX_BLOCK_SIZE;
    PREPARE_LOOP
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        if (v->pos >= loop_end)
        {
            if (IS_LOOP_FINISHED)
            {
                v->mode = spt_inactive;
                return i;
            }
            v->pos = v->pos - loop_end + v->loop_start;
        }
        
        float t = (v->frac_pos >> 8) * (1.0 / (256.0 * 65536.0));
        
        float idata[4];
        if (v->pos + 4 < loop_end)
        {
            int16_t *p = &sample_data[v->pos];
            for (int s = 0; s < 4; s++)
                idata[s] = p[s];
        }
        else
        {
            uint32_t nextsample = v->pos;
            int s;
            for (s = 0; s < 4; s++)
            {
                if (nextsample >= loop_end)
                {
                    if (v->loop_start == (uint32_t)-1)
                        break;
                    nextsample -= loop_end - v->loop_start;
                    sample_data = sample_data_loop;
                }
                idata[s] = sample_data[nextsample];
                nextsample++;
            }
            while(s < 4)
                idata[s++] = 0.f;
        }
        
        if (v->loop_start != (uint32_t)-1 && v->pos >= loop_end - v->loop_overlap && v->loop_start > v->loop_overlap)
        {
            uint32_t nextsample = v->pos - (loop_end - v->loop_start);
            float xfade = (v->pos - (loop_end - v->loop_overlap)) * v->loop_overlap_step;
            for (int s = 0; s < 4 && xfade < 1; s++)
            {
                idata[s] += (sample_data_loop[nextsample] - idata[s]) * xfade;
                nextsample++;
                xfade += v->loop_overlap_step;
            }
        }
        float sample = (-t*(t-1)*(t-2) * idata[0] + 3*(t+1)*(t-1)*(t-2) * idata[1] - 3*(t+1)*t*(t-2) * idata[2] + (t+1)*t*(t-1) * idata[3]) * (1.0 / 6.0);
        
        if (v->frac_pos > ~v->frac_delta)
            v->pos++;
        v->frac_pos += v->frac_delta;
        v->pos += v->delta;
        
        output[0][i] = sample * lgain;
        output[1][i] = sample * rgain;
        lgain += lgain_delta;
        rgain += rgain_delta;
    }
    return CBOX_BLOCK_SIZE;
}

#endif

#if LOW_QUALITY_INTERPOLATION

static uint32_t process_voice_stereo_lerp(struct sampler_gen *v, float **output)
{
    float lgain = v->last_lgain;
    float rgain = v->last_rgain;
    float lgain_delta = (v->lgain - v->last_lgain) / CBOX_BLOCK_SIZE;
    float rgain_delta = (v->rgain - v->last_rgain) / CBOX_BLOCK_SIZE;
    PREPARE_LOOP

    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        if (v->pos >= loop_end)
        {
            if (IS_LOOP_FINISHED)
            {
                v->mode = spt_inactive;
                return i;
            }
            v->pos = v->pos - loop_end + v->loop_start;
            sample_data = sample_data_loop;
        }
        
        float fr = (v->frac_pos >> 8) * (1.0 / (256.0 * 65536.0));
        uint32_t nextsample = v->pos + 1;
        if (nextsample >= loop_end && v->loop_start != (uint32_t)-1)
            nextsample -= v->loop_start;
        
        float lsample = fr * sample_data[nextsample << 1] + (1 - fr) * sample_data[v->pos << 1];
        float rsample = fr * sample_data[1 + (nextsample << 1)] + (1 - fr) * sample_data[1 + (v->pos << 1)];
        
        if (v->frac_pos > ~v->frac_delta)
            v->pos++;
        v->frac_pos += v->frac_delta;
        v->pos += v->delta;
        
        output[0][i] = lsample * lgain;
        output[1][i] = rsample * rgain;
        lgain += lgain_delta;
        rgain += rgain_delta;
    }
    return CBOX_BLOCK_SIZE;
}

#else

static inline uint32_t process_voice_stereo_noloop(struct sampler_gen *v, float **output)
{
    float ffrac = 1.0f / 6.0f;
    float lgain = v->last_lgain * ffrac;
    float rgain = v->last_rgain * ffrac;
    float lgain_delta = (v->lgain - v->last_lgain) / CBOX_BLOCK_SIZE * ffrac;
    float rgain_delta = (v->rgain - v->last_rgain) / CBOX_BLOCK_SIZE * ffrac;
    float scaler = 1.0 / (256.0 * 65536.0);
    PREPARE_LOOP

    uint32_t oldpos = v->pos;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float t = (v->frac_pos >> 8) * scaler;
        int16_t *p = &sample_data[v->pos << 1];
        float b0 = -t*(t-1.f)*(t-2.f);
        float b1 = 3.f*(t+1.f)*(t-1.f)*(t-2.f);
        float c0 = (b0 * p[0] + b1 * p[2] - 3.f*(t+1.f)*t*(t-2.f) * p[4] + (t+1.f)*t*(t-1.f) * p[6]);
        float c1 = (b0 * p[1] + b1 * p[3] - 3.f*(t+1.f)*t*(t-2.f) * p[5] + (t+1.f)*t*(t-1.f) * p[7]);
        output[0][i] = lgain * c0;
        output[1][i] = rgain * c1;
        lgain += lgain_delta;
        rgain += rgain_delta;
        if (v->frac_pos > ~v->frac_delta)
            v->pos++;
        v->frac_pos += v->frac_delta;
        v->pos += v->delta;
    }
    v->consumed += v->pos - oldpos;
    return CBOX_BLOCK_SIZE;
}

static uint32_t process_voice_stereo(struct sampler_gen *v, float **output)
{
    PREPARE_LOOP

    if (v->pos < loop_end - v->loop_overlap)
    {
        uint32_t remain = loop_end - v->loop_overlap - v->pos;
        if (remain > CBOX_BLOCK_SIZE * (1 + v->delta))
            return process_voice_stereo_noloop(v, output);
    }

    float lgain = v->last_lgain;
    float rgain = v->last_rgain;
    float lgain_delta = (v->lgain - v->last_lgain) / CBOX_BLOCK_SIZE;
    float rgain_delta = (v->rgain - v->last_rgain) / CBOX_BLOCK_SIZE;
    uint32_t oldpos = v->pos;
    int32_t corr = 0;

    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        if (v->pos >= loop_end)
        {
            if (IS_LOOP_FINISHED)
            {
                v->mode = spt_inactive;
                return i;
            }
            v->pos = v->pos - loop_end + v->loop_start;
            corr += loop_end - v->loop_start;
            sample_data = v->sample_data = v->sample_data_loop;
            loop_end = v->loop_end = v->loop_end2;
            v->loop_count++;
        }
        
        float idata[2][4];
        if (v->pos + 4 < loop_end)
        {
            int16_t *p = &sample_data[v->pos << 1];
            for (int s = 0; s < 4; s++, p += 2)
            {
                idata[0][s] = p[0];
                idata[1][s] = p[1];
            }
        }
        else
        {
            uint32_t nextsample = v->pos;
            int16_t *cur_sample_data = sample_data; 
            int s;
            for (s = 0; s < 4; s++)
            {
                if (nextsample >= loop_end)
                {
                    if (v->loop_start == (uint32_t)-1)
                        break;
                    nextsample -= loop_end - v->loop_start;
                    cur_sample_data = sample_data_loop;
                }
                idata[0][s] = cur_sample_data[nextsample << 1];
                idata[1][s] = cur_sample_data[1 + (nextsample << 1)];
                nextsample++;
            }
            for(; s < 4; s++)
                idata[0][s] = idata[1][s] = 0;
        }
        if (v->loop_start != (uint32_t)-1 && v->pos >= loop_end - v->loop_overlap && v->loop_start > v->loop_overlap)
        {
            assert(v->sample_data == v->sample_data_loop);
            uint32_t nextsample = v->pos - (loop_end - v->loop_start);
            float xfade = (v->pos - (loop_end - v->loop_overlap)) * v->loop_overlap_step;
            for (int s = 0; s < 4 && xfade < 1; s++)
            {
                idata[0][s] += (sample_data[nextsample << 1] - idata[0][s]) * xfade;
                idata[1][s] += (sample_data[1 + (nextsample << 1)] - idata[1][s]) * xfade;
                nextsample++;
                xfade += v->loop_overlap_step;
            }
        }
        
        float ch[2] = {0, 0};
        float t = (v->frac_pos >> 8) * (1.0 / (256.0 * 65536.0));
        for (int c = 0; c < 2; c++)
        {
            ch[c] = (-t*(t-1.f)*(t-2.f) * idata[c][0] + 3.f*(t+1.f)*(t-1.f)*(t-2.f) * idata[c][1] - 3.f*(t+1.f)*t*(t-2.f) * idata[c][2] + (t+1.f)*t*(t-1.f) * idata[c][3]) * (1.0f / 6.0f);
        }
        
        if (v->frac_pos > ~v->frac_delta)
            v->pos++;
        v->frac_pos += v->frac_delta;
        v->pos += v->delta;
        
        output[0][i] = ch[0] * lgain;
        output[1][i] = ch[1] * rgain;
        lgain += lgain_delta;
        rgain += rgain_delta;
    }
    v->consumed += v->pos - oldpos + corr;
    return CBOX_BLOCK_SIZE;
}

#endif

void sampler_gen_reset(struct sampler_gen *v)
{
    v->mode = spt_inactive;
    v->frac_pos = 0;
    v->last_lgain = 0.f;
    v->last_rgain = 0.f;
    v->loop_count = 0;
    v->consumed = 0;
}

uint32_t sampler_gen_sample_playback(struct sampler_gen *v, float **tmp_outputs)
{
    uint32_t result;
#if LOW_QUALITY_INTERPOLATION    
    if (v->mode == spt_stereo16)
        result = process_voice_stereo_lerp(v, tmp_outputs);
    else
        result = process_voice_mono_lerp(v, tmp_outputs);
#else
    if (v->mode == spt_stereo16)
        result = process_voice_stereo(v, tmp_outputs);
    else
        result = process_voice_mono(v, tmp_outputs);
#endif
    v->last_lgain = v->lgain;
    v->last_rgain = v->rgain;
    return result;
}

