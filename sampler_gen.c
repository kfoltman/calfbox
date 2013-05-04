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

struct resampler_state
{
    float *left, *right;
    int offset;
    float lgain, rgain, lgain_delta, rgain_delta;
};

static inline void process_voice_stereo_noloop(struct sampler_gen *v, struct resampler_state *rs, const int16_t *srcdata, uint32_t pos_offset, int endpos)
{
    const float ffrac = 1.0f / 6.0f;
    const float scaler = 1.0 / (256.0 * 65536.0);

    pos_offset = pos_offset << 1;
    for (int i = rs->offset; i < endpos; i++)
    {
        float t = ((v->bigpos >> 8) & 0x00FFFFFF) * scaler;
        const int16_t *p = &srcdata[((v->bigpos >> 31) - pos_offset) & ~1];
        float b0 = -t*(t-1.f)*(t-2.f);
        float b1 = 3.f*(t+1.f)*(t-1.f)*(t-2.f);
        float c0 = (b0 * p[0] + b1 * p[2] - 3.f*(t+1.f)*t*(t-2.f) * p[4] + (t+1.f)*t*(t-1.f) * p[6]) * ffrac;
        float c1 = (b0 * p[1] + b1 * p[3] - 3.f*(t+1.f)*t*(t-2.f) * p[5] + (t+1.f)*t*(t-1.f) * p[7]) * ffrac;
        rs->left[i] = rs->lgain * c0;
        rs->right[i] = rs->rgain * c1;
        rs->lgain += rs->lgain_delta;
        rs->rgain += rs->rgain_delta;
        v->bigpos += v->bigdelta;
    }
    rs->offset = endpos;
}

static inline void process_voice_mono_noloop(struct sampler_gen *v, struct resampler_state *rs, const int16_t *srcdata, uint32_t pos_offset, int endpos)
{
    const float ffrac = 1.0f / 6.0f;
    const float scaler = 1.0 / (256.0 * 65536.0);

    for (int i = rs->offset; i < endpos; i++)
    {
        float t = ((v->bigpos >> 8) & 0x00FFFFFF) * scaler;
        const int16_t *p = &srcdata[(v->bigpos >> 32) - pos_offset];
        float b0 = -t*(t-1.f)*(t-2.f);
        float b1 = 3.f*(t+1.f)*(t-1.f)*(t-2.f);
        float c = (b0 * p[0] + b1 * p[1] - 3.f*(t+1.f)*t*(t-2.f) * p[2] + (t+1.f)*t*(t-1.f) * p[3]) * ffrac;
        rs->left[i] = rs->lgain * c;
        rs->right[i] = rs->rgain * c;
        rs->lgain += rs->lgain_delta;
        rs->rgain += rs->rgain_delta;
        v->bigpos += v->bigdelta;
    }
    rs->offset = endpos;
}

static inline uint32_t process_voice_noloop(struct sampler_gen *v, struct resampler_state *rs, const int16_t *srcdata, uint32_t pos_offset, int endpos)
{
    assert(endpos > rs->offset && endpos <= CBOX_BLOCK_SIZE);
    uint32_t oldpos = v->bigpos >> 32;
    if (v->mode == spt_stereo16)
        process_voice_stereo_noloop(v, rs, srcdata, pos_offset, endpos);
    else
        process_voice_mono_noloop(v, rs, srcdata, pos_offset, endpos);
    return (v->bigpos >> 32) - oldpos;
}

#define MAX_INTERPOLATION_ORDER 3

