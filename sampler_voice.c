/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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
#include "errors.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_impl.h"
#include "sfzloader.h"
#include "stm.h"
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>


static void lfo_update_freq(struct sampler_lfo *lfo, struct sampler_lfo_params *lfop, int srate, double srate_inv)
{
    lfo->delta = (uint32_t)(lfop->freq * 65536.0 * 65536.0 * CBOX_BLOCK_SIZE * srate_inv);
    lfo->delay = (uint32_t)(lfop->delay * srate);
    lfo->fade = (uint32_t)(lfop->fade * srate);
    lfo->wave = (int32_t)(lfop->wave);
}

static void lfo_init(struct sampler_lfo *lfo, struct sampler_lfo_params *lfop, int srate, double srate_inv)
{
    lfo->phase = 0;
    lfo->xdelta = 0;
    lfo->random_value = 0; // safe, less CPU intensive value
    lfo_update_freq(lfo, lfop, srate, srate_inv);
}

static inline float lfo_wave_calculate(int wave, uint32_t phase)
{
    const int FRAC_BITS = 32 - 11;

    switch(wave) {
        case 0: // triangle
        {
            uint32_t ph = phase + 0x40000000;
            uint32_t tri = (ph & 0x7FFFFFFF) ^ (((ph >> 31) - 1) & 0x7FFFFFFF);
            return -1.0 + tri * (2.0 / (1U << 31));
        }
        case 1: // sine (default)
        default:
        {
            uint32_t iphase = phase >> FRAC_BITS;
            float frac = (phase & ((1 << FRAC_BITS) - 1)) * (1.0 / (1 << FRAC_BITS));
            return sampler_sine_wave[iphase] + (sampler_sine_wave[iphase + 1] - sampler_sine_wave[iphase]) * frac;
        }
        case 2:
            return phase < 0xC0000000 ? 1 : -1;
        case 3:
            return phase < 0x80000000 ? 1 : -1;
        case 4:
            return phase < 0x40000000 ? 1 : -1;
            break;
        case 5:
            return phase < 0x20000000 ? 1 : -1;
            break;
        case 6:
            return -1 + phase * (1.0 / (1U << 31));
        case 7:
            return 1 - phase * (1.0 / (1U << 31));
    }
}

static inline float sampler_voice_lfo_process(struct sampler_voice *voice, struct sampler_lfo *lfo)
{
    if (voice->age < lfo->delay)
        return 0.f;
    uint32_t delta = lfo->delta + lfo->xdelta;

    float v;
    if (lfo->wave == 12) {
        if ((lfo->phase & 0x80000000) != ((lfo->phase + lfo->delta) & 0x80000000))
            lfo->random_value = -1 + 2 * rand() / (1.0 * RAND_MAX);
        v = lfo->random_value;
    } else {
        v = lfo_wave_calculate(lfo->wave, lfo->delta);
    }
    lfo->phase += delta;
    if (lfo->fade && voice->age < lfo->delay + lfo->fade)
        v *= (voice->age - lfo->delay) * 1.0 / lfo->fade;

    return v;
}

static gboolean is_tail_finished(struct sampler_voice *v)
{
    if (!v->layer->computed.eff_num_stages)
        return TRUE;
    double eps = 1.0 / 65536.0;
    if (cbox_biquadf_is_audible(&v->filter.filter_left[0], eps))
        return FALSE;
    if (cbox_biquadf_is_audible(&v->filter.filter_right[0], eps))
        return FALSE;
    int num_stages = v->layer->computed.eff_num_stages;
    if (num_stages > 1)
    {
        if (cbox_biquadf_is_audible(&v->filter.filter_left[num_stages - 1], eps))
            return FALSE;
        if (cbox_biquadf_is_audible(&v->filter.filter_right[num_stages - 1], eps))
            return FALSE;
    }
    
    return TRUE;
}

#if USE_NEON

#include <arm_neon.h>

static inline void mix_block_into_with_gain(cbox_sample_t **outputs, int oofs, float *src_leftright, float gain)
{
    float *dst_left = outputs[oofs];
    float *dst_right = outputs[oofs + 1];
    float32x2_t gain2 = {gain, gain};
    for (size_t i = 0; i < CBOX_BLOCK_SIZE; i += 2)
    {
        float32x2_t lr1 = vld1_f32(&src_leftright[2 * i]);
        float32x2_t lr2 = vld1_f32(&src_leftright[2 * i + 2]);
        float32x2x2_t lr12 = vtrn_f32(lr1, lr2);
        float32x2_t dl1 = vld1_f32(&dst_left[i]);
        float32x2_t dr1 = vld1_f32(&dst_right[i]);
        
        float32x2_t l1 = vmla_f32(dl1, lr12.val[0], gain2);
        vst1_f32(&dst_left[i], l1);
        float32x2_t r1 = vmla_f32(dr1, lr12.val[1], gain2);
        vst1_f32(&dst_right[i], r1);
    }
}

static inline void mix_block_into(cbox_sample_t **outputs, int oofs, float *src_leftright)
{
    float *dst_left = outputs[oofs];
    float *dst_right = outputs[oofs + 1];
    for (size_t i = 0; i < CBOX_BLOCK_SIZE; i += 2)
    {
        float32x2_t lr1 = vld1_f32(&src_leftright[2 * i]);
        float32x2_t lr2 = vld1_f32(&src_leftright[2 * i + 2]);
        float32x2x2_t lr12 = vtrn_f32(lr1, lr2);
        float32x2_t dl1 = vld1_f32(&dst_left[i]);
        float32x2_t dr1 = vld1_f32(&dst_right[i]);
        
        float32x2_t l1 = vadd_f32(dl1, lr12.val[0]);
        vst1_f32(&dst_left[i], l1);
        float32x2_t r1 = vadd_f32(dr1, lr12.val[1]);
        vst1_f32(&dst_right[i], r1);
    }
}

#else

static inline void mix_block_into_with_gain(cbox_sample_t **outputs, int oofs, float *src_leftright, float gain)
{
    cbox_sample_t *dst_left = outputs[oofs];
    cbox_sample_t *dst_right = outputs[oofs + 1];
    for (size_t i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        dst_left[i] += gain * src_leftright[2 * i];
        dst_right[i] += gain * src_leftright[2 * i + 1];
    }
}

static inline void mix_block_into(cbox_sample_t **outputs, int oofs, float *src_leftright)
{
    cbox_sample_t *dst_left = outputs[oofs];
    cbox_sample_t *dst_right = outputs[oofs + 1];
    for (size_t i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        dst_left[i] += src_leftright[2 * i];
        dst_right[i] += src_leftright[2 * i + 1];
    }
}

#endif

////////////////////////////////////////////////////////////////////////////////

static float sfz_crossfade(float param, float xfin_lo, float xfin_hi, float xfout_lo, float xfout_hi, enum sampler_xf_curve xfc)
{
    if (param >= xfin_hi && param <= xfout_lo)
        return 1.f;
    if (param < xfin_lo || param > xfout_hi)
        return 0.f;
    float for0 = (param < xfout_lo) ? xfin_lo : xfout_hi;
    float for1 = (param < xfout_lo) ? xfin_hi : xfout_lo;
    if (for0 == for1)
        return 1.f;
    if (xfc == stxc_gain)
        return (param - for0) / (for1 - for0);
    else
    {
        float v = (param - for0) / (for1 - for0);
        return sqrtf(v);
    }
}

// One-half version for CC-based crossfades
static inline float sfz_crossfade2(float param, float xflo, float xfhi, float left, float right, enum sampler_xf_curve xfc)
{
    if (xflo > xfhi)
        return sfz_crossfade2(param, xfhi, xflo, right, left, xfc);
    if (param <= xflo)
        return left;
    if (param >= xfhi)
        return right;
    float res;
    if (xflo == xfhi)
        res = 0.5f * (left + right);
    else
        res = left + (right - left) * (param - xflo) / (xfhi - xflo);
    if (xfc == stxc_gain)
        return res;
    else
        return sqrtf(res);
}

////////////////////////////////////////////////////////////////////////////////

void sampler_voice_activate(struct sampler_voice *v, enum sampler_player_type mode)
{
    assert(v->gen.mode == spt_inactive);
    sampler_voice_unlink(&v->program->module->voices_free, v);
    assert(mode != spt_inactive);
    assert(v->channel);
    v->gen.mode = mode;
    sampler_voice_link(&v->channel->voices_running, v);
}

void sampler_voice_start_silent(struct sampler_layer_data *l, struct sampler_released_groups *exgroupdata)
{
    if (l->group >= 1)
        sampler_released_groups_add(exgroupdata, l->group);
}