static void process_voice_withloop(struct sampler_gen *v, struct resampler_state *rs, uint32_t limit)
{
    // This is the first frame where interpolation will cross the loop boundary
    uint32_t loop_end = (v->loop_count && v->streaming_buffer) ? v->streaming_buffer_frames : v->loop_end;
    uint32_t loop_edge = loop_end - MAX_INTERPOLATION_ORDER;
    int16_t *post_loop_source_data = v->streaming_buffer ? v->streaming_buffer : v->sample_data;
    int16_t scratch[2 * MAX_INTERPOLATION_ORDER * 2];
    
    while ( limit && rs->offset < CBOX_BLOCK_SIZE ) {
        uint64_t startframe = v->bigpos >> 32;
        
        int16_t *source_data = v->loop_count ? post_loop_source_data : v->sample_data;
        uint32_t source_offset = 0;
        uint32_t usable_sample_end = loop_edge;
        // if the first frame to play is already within 3 frames of loop end
        // (we need consecutive 4 frames for cubic interpolation) then
        // "straighten out" the area around the loop, and play that
        if (startframe >= loop_edge)
        {
            // if fully past the loop end, then it's normal wraparound
            // (or end of the sample if not looping)
            if (startframe >= loop_end)
            {
                if (v->loop_start == (uint32_t)-1)
                {
                    v->mode = spt_inactive;
                    return;
                }
                v->bigpos -= (uint64_t)(loop_end - v->loop_start) << 32;
                v->loop_count++;
                if (v->streaming_buffer)
                {
                    loop_end = v->streaming_buffer_frames;
                    loop_edge = loop_end - MAX_INTERPOLATION_ORDER;
                }
                continue;
            }

            int shift = (v->mode == spt_stereo16) ? 1 : 0;
            
            // 'linearize' the virtual circular buffer - write 3 (or N) frames before end of the loop
            // and 3 (N) frames at the start of the loop, and play it; in rare cases this will need to be
            // repeated twice if output write pointer is close to CBOX_BLOCK_SIZE or playback rate is very low,
            // but that's OK.
            uint32_t halfscratch = MAX_INTERPOLATION_ORDER << shift;
            memcpy(&scratch[0], &source_data[(loop_end - MAX_INTERPOLATION_ORDER) << shift], halfscratch * sizeof(int16_t) );
            if (v->loop_start == (uint32_t)-1)
                memset(scratch + halfscratch, 0, halfscratch * sizeof(int16_t));
            else
                memcpy(scratch + halfscratch, &post_loop_source_data[v->loop_start << shift], halfscratch * sizeof(int16_t));

            usable_sample_end = loop_end;
            source_data = scratch;
            source_offset = loop_edge;
        }
        if (limit != (uint32_t)-1 && usable_sample_end - startframe > limit)
        {
            usable_sample_end = startframe + limit;
        }

        uint32_t out_frames = CBOX_BLOCK_SIZE - rs->offset;
        uint64_t sample_end64 = ((uint64_t)usable_sample_end) << 32;
        // Check how many frames can be written to output buffer without going
        // past usable_sample_end.
        if (v->bigpos + (out_frames - 1) * v->bigdelta >= sample_end64)
            out_frames = (sample_end64 - v->bigpos) / v->bigdelta + 1;
        uint32_t consumed = process_voice_noloop(v, rs, source_data, source_offset, rs->offset + out_frames);
        if (consumed > limit)
            consumed = limit;
        v->consumed += consumed;
        if (consumed < limit)
            limit -= consumed;
        else
            break;
    }
}

void sampler_gen_reset(struct sampler_gen *v)
{
    v->mode = spt_inactive;
    v->bigpos = 0;
    v->last_lgain = 0.f;
    v->last_rgain = 0.f;
    v->loop_count = 0;
    v->consumed = 0;
}

uint32_t sampler_gen_sample_playback(struct sampler_gen *v, float *left, float *right, uint32_t limit)
{
    struct resampler_state rs;
    rs.left = left;
    rs.right = right;
    rs.offset = 0;
    rs.lgain = v->last_lgain;
    rs.rgain = v->last_rgain;
    rs.lgain_delta = (v->lgain - v->last_lgain) * (1.f / CBOX_BLOCK_SIZE);
    rs.rgain_delta = (v->rgain - v->last_rgain) * (1.f / CBOX_BLOCK_SIZE);
    process_voice_withloop(v, &rs, limit);
    v->last_lgain = v->lgain;
    v->last_rgain = v->rgain;
    return rs.offset;
}