void sampler_voice_start(struct sampler_voice *v, struct sampler_channel *c, struct sampler_layer_data *l, int note, int vel, struct sampler_released_groups *exgroupdata)
{
    struct sampler_module *m = c->module;
    sampler_gen_reset(&v->gen);
    
    v->age = 0;
    if (l->trigger == stm_release || l->trigger == stm_release_key)
    {
        // time since last 'note on' for that note
        v->age = m->current_time - c->prev_note_start_time[note];
        double age = v->age *  m->module.srate_inv;
        // if attenuation is more than 84dB, ignore the release trigger
        if (age * l->rt_decay > 84)
            return;
    }
    uint32_t end = l->computed.eff_waveform->info.frames;
    if (l->end != 0)
        end = (l->end == SAMPLER_NO_LOOP) ? 0 : l->end;
    v->last_waveform = l->computed.eff_waveform;
    v->gen.cur_sample_end = end;
    if (end > l->computed.eff_waveform->info.frames)
        end = l->computed.eff_waveform->info.frames;
    
    assert(!v->current_pipe);
    if (end > l->computed.eff_waveform->preloaded_frames)
    {
        if (l->computed.eff_loop_mode == slm_loop_continuous && l->computed.eff_loop_end < l->computed.eff_waveform->preloaded_frames)
        {
            // Everything fits in prefetch, because loop ends in prefetch and post-loop part is not being played
        }
        else
        {
            uint32_t loop_start = -1, loop_end = end;
            // If in loop mode, set the loop over the looped part... unless we're doing sustain-only loop on prefetch area only. Then
            // streaming will only cover the release part, and it shouldn't be looped.
            if (l->computed.eff_loop_mode == slm_loop_continuous || (l->computed.eff_loop_mode == slm_loop_sustain && l->computed.eff_loop_end >= l->computed.eff_waveform->preloaded_frames))
            {
                loop_start = l->computed.eff_loop_start;
                loop_end = l->computed.eff_loop_end;
            }
            // Those are initial values only, they will be adjusted in process function
            v->current_pipe = cbox_prefetch_stack_pop(m->pipe_stack, l->computed.eff_waveform, loop_start, loop_end, l->count);
            if (!v->current_pipe)
            {
                g_warning("Prefetch pipe pool exhausted, no streaming playback will be possible");
                end = l->computed.eff_waveform->preloaded_frames;
                v->gen.cur_sample_end = end;
            }
        }
    }
    
    v->output_pair_no = (l->output + c->output_shift) % m->output_pairs;
    v->serial_no = m->serial_no;

    v->gen.loop_overlap = l->loop_overlap;
    v->gen.loop_overlap_step = l->loop_overlap > 0 ? 1.0 / l->loop_overlap : 0;
    v->gain_fromvel = l->computed.eff_amp_velcurve[vel];
    v->gain_shift = (note - l->amp_keycenter) * l->amp_keytrack;

    v->gain_fromvel *= sfz_crossfade(note, l->xfin_lokey, l->xfin_hikey, l->xfout_lokey, l->xfout_hikey, l->xf_keycurve);
    v->gain_fromvel *= sfz_crossfade(vel, l->xfin_lovel, l->xfin_hivel, l->xfout_lovel, l->xfout_hivel, l->xf_velcurve);

    v->note = note;
    v->vel = vel;
    v->off_vel = 0;
    v->pitch_shift = 0;
    v->released = 0;
    v->released_with_sustain = 0;
    v->released_with_sostenuto = 0;
    v->captured_sostenuto = 0;
    v->channel = c;
    v->layer = l;
    v->program = c->program;
    v->amp_env.shape = &l->amp_env_shape;
    v->filter_env.shape = &l->filter_env_shape;
    v->pitch_env.shape = &l->pitch_env_shape;
    
    v->cutoff_shift = vel * l->fil_veltrack / 127.0 + (note - l->fil_keycenter) * l->fil_keytrack;
    v->cutoff2_shift = vel * l->fil2_veltrack / 127.0 + (note - l->fil2_keycenter) * l->fil2_keytrack;
    v->loop_mode = l->computed.eff_loop_mode;
    v->off_by = l->off_by;
    v->reloffset = l->reloffset;
    int auxes = (m->module.outputs - m->module.aux_offset) / 2;
    if (l->effect1bus >= 1 && l->effect1bus < 1 + auxes)
        v->send1bus = l->effect1bus;
    else
        v->send1bus = 0;
    if (l->effect2bus >= 1 && l->effect2bus < 1 + auxes)
        v->send2bus = l->effect2bus;
    else
        v->send2bus = 0;
    v->send1gain = l->effect1 * 0.01;
    v->send2gain = l->effect2 * 0.01;
    if (l->group >= 1)
    {
        sampler_released_groups_add(exgroupdata, l->group);
    }
    lfo_init(&v->amp_lfo, &l->amp_lfo, m->module.srate, m->module.srate_inv);
    lfo_init(&v->filter_lfo, &l->filter_lfo, m->module.srate, m->module.srate_inv);
    lfo_init(&v->pitch_lfo, &l->pitch_lfo, m->module.srate, m->module.srate_inv);
    
    for (int i = 0; i < 3; ++i)
    {
        cbox_biquadf_reset(&v->filter.filter_left[i]);
        cbox_biquadf_reset(&v->filter.filter_right[i]);
        cbox_biquadf_reset(&v->filter2.filter_left[i]);
        cbox_biquadf_reset(&v->filter2.filter_right[i]);
    }
    cbox_onepolef_reset(&v->onepole_left);
    cbox_onepolef_reset(&v->onepole_right);
    // set gain later (it's a less expensive operation)
    if (l->tonectl_freq != 0)
        cbox_onepolef_set_highshelf_tonectl(&v->onepole_coeffs, l->tonectl_freq * M_PI * m->module.srate_inv, 1.0);
    
    v->offset = l->offset;
    for(struct sampler_noteinitfunc *nif = v->layer->voice_nifs; nif; nif = nif->next)
        nif->key.notefunc_voice(nif, v);
    if (v->gain_shift)
        v->gain_fromvel *= dB2gain(v->gain_shift);

    if (v->offset + v->reloffset != 0)
    {
        // For streamed samples, allow only half the preload period worth of offset to avoid gaps
        // (maybe we can allow up to preload period minus one buffer size here?)
        uint32_t maxend = v->current_pipe ? (l->computed.eff_waveform->preloaded_frames >> 1) : l->computed.eff_waveform->preloaded_frames;
        int32_t pos = v->offset + v->reloffset * maxend * 0.01;
        if (pos < 0)
            pos = 0;
        if ((uint32_t)pos > maxend)
            pos = (int32_t)maxend;
        v->offset = pos;
    }
    
    cbox_envelope_reset(&v->amp_env);
    cbox_envelope_reset(&v->filter_env);
    cbox_envelope_reset(&v->pitch_env);

    v->last_eq_bitmask = 0;

    sampler_voice_activate(v, l->computed.eff_waveform->info.channels == 2 ? spt_stereo16 : spt_mono16);
    
    uint32_t pos = v->offset;
    if (l->offset_random)
        pos += ((uint32_t)(rand() + (rand() << 16))) % l->offset_random;
    if (pos >= end)
        pos = end;
    v->gen.bigpos = ((uint64_t)pos) << 32;
    v->gen.virtpos = ((uint64_t)pos) << 32;
    
    if (v->current_pipe && v->gen.bigpos)
        cbox_prefetch_pipe_consumed(v->current_pipe, v->gen.bigpos >> 32);

    for (struct sampler_flex_lfo *p = v->layer->flex_lfos; p; p = p->next) {
        if (p->key.id < MAX_FLEX_LFOS) {
            v->flexlfo_phase[p->key.id] = p->value.phase * 65536.0 * 65536.0;
        }
    }
    v->layer_changed = TRUE;
}

void sampler_voice_link(struct sampler_voice **pv, struct sampler_voice *v)
{
    v->prev = NULL;
    v->next = *pv;
    if (*pv)
        (*pv)->prev = v;
    *pv = v;
}

void sampler_voice_unlink(struct sampler_voice **pv, struct sampler_voice *v)
{
    if (*pv == v)
        *pv = v->next;
    if (v->prev)
        v->prev->next = v->next;
    if (v->next)
        v->next->prev = v->prev;
    v->prev = NULL;
    v->next = NULL;
}

void sampler_voice_inactivate(struct sampler_voice *v, gboolean expect_active)
{
    assert((v->gen.mode != spt_inactive) == expect_active);
    sampler_voice_unlink(&v->channel->voices_running, v);
    v->gen.mode = spt_inactive;
    if (v->current_pipe)
    {
        cbox_prefetch_stack_push(v->program->module->pipe_stack, v->current_pipe);
        v->current_pipe = NULL;
    }
    v->channel = NULL;
    sampler_voice_link(&v->program->module->voices_free, v);
}

void sampler_voice_release(struct sampler_voice *v, gboolean is_polyaft)
{
    if ((v->loop_mode == slm_one_shot_chokeable) != is_polyaft)
        return;
    if (v->loop_mode != slm_one_shot && !v->layer->count)
    {
        v->released = 1;
        if (v->loop_mode == slm_loop_sustain && v->current_pipe)
        {
            // Break the loop
            v->current_pipe->file_loop_end = v->gen.cur_sample_end;
            v->current_pipe->file_loop_start = -1;
        }
    }
}

void sampler_voice_update_params_from_layer(struct sampler_voice *v)
{
    struct sampler_layer_data *l = v->layer;
    struct sampler_module *m = v->program->module;
    lfo_update_freq(&v->amp_lfo, &l->amp_lfo, m->module.srate, m->module.srate_inv);
    lfo_update_freq(&v->filter_lfo, &l->filter_lfo, m->module.srate, m->module.srate_inv);
    lfo_update_freq(&v->pitch_lfo, &l->pitch_lfo, m->module.srate, m->module.srate_inv);
    cbox_envelope_update_shape(&v->amp_env, &l->amp_env_shape);
    cbox_envelope_update_shape(&v->filter_env, &l->filter_env_shape);
    cbox_envelope_update_shape(&v->pitch_env, &l->pitch_env_shape);
}

static inline void lfo_update_xdelta(struct sampler_module *m, struct sampler_lfo *lfo, uint32_t modmask, uint32_t dest, const float *moddests)
{
    if (!(modmask & (1 << dest)))
        lfo->xdelta = 0;
    else
        lfo->xdelta = (uint32_t)(moddests[dest] * 65536.0 * 65536.0 * CBOX_BLOCK_SIZE * m->module.srate_inv);
}

static inline void sampler_filter_process_control(struct sampler_filter *f, enum sampler_filter_type fil_type, float logcutoff, float resonance_linearized, const struct cbox_sincos *sincos_base)
{
    f->second_filter = &f->filter_coeffs;

    if (logcutoff < 0)
        logcutoff = 0;
    if (logcutoff > 12798)
        logcutoff = 12798;
    //float resonance = v->resonance*pow(32.0,c->cc[71]/maxv);
    float resonance = resonance_linearized;
    if (resonance < 0.7f)
        resonance = 0.7f;
    if (resonance > 32.f)
        resonance = 32.f;
    const struct cbox_sincos *sincos = &sincos_base[(int)logcutoff];
    switch(fil_type)
    {
    case sft_lp24hybrid:
        cbox_biquadf_set_lp_rbj_lookup(&f->filter_coeffs, sincos, resonance * resonance);
        cbox_biquadf_set_1plp_lookup(&f->filter_coeffs_extra, sincos, 1);
        f->second_filter = &f->filter_coeffs_extra;
        break;
    case sft_lp12:
    case sft_lp24:
    case sft_lp36:
        cbox_biquadf_set_lp_rbj_lookup(&f->filter_coeffs, sincos, resonance);
        break;
    case sft_hp12:
    case sft_hp24:
        cbox_biquadf_set_hp_rbj_lookup(&f->filter_coeffs, sincos, resonance);
        break;
    case sft_bp6:
    case sft_bp12:
        cbox_biquadf_set_bp_rbj_lookup(&f->filter_coeffs, sincos, resonance);
        break;
    case sft_lp6:
    case sft_lp12nr:
    case sft_lp24nr:
        cbox_biquadf_set_1plp_lookup(&f->filter_coeffs, sincos, fil_type != sft_lp6);
        break;
    case sft_hp6:
    case sft_hp12nr:
    case sft_hp24nr:
        cbox_biquadf_set_1php_lookup(&f->filter_coeffs, sincos, fil_type != sft_hp6);
        break;
    default:
        assert(0);
    }
}

static inline void sampler_filter_process_audio(struct sampler_filter *f, int num_stages, float *leftright)
{
    for (int i = 0; i < num_stages; ++i)
        cbox_biquadf_process_stereo(&f->filter_left[i], &f->filter_right[i], i ? f->second_filter : &f->filter_coeffs, leftright);
}

static inline void do_channel_mixing(float *leftright, uint32_t numsamples, float position, float width)
{
    float crossmix = (100.0 - width) * 0.005f;
    float amtleft = position > 0 ? 1 - 0.01 * position : 1;
    float amtright = position < 0 ? 1 - 0.01 * -position : 1;
    if (amtleft < 0)
        amtleft = 0;
    if (amtright < 0)
        amtright = 0;
    for (uint32_t i = 0; i < 2 * numsamples; i += 2) {
        float left = leftright[i], right = leftright[i + 1];
        float newleft = left + crossmix * (right - left);
        float newright = right + crossmix * (left - right);
        leftright[i] = newleft * amtleft;
        leftright[i + 1] = newright * amtright;
    }
}

static inline uint32_t sampler_gen_sample_playback_with_pipe(struct sampler_gen *gen, float *leftright, struct cbox_prefetch_pipe *current_pipe)
{
    if (!current_pipe)
        return sampler_gen_sample_playback(gen, leftright, (uint32_t)-1);

    uint32_t limit = cbox_prefetch_pipe_get_remaining(current_pipe);
    if (limit <= 4)
    {
        gen->mode = spt_inactive;
        return 0;
    }
    uint32_t samples = sampler_gen_sample_playback(gen, leftright, limit - 4);
    cbox_prefetch_pipe_consumed(current_pipe, gen->consumed);
    gen->consumed = 0;
    return samples;
}

static const float gain_for_num_stages[] = { 1, 1, 0.5, 0.33f };

static inline float sampler_voice_flexlfo_process(struct sampler_voice *v, uint32_t lfo_num, float *lfo_state, uint32_t *mask)
{
    if (lfo_num >= MAX_FLEX_LFOS)
        return 0;
    if (*mask & (1 << lfo_num))
        return lfo_state[lfo_num];
    float value = 0;
    struct sampler_flex_lfo *p = v->layer->computed.eff_flex_lfo_by_num[lfo_num];
    if (p) {
        double srate_inv = 1.0 / v->program->module->module.srate;
        float age_sec = v->age * srate_inv;
        if (age_sec >= p->value.delay) {
            age_sec -= p->value.delay;
            value = lfo_wave_calculate(p->value.wave, v->flexlfo_phase[lfo_num]);
            if (age_sec < p->value.fade)
                value *= age_sec / p->value.fade;
            v->flexlfo_phase[lfo_num] += p->value.freq * 65536.0 * 65536.0 * CBOX_BLOCK_SIZE * srate_inv;
        }
    }
    
    *mask |= 1 << lfo_num;
    lfo_state[lfo_num] = value;
    return value;
}

void sampler_voice_process(struct sampler_voice *v, struct sampler_module *m, cbox_sample_t **outputs)
{
    struct sampler_layer_data *l = v->layer;
    assert(v->gen.mode != spt_inactive);
    
    struct sampler_channel *c = v->channel;
    v->age += CBOX_BLOCK_SIZE;
    
    const float velscl = v->vel * (1.f / 127.f);

    struct cbox_envelope_shape *pitcheg_shape = v->pitch_env.shape, *fileg_shape = v->filter_env.shape, *ampeg_shape = v->amp_env.shape;
    if (__builtin_expect(l->computed.mod_bitmask, 0))
    {
        #define COPY_ORIG_SHAPE(envtype, envtype2, index) \
            if (l->computed.mod_bitmask & slmb_##envtype2##eg_cc) { \
                memcpy(&v->cc_envs[index], envtype2##eg_shape, sizeof(struct cbox_envelope_shape)); \
                envtype2##eg_shape = &v->cc_envs[index]; \
            }
        COPY_ORIG_SHAPE(amp, amp, 0)
        COPY_ORIG_SHAPE(filter, fil, 1)
        COPY_ORIG_SHAPE(pitch, pitch, 2)

        for(struct sampler_modulation *sm = l->modulations; sm; sm = sm->next)
        {
            // Simplified modulations for EG stages (CCs only)
            if (sm->key.dest >= smdest_eg_stage_start && sm->key.dest <= smdest_eg_stage_end)
            {
                float value = 0.f;
                if (sm->key.src < smsrc_pernote_offset)
                    value = sampler_channel_getcc_mod(c, v, sm->key.src, sm->value.curve_id, sm->value.step);
                uint32_t param = sm->key.dest - smdest_eg_stage_start;
                if (value * sm->value.amount != 0)
                    cbox_envelope_modify_dahdsr(&v->cc_envs[(param >> 4)], param & 0x0F, value * sm->value.amount, m->module.srate * 1.0 / CBOX_BLOCK_SIZE);
            }
        }
        #define UPDATE_ENV_POSITION(envtype, envtype2) \
            if (l->computed.mod_bitmask & slmb_##envtype##eg_cc) \
                cbox_envelope_update_shape_after_modify(&v->envtype2##_env, envtype##eg_shape, m->module.srate * 1.0 / CBOX_BLOCK_SIZE);
        UPDATE_ENV_POSITION(amp, amp)
        UPDATE_ENV_POSITION(fil, filter)
        UPDATE_ENV_POSITION(pitch, pitch)
    }

    // if it's a DAHD envelope without sustain, consider the note finished
    if (__builtin_expect(v->amp_env.cur_stage == 4 && ampeg_shape->stages[3].end_value <= 0.f, 0))
        cbox_envelope_go_to(&v->amp_env, 15);

    #define RECALC_EQ_MASK_EQ1 (7 << smdest_eq1_freq)
    #define RECALC_EQ_MASK_EQ2 (7 << smdest_eq2_freq)
    #define RECALC_EQ_MASK_EQ3 (7 << smdest_eq3_freq)
    #define RECALC_EQ_MASK_ALL (RECALC_EQ_MASK_EQ1 | RECALC_EQ_MASK_EQ2 | RECALC_EQ_MASK_EQ3)
    uint32_t recalc_eq_mask = 0;

    if (__builtin_expect(v->layer_changed, 0))
    {
        v->last_level = -1;
        if (v->last_waveform != v->layer->computed.eff_waveform)
        {
            v->last_waveform = v->layer->computed.eff_waveform;
            if (v->layer->computed.eff_waveform)
            {
                v->gen.mode = v->layer->computed.eff_waveform->info.channels == 2 ? spt_stereo16 : spt_mono16;
                v->gen.cur_sample_end = v->layer->computed.eff_waveform->info.frames;
            }
            else
            {
                sampler_voice_inactivate(v, TRUE);
                return;
            }
        }
        if (l->computed.eq_bitmask & (1 << 0)) recalc_eq_mask |= RECALC_EQ_MASK_EQ1;
        if (l->computed.eq_bitmask & (1 << 1)) recalc_eq_mask |= RECALC_EQ_MASK_EQ2;
        if (l->computed.eq_bitmask & (1 << 2)) recalc_eq_mask |= RECALC_EQ_MASK_EQ3;
        v->last_eq_bitmask = l->computed.eq_bitmask;
        v->layer_changed = FALSE;
    }

    float pitch = (v->note - l->computed.eff_pitch_keycenter) * l->pitch_keytrack + l->tune + l->transpose * 100 + v->pitch_shift;
    float modsrcs[smsrc_pernote_count];
    modsrcs[smsrc_vel - smsrc_pernote_offset] = v->vel * velscl;
    modsrcs[smsrc_pitch - smsrc_pernote_offset] = pitch * (1.f / 100.f);
    modsrcs[smsrc_chanaft - smsrc_pernote_offset] = c->last_chanaft * (1.f / 127.f);
    modsrcs[smsrc_polyaft - smsrc_pernote_offset] = sampler_channel_get_poly_pressure(c, v->note);
    modsrcs[smsrc_pitchenv - smsrc_pernote_offset] = cbox_envelope_get_value(&v->pitch_env, pitcheg_shape) * 0.01f;
    modsrcs[smsrc_filenv - smsrc_pernote_offset] = l->computed.eff_use_filter_mods ? cbox_envelope_get_value(&v->filter_env, fileg_shape) * 0.01f : 0;
    modsrcs[smsrc_ampenv - smsrc_pernote_offset] = cbox_envelope_get_value(&v->amp_env, ampeg_shape) * 0.01f;

    modsrcs[smsrc_amplfo - smsrc_pernote_offset] = sampler_voice_lfo_process(v, &v->amp_lfo);
    modsrcs[smsrc_fillfo - smsrc_pernote_offset] = l->computed.eff_use_filter_mods ? sampler_voice_lfo_process(v, &v->filter_lfo) : 0;
    modsrcs[smsrc_pitchlfo - smsrc_pernote_offset] = sampler_voice_lfo_process(v, &v->pitch_lfo);

    float moddests[smdestcount];
    moddests[smdest_pitch] = pitch;
    moddests[smdest_cutoff] = v->cutoff_shift;
    moddests[smdest_cutoff2] = v->cutoff2_shift;
    // These are always set
    uint32_t modmask = (1 << smdest_pitch) | (1 << smdest_cutoff) | (1 << smdest_cutoff2);
#if 0
    // Those are lazy-initialized using modmask.
    moddests[smdest_gain] = 0;
    moddests[smdest_resonance] = 0;
    moddests[smdest_tonectl] = 0;
    moddests[smdest_pitchlfo_freq] = 0;
    moddests[smdest_fillfo_freq] = 0;
    moddests[smdest_amplfo_freq] = 0;
#endif
    if (__builtin_expect(l->trigger == stm_release || l->trigger == stm_release_key, 0))
    {
        moddests[smdest_gain] = -v->age * l->rt_decay * m->module.srate_inv;
        modmask |= (1 << smdest_gain);
    }
    
    if (c->pitchwheel)
    {
        int pw = c->pitchwheel * (c->pitchwheel > 0 ? l->bend_up : -l->bend_down);
        // approximate dividing by 8191
        if (pw < 0)
            pw >>= 13;
        else
            pw = (pw + 4096) >> 13;
        if (l->bend_step > 1)
            pw = (pw / l->bend_step) * l->bend_step;
        moddests[smdest_pitch] += pw;
    }
    float flexlfo_state[MAX_FLEX_LFOS];
    uint32_t flexlfo_mask = 0;
    
    for (struct sampler_modulation *sm = l->modulations; sm; sm = sm->next)
    {
        enum sampler_modsrc src = sm->key.src;
        enum sampler_modsrc src2 = sm->key.src2;
        enum sampler_moddest dest = sm->key.dest;
        float value = 0.f, value2 = 1.f;
        if (src < smsrc_pernote_offset)
            value = sampler_channel_getcc_mod(c, v, src, sm->value.curve_id, sm->value.step);
        else if (IS_SMSRC_FLEXLFO(src)) {
            value = sampler_voice_flexlfo_process(v, SMSRC_FLEXLFO_NUM(src), flexlfo_state, &flexlfo_mask);
        } else
            value = modsrcs[src - smsrc_pernote_offset];

        if (src2 != smsrc_none)
        {
            if (src2 < smsrc_pernote_offset)
                value2 = sampler_channel_getcc_mod(c, v, src2, sm->value.curve_id, sm->value.step);
            else
                value2 = modsrcs[src2 - smsrc_pernote_offset];
            
            value *= value2;
        }
        if (dest < 32)
        {
            if (dest == smdest_amplitude)
            {
                if (!(modmask & (1 << dest))) // first value
                {
                    moddests[dest] = value * sm->value.amount;
                    modmask |= (1 << dest);
                }
                else
                    moddests[dest] *= value * sm->value.amount;
            }
            else if (!(modmask & (1 << dest))) // first value
            {
                moddests[dest] = value * sm->value.amount;
                modmask |= (1 << dest);
            }
            else
                moddests[dest] += value * sm->value.amount;
        }
    }
    lfo_update_xdelta(m, &v->pitch_lfo, modmask, smdest_pitchlfo_freq, moddests);
    if (l->computed.eff_use_filter_mods)
        lfo_update_xdelta(m, &v->filter_lfo, modmask, smdest_fillfo_freq, moddests);
    lfo_update_xdelta(m, &v->amp_lfo, modmask, smdest_amplfo_freq, moddests);
    recalc_eq_mask |= modmask;

    #define RECALC_EQ_IF(index) \
        if (recalc_eq_mask & RECALC_EQ_MASK_EQ##index) \
        { \
            float dfreq = velscl * l->eq##index.vel2freq + ((modmask & (1 << smdest_eq##index##_freq)) ? moddests[smdest_eq##index##_freq] : 0);\
            float fbw = (modmask & (1 << smdest_eq##index##_bw)) ? pow(0.5, moddests[smdest_eq##index##_bw]) : 1;\
            float dgain = velscl * l->eq##index.vel2gain + ((modmask & (1 << smdest_eq##index##_gain)) ? moddests[smdest_eq##index##_gain] : 0);\
            cbox_biquadf_set_peakeq_rbj_scaled(&v->eq_coeffs[index - 1], l->eq##index.effective_freq + dfreq, fbw / l->eq##index.bw, dB2gain(0.5 * (l->eq##index.gain + dgain)), m->module.srate); \
            if (!(v->last_eq_bitmask & (1 << (index - 1)))) \
            { \
                cbox_biquadf_reset(&v->eq_left[index-1]); \
                cbox_biquadf_reset(&v->eq_right[index-1]); \
            } \
        }
    if (__builtin_expect(recalc_eq_mask, 0))
    {
        RECALC_EQ_IF(1)
        RECALC_EQ_IF(2)
        RECALC_EQ_IF(3)
    }
    cbox_envelope_advance(&v->pitch_env, v->released, pitcheg_shape);
    if (l->computed.eff_use_filter_mods)
        cbox_envelope_advance(&v->filter_env, v->released, fileg_shape);
    cbox_envelope_advance(&v->amp_env, v->released, ampeg_shape);
    if (__builtin_expect(v->amp_env.cur_stage < 0, 0))
    {
        if (__builtin_expect(is_tail_finished(v), 0))
        {
            sampler_voice_inactivate(v, TRUE);
            return;
        }
    }
    
    double maxv = 127 << 7;
    double freq = l->computed.eff_freq * cent2factor(moddests[smdest_pitch]) ;
    uint64_t freq64 = (uint64_t)(freq * 65536.0 * 65536.0 * m->module.srate_inv);

    gboolean playing_sustain_loop = !v->released && v->loop_mode == slm_loop_sustain;
    uint32_t loop_start, loop_end;
    gboolean bandlimited = FALSE;

    if (!v->current_pipe)
    {
        v->gen.sample_data = v->last_waveform->data;
        if (v->last_waveform->levels)
        {
            gboolean use_cached = v->last_level > 0 && v->last_level < v->last_waveform->level_count
                && freq64 > v->last_level_min_rate && freq64 <= v->last_waveform->levels[v->last_level].max_rate;
            if (__builtin_expect(use_cached, 1))
            {
                v->gen.sample_data = v->last_waveform->levels[v->last_level].data;
                bandlimited = TRUE;
            }
            else
            {
                for (int i = 0; i < v->last_waveform->level_count; i++)
                {
                    if (freq64 <= v->last_waveform->levels[i].max_rate)
                    {
                        v->last_level = i;
                        v->gen.sample_data = v->last_waveform->levels[i].data;
                        bandlimited = TRUE;
                        
                        break;
                    }
                    v->last_level_min_rate = v->last_waveform->levels[i].max_rate;
                }
            }
        }
    }
    
    // XXXKF or maybe check for on-cc being in the on-cc range instead?
    gboolean play_loop = v->layer->computed.eff_loop_end && (v->loop_mode == slm_loop_continuous || playing_sustain_loop) && !v->layer->on_cc;
    loop_start = play_loop ? v->layer->computed.eff_loop_start : (v->layer->count ? 0 : (uint32_t)-1);
    loop_end = play_loop ? v->layer->computed.eff_loop_end : v->gen.cur_sample_end;

    if (v->current_pipe)
    {
        v->gen.sample_data = v->gen.loop_count ? v->current_pipe->data : v->last_waveform->data;
        v->gen.streaming_buffer = v->current_pipe->data;
        
        v->gen.prefetch_only_loop = (loop_end < v->last_waveform->preloaded_frames);
        v->gen.loop_overlap = 0;
        if (v->gen.prefetch_only_loop)
        {
            assert(!v->gen.in_streaming_buffer); // XXXKF this won't hold true when loops are edited while sound is being played (but that's not supported yet anyway)
            v->gen.loop_start = loop_start;
            v->gen.loop_end = loop_end;
            v->gen.streaming_buffer_frames = 0;
        }
        else
        {
            v->gen.loop_start = 0;
            v->gen.loop_end = v->last_waveform->preloaded_frames;
            v->gen.streaming_buffer_frames = v->current_pipe->buffer_loop_end;
        }
    }
    else
    {
        v->gen.loop_count = v->layer->count;
        v->gen.loop_start = loop_start;
        v->gen.loop_end = loop_end;
        
        if (!bandlimited)
        {
            // Use pre-calculated join
            v->gen.scratch = loop_start == (uint32_t)-1 ? v->layer->computed.scratch_end : v->layer->computed.scratch_loop;
        }
        else
        {
            // The standard waveforms have extra MAX_INTERPOLATION_ORDER of samples from the loop start added past loop_end,
            // to avoid wasting time generating the joins in all the practical cases. The slow path covers custom loops
            // (i.e. partial loop or no loop) over bandlimited versions of the standard waveforms, and those are probably
            // not very useful anyway, as changing the loop removes the guarantee of the waveform being bandlimited and
            // may cause looping artifacts or introduce DC offset (e.g. if only a positive part of a sine wave is looped).
            if (loop_start == 0 && loop_end == l->computed.eff_waveform->info.frames)
                v->gen.scratch = v->gen.sample_data + l->computed.eff_waveform->info.frames - MAX_INTERPOLATION_ORDER;
            else
            {
                // Generate the join for the current wave level
                // XXXKF this could be optimised further, by checking if waveform and loops are the same as the last
                // time. However, this code is not likely to be used... ever, so optimising it is not the priority.
                int shift = l->computed.eff_waveform->info.channels == 2 ? 1 : 0;
                uint32_t halfscratch = MAX_INTERPOLATION_ORDER << shift;
                
                v->gen.scratch = v->gen.scratch_bandlimited;
                memcpy(&v->gen.scratch_bandlimited[0], &v->gen.sample_data[(loop_end - MAX_INTERPOLATION_ORDER) << shift], halfscratch * sizeof(int16_t) );
                if (loop_start != (uint32_t)-1)
                    memcpy(v->gen.scratch_bandlimited + halfscratch, &v->gen.sample_data[loop_start << shift], halfscratch * sizeof(int16_t));
                else
                    memset(v->gen.scratch_bandlimited + halfscratch, 0, halfscratch * sizeof(int16_t));
            }
        }
    }
        
    if (l->timestretch)
    {
        v->gen.bigdelta = freq64;
        v->gen.virtdelta = (uint64_t)(l->computed.eff_freq * 65536.0 * 65536.0 * m->module.srate_inv);
        v->gen.stretching_jump = l->timestretch_jump;
        v->gen.stretching_crossfade = l->timestretch_crossfade;
    }
    else
    {
        v->gen.bigdelta = freq64;
        v->gen.virtdelta = freq64;
    }
    float gain = modsrcs[smsrc_ampenv - smsrc_pernote_offset] * l->volume_linearized * v->gain_fromvel * c->channel_volume_cc * sampler_channel_addcc(c, 11) / (maxv * maxv);
    if (l->computed.eff_use_xfcc) {
        for(struct sampler_cc_range *p = l->xfin_cc; p; p = p->next)
            gain *= sfz_crossfade2(c->intcc[p->key.cc_number], p->value.locc, p->value.hicc, 0, 1, l->xf_cccurve);
        for(struct sampler_cc_range *p = l->xfout_cc; p; p = p->next)
            gain *= sfz_crossfade2(c->intcc[p->key.cc_number], p->value.locc, p->value.hicc, 1, 0, l->xf_cccurve);
    }
    if ((modmask & (1 << smdest_gain)) && moddests[smdest_gain] != 0.f)
        gain *= dB2gain(moddests[smdest_gain]);

    float amplitude = l->amplitude;
    if ((modmask & (1 << smdest_amplitude)))
        amplitude *= moddests[smdest_amplitude];

    gain *= amplitude * (1.0 / 100.0);
    // http://drealm.info/sfz/plj-sfz.xhtml#amp "The overall gain must remain in the range -144 to 6 decibels."
    if (gain > 2.f)
        gain = 2.f;
    float pan = (l->pan + ((modmask & (1 << smdest_pan) ? moddests[smdest_pan] : 0)) + 100.f) * (1.f / 200.f) + (c->channel_pan_cc * 1.f / maxv - 0.5f) * 2.f;
    if (pan < 0.f)
        pan = 0.f;
    if (pan > 1.f)
        pan = 1.f;
    v->gen.lgain = gain * (1.f - pan)  / 32768.f;
    v->gen.rgain = gain * pan / 32768.f;

    if (l->cutoff != -1)
    {
        float mod_resonance = (modmask & (1 << smdest_resonance)) ? dB2gain(gain_for_num_stages[l->computed.eff_num_stages] * moddests[smdest_resonance]) : 1;
        sampler_filter_process_control(&v->filter, l->fil_type, l->computed.logcutoff + moddests[smdest_cutoff], l->computed.resonance_scaled * mod_resonance, m->sincos);
    }
    if (l->cutoff2 != -1)
    {
        float mod_resonance = (modmask & (1 << smdest_resonance2)) ? dB2gain(gain_for_num_stages[l->computed.eff_num_stages2] * moddests[smdest_resonance2]) : 1;
        sampler_filter_process_control(&v->filter2, l->fil2_type, l->computed.logcutoff2 + moddests[smdest_cutoff2], l->computed.resonance2_scaled * mod_resonance, m->sincos);
    }

    if (__builtin_expect(l->tonectl_freq != 0, 0))
    {
        float ctl = l->tonectl + (modmask & (1 << smdest_tonectl) ? moddests[smdest_tonectl] : 0);
        if (fabs(ctl) > 0.0001f)
            cbox_onepolef_set_highshelf_setgain(&v->onepole_coeffs, dB2gain(ctl));
        else
            cbox_onepolef_set_highshelf_setgain(&v->onepole_coeffs, 1.0);
    }

    // Audio processing starts here
    float leftright[2 * CBOX_BLOCK_SIZE];
        
    uint32_t samples = sampler_gen_sample_playback_with_pipe(&v->gen, leftright, v->current_pipe);
    if (l->computed.eff_use_channel_mixer)
        do_channel_mixing(leftright, samples, l->position, l->width);
    for (int i = 2 * samples; i < 2 * CBOX_BLOCK_SIZE; i++)
        leftright[i] = 0.f;

    if (l->cutoff != -1)
        sampler_filter_process_audio(&v->filter, l->computed.eff_num_stages, leftright);
    if (l->cutoff2 != -1)
        sampler_filter_process_audio(&v->filter2, l->computed.eff_num_stages2, leftright);

    if (__builtin_expect(l->tonectl_freq != 0, 0))
        cbox_onepolef_process_stereo(&v->onepole_left, &v->onepole_right, &v->onepole_coeffs, leftright);

    if (__builtin_expect(l->computed.eq_bitmask, 0))
    {
        for (int eq = 0; eq < 3; eq++)
        {
            if (l->computed.eq_bitmask & (1 << eq))
            { 
                cbox_biquadf_process_stereo(&v->eq_left[eq], &v->eq_right[eq], &v->eq_coeffs[eq], leftright);
            }
        }
    }
        
    mix_block_into(outputs, v->output_pair_no * 2, leftright);
    if (__builtin_expect((v->send1bus > 0 && v->send1gain != 0) || (v->send2bus > 0 && v->send2gain != 0), 0))
    {
        if (v->send1bus > 0 && v->send1gain != 0)
        {
            int oofs = m->module.aux_offset + (v->send1bus - 1) * 2;
            mix_block_into_with_gain(outputs, oofs, leftright, v->send1gain);
        }
        if (v->send2bus > 0 && v->send2gain != 0)
        {
            int oofs = m->module.aux_offset + (v->send2bus - 1) * 2;
            mix_block_into_with_gain(outputs, oofs, leftright, v->send2gain);
        }
    }
    if (v->gen.mode == spt_inactive)
        sampler_voice_inactivate(v, FALSE);
}

