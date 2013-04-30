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


static void lfo_init(struct sampler_lfo *lfo, struct sampler_lfo_params *lfop, int srate, double srate_inv)
{
    lfo->phase = 0;
    lfo->age = 0;
    lfo->delta = (uint32_t)(lfop->freq * 65536.0 * 65536.0 * CBOX_BLOCK_SIZE * srate_inv);
    lfo->delay = (uint32_t)(lfop->delay * srate);
    lfo->fade = (uint32_t)(lfop->fade * srate);
}

static inline float lfo_run(struct sampler_lfo *lfo)
{
    if (lfo->age < lfo->delay)
    {
        lfo->age += CBOX_BLOCK_SIZE;
        return 0.f;
    }

    const int FRAC_BITS = 32 - 11;
    lfo->phase += lfo->delta;
    uint32_t iphase = lfo->phase >> FRAC_BITS;
    float frac = (lfo->phase & ((1 << FRAC_BITS) - 1)) * (1.0 / (1 << FRAC_BITS));

    float v = sampler_sine_wave[iphase] + (sampler_sine_wave[iphase + 1] - sampler_sine_wave[iphase]) * frac;
    if (lfo->fade && lfo->age < lfo->delay + lfo->fade)
    {
        v *= (lfo->age - lfo->delay) * 1.0 / lfo->fade;
        lfo->age += CBOX_BLOCK_SIZE;
    }

    return v;
}

static gboolean is_4pole(struct sampler_layer_data *v)
{
    if (v->cutoff == -1)
        return FALSE;
    return v->fil_type == sft_lp24 || v->fil_type == sft_hp24 || v->fil_type == sft_bp12;
}

static gboolean is_tail_finished(struct sampler_voice *v)
{
    if (v->layer->cutoff == -1)
        return TRUE;
    double eps = 1.0 / 65536.0;
    if (cbox_biquadf_is_audible(&v->filter_left, eps))
        return FALSE;
    if (cbox_biquadf_is_audible(&v->filter_right, eps))
        return FALSE;
    if (is_4pole(v->layer))
    {
        if (cbox_biquadf_is_audible(&v->filter_left2, eps))
            return FALSE;
        if (cbox_biquadf_is_audible(&v->filter_right2, eps))
            return FALSE;
    }
    
    return TRUE;
}

static inline void mix_block_into_with_gain(cbox_sample_t **outputs, int oofs, float *src_left, float *src_right, float gain)
{
    cbox_sample_t *dst_left = outputs[oofs];
    cbox_sample_t *dst_right = outputs[oofs + 1];
    for (size_t i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        dst_left[i] += gain * src_left[i];
        dst_right[i] += gain * src_right[i];
    }
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

void sampler_voice_start(struct sampler_voice *v, struct sampler_channel *c, struct sampler_layer_data *l, int note, int vel, int *exgroups, int *pexgroupcount)
{
    struct sampler_module *m = c->module;
    sampler_gen_reset(&v->gen);
    
    v->age = 0;
    if (l->trigger == stm_release)
    {
        // time since last 'note on' for that note
        v->age = m->current_time - c->prev_note_start_time[note];
        double age = v->age *  m->module.srate_inv;
        // if attenuation is more than 84dB, ignore the release trigger
        if (age * l->rt_decay > 84)
            return;
    }
    uint32_t end = l->eff_waveform->info.frames;
    if (l->end != 0)
        end = (l->end == -1) ? 0 : l->end;
    v->last_waveform = l->eff_waveform;
    v->gen.cur_sample_end = end;
    if (end > l->eff_waveform->info.frames)
        end = l->eff_waveform->info.frames;
    
    if (end > l->eff_waveform->preloaded_frames)
    {
        // XXXKF allow looping
        v->current_pipe = cbox_prefetch_stack_pop(m->pipe_stack, l->eff_waveform, -1, end);
        if (!v->current_pipe)
            g_warning("Prefetch pipe pool exhausted, no streaming playback will be possible");
    }
    
    v->output_pair_no = l->output % m->output_pairs;
    v->serial_no = m->serial_no;
    
    uint32_t pos = l->offset;
    pos = l->offset;
    if (l->offset_random)
        pos += ((uint32_t)(rand() + (rand() << 16))) % l->offset_random;
    if (pos >= end)
        pos = end;
    v->gen.pos = pos;
    
    float delay = l->delay;
    if (l->delay_random)
        delay += rand() * (1.0 / RAND_MAX) * l->delay_random;
    if (delay > 0)
        v->delay = (int)(delay * m->module.srate);
    else
        v->delay = 0;
    v->gen.loop_overlap = l->loop_overlap;
    v->gen.loop_overlap_step = 1.0 / l->loop_overlap;    
    v->gain_fromvel = 1.0 + (l->eff_velcurve[vel] - 1.0) * l->amp_veltrack * 0.01;
    v->gain_shift = 0.0;
    v->note = note;
    v->vel = vel;
    v->pitch_shift = 0;
    v->released = 0;
    v->released_with_sustain = 0;
    v->released_with_sostenuto = 0;
    v->captured_sostenuto = 0;
    v->channel = c;
    v->layer = l;
    v->play_count = 0;
    v->program = c->program;
    v->amp_env.shape = &l->amp_env_shape;
    v->filter_env.shape = &l->filter_env_shape;
    v->pitch_env.shape = &l->pitch_env_shape;
    
    v->cutoff_shift = vel * l->fil_veltrack / 127.0 + (note - l->fil_keycenter) * l->fil_keytrack;
    v->loop_mode = l->loop_mode;
    v->off_by = l->off_by;
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
    if (l->group >= 1 && *pexgroupcount < MAX_RELEASED_GROUPS)
    {
        gboolean found = FALSE;
        for (int j = 0; j < *pexgroupcount; j++)
        {
            if (exgroups[j] == l->group)
            {
                found = TRUE;
                break;
            }
        }
        if (!found)
        {
            exgroups[(*pexgroupcount)++] = l->group;
        }
    }
    lfo_init(&v->amp_lfo, &l->amp_lfo, m->module.srate, m->module.srate_inv);
    lfo_init(&v->filter_lfo, &l->filter_lfo, m->module.srate, m->module.srate_inv);
    lfo_init(&v->pitch_lfo, &l->pitch_lfo, m->module.srate, m->module.srate_inv);
    
    cbox_biquadf_reset(&v->filter_left);
    cbox_biquadf_reset(&v->filter_right);
    cbox_biquadf_reset(&v->filter_left2);
    cbox_biquadf_reset(&v->filter_right2);
    
    GSList *nif = v->layer->nifs;
    while(nif)
    {
        struct sampler_noteinitfunc *p = nif->data;
        p->notefunc(p, v);
        nif = nif->next;
    }
    
    cbox_envelope_reset(&v->amp_env);
    cbox_envelope_reset(&v->filter_env);
    cbox_envelope_reset(&v->pitch_env);

    sampler_voice_activate(v, l->eff_waveform->info.channels == 2 ? spt_stereo16 : spt_mono16);
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
    if (v->delay >= v->age + CBOX_BLOCK_SIZE)
    {
        v->released = 1;
        sampler_voice_inactivate(v, TRUE);
    }
    else
    {
        if (v->loop_mode != slm_one_shot && !v->layer->count)
            v->released = 1;
    }
}

void sampler_voice_process(struct sampler_voice *v, struct sampler_module *m, cbox_sample_t **outputs)
{
    struct sampler_layer_data *l = v->layer;
    assert(v->gen.mode != spt_inactive);
    
    // if it's a DAHD envelope without sustain, consider the note finished
    if (v->amp_env.cur_stage == 4 && v->amp_env.shape->stages[3].end_value == 0)
        cbox_envelope_go_to(&v->amp_env, 15);                

    struct sampler_channel *c = v->channel;
    v->age += CBOX_BLOCK_SIZE;
    
    if (v->age < v->delay)
        return;

    if (v->last_waveform != v->layer->eff_waveform)
    {
        v->last_waveform = v->layer->eff_waveform;
        if (v->layer->eff_waveform)
        {
            v->gen.mode = v->layer->eff_waveform->info.channels == 2 ? spt_stereo16 : spt_mono16;
            v->gen.cur_sample_end = v->layer->eff_waveform->info.frames;
        }
        else
        {
            sampler_voice_inactivate(v, TRUE);
            return;
        }        
    }
    // XXXKF I'm sacrificing sample accuracy for delays for now
    v->delay = 0;
    
    float pitch = (v->note - l->pitch_keycenter) * l->pitch_keytrack + l->tune + l->transpose * 100 + v->pitch_shift;
    float modsrcs[smsrc_pernote_count];
    modsrcs[smsrc_vel - smsrc_pernote_offset] = v->vel * (1.0 / 127.0);
    modsrcs[smsrc_pitch - smsrc_pernote_offset] = pitch * (1.0 / 100.0);
    modsrcs[smsrc_polyaft - smsrc_pernote_offset] = 0; // XXXKF not supported yet
    modsrcs[smsrc_pitchenv - smsrc_pernote_offset] = cbox_envelope_get_next(&v->pitch_env, v->released) * 0.01f;
    modsrcs[smsrc_filenv - smsrc_pernote_offset] = cbox_envelope_get_next(&v->filter_env, v->released) * 0.01f;
    modsrcs[smsrc_ampenv - smsrc_pernote_offset] = cbox_envelope_get_next(&v->amp_env, v->released) * 0.01f;

    modsrcs[smsrc_amplfo - smsrc_pernote_offset] = lfo_run(&v->amp_lfo);
    modsrcs[smsrc_fillfo - smsrc_pernote_offset] = lfo_run(&v->filter_lfo);
    modsrcs[smsrc_pitchlfo - smsrc_pernote_offset] = lfo_run(&v->pitch_lfo);
    
    if (v->amp_env.cur_stage < 0)
    {
        if (is_tail_finished(v))
        {
            sampler_voice_inactivate(v, TRUE);
            return;
        }
    }
    
    float moddests[smdestcount];
    moddests[smdest_gain] = 0;
    moddests[smdest_pitch] = pitch;
    moddests[smdest_cutoff] = v->cutoff_shift;
    moddests[smdest_resonance] = 0;
    GSList *mod = l->modulations;
    if (l->trigger == stm_release)
        moddests[smdest_gain] -= v->age * l->rt_decay * m->module.srate_inv;
    
    if (c->pitchwheel)
        moddests[smdest_pitch] += c->pitchwheel * (c->pitchwheel > 0 ? l->bend_up : l->bend_down) >> 13;
    
    static const int modoffset[4] = {0, -1, -1, 1 };
    static const int modscale[4] = {1, 1, 2, -2 };
    while(mod)
    {
        struct sampler_modulation *sm = mod->data;
        float value = 0.f, value2 = 1.f;
        if (sm->src < smsrc_pernote_offset)
            value = c->cc[sm->src]  / 127.0;
        else
            value = modsrcs[sm->src - smsrc_pernote_offset];
        value = modoffset[sm->flags & 3] + value * modscale[sm->flags & 3];

        if (sm->src2 != smsrc_none)
        {
            if (sm->src2 < smsrc_pernote_offset)
                value2 = c->cc[sm->src2] / 127.0;
            else
                value2 = modsrcs[sm->src2 - smsrc_pernote_offset];
            
            value2 = modoffset[(sm->flags & 12) >> 2] + value2 * modscale[(sm->flags & 12) >> 2];
            value *= value2;
        }
        moddests[sm->dest] += value * sm->amount;
        
        mod = g_slist_next(mod);
    }
    
    double maxv = 127 << 7;
    double freq = l->eff_freq * cent2factor(moddests[smdest_pitch]) ;
    uint64_t freq64 = (uint64_t)(freq * 65536.0 * 65536.0 * m->module.srate_inv);
    if (!v->current_pipe)
    {
        v->gen.sample_data = v->last_waveform->data;
        if (v->last_waveform->levels)
        {
            // XXXKF: optimise later by caching last lookup value
            // XXXKF: optimise later by using binary search
            for (int i = 0; i < v->last_waveform->level_count; i++)
            {
                if (freq64 <= v->last_waveform->levels[i].max_rate)
                {
                    v->gen.sample_data = v->last_waveform->levels[i].data;
                    break;
                }
            }
        }
    }
    gboolean post_sustain = v->released && v->loop_mode == slm_loop_sustain;
    uint32_t loop_start, loop_end;
    if (v->layer->count > 0)
    {
        // End the loop on the last time
        gboolean play_loop = (v->play_count < v->layer->count - 1);
        loop_start = play_loop ? 0 : (uint32_t)-1;
        loop_end = v->gen.cur_sample_end;
    }
    else
    {
        gboolean play_loop = v->layer->loop_end && (v->loop_mode == slm_loop_continuous || (v->loop_mode == slm_loop_sustain && !post_sustain)) && v->layer->on_cc_number == -1;
        loop_start = play_loop ? v->layer->loop_start : (uint32_t)-1;
        loop_end = play_loop ? v->layer->loop_end : v->gen.cur_sample_end;
    }
    
    if (v->current_pipe)
    {
        v->current_pipe->file_loop_end = loop_end;
        v->current_pipe->file_loop_start = loop_start;
        v->gen.sample_data = v->gen.loop_count ? v->current_pipe->data : v->last_waveform->data;
        v->gen.sample_data_loop = v->current_pipe->data;
        
        v->gen.loop_start = 0;
        v->gen.loop_overlap = 0;
        v->gen.loop_end = v->gen.loop_count ? v->current_pipe->buffer_loop_end : v->last_waveform->preloaded_frames;
        v->gen.loop_end2 = v->current_pipe->buffer_loop_end;
    }
    else
    {
        v->gen.loop_start = loop_start;
        v->gen.loop_end = loop_end;
    }
        
    
    v->gen.delta = freq64 >> 32;
    v->gen.frac_delta = freq64 & 0xFFFFFFFF;
    float gain = modsrcs[smsrc_ampenv - smsrc_pernote_offset] * l->volume_linearized * v->gain_fromvel * sampler_channel_addcc(c, 7) * sampler_channel_addcc(c, 11) / (maxv * maxv);
    if (moddests[smdest_gain] != 0.0)
        gain *= dB2gain(moddests[smdest_gain]);
    // http://drealm.info/sfz/plj-sfz.xhtml#amp "The overall gain must remain in the range -144 to 6 decibels."
    if (gain > 2)
        gain = 2;
    float pan = (l->pan + 100) * (1.0 / 200.0) + (sampler_channel_addcc(c, 10) * 1.0 / maxv - 0.5) * 2;
    if (pan < 0)
        pan = 0;
    if (pan > 1)
        pan = 1;
    v->gen.lgain = gain * (1 - pan)  / 32768.0;
    v->gen.rgain = gain * pan / 32768.0;
    if (l->cutoff != -1)
    {
        float cutoff = l->cutoff * cent2factor(moddests[smdest_cutoff]);
        if (cutoff < 20)
            cutoff = 20;
        if (cutoff > m->module.srate * 0.45)
            cutoff = m->module.srate * 0.45;
        //float resonance = v->resonance*pow(32.0,c->cc[71]/maxv);
        float resonance = l->resonance_linearized * dB2gain(moddests[smdest_resonance]);
        if (resonance < 0.7)
            resonance = 0.7;
        if (resonance > 32)
            resonance = 32;
        // XXXKF this is found experimentally and probably far off from correct formula
        if (is_4pole(v->layer))
            resonance = sqrt(resonance / 0.707) * 0.5;
        switch(l->fil_type)
        {
        case sft_lp12:
        case sft_lp24:
            cbox_biquadf_set_lp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_hp12:
        case sft_hp24:
            cbox_biquadf_set_hp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_bp6:
        case sft_bp12:
            cbox_biquadf_set_bp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_lp6:
            cbox_biquadf_set_1plp(&v->filter_coeffs, cutoff, m->module.srate);
            break;
        case sft_hp6:
            cbox_biquadf_set_1php(&v->filter_coeffs, cutoff, m->module.srate);
            break;
        default:
            assert(0);
        }
    }
    
    float left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
    float *tmp_outputs[2] = {left, right};
    uint32_t samples = 0;
        
    samples = sampler_gen_sample_playback(&v->gen, tmp_outputs);

    if (v->current_pipe)
    {
        cbox_prefetch_pipe_consumed(v->current_pipe, v->gen.consumed);
        v->gen.consumed = 0;
    }
    else
        v->play_count = v->gen.loop_count;

    for (int i = samples; i < CBOX_BLOCK_SIZE; i++)
        left[i] = right[i] = 0.f;
    if (l->cutoff != -1)
    {
        cbox_biquadf_process(&v->filter_left, &v->filter_coeffs, left);
        if (is_4pole(v->layer))
            cbox_biquadf_process(&v->filter_left2, &v->filter_coeffs, left);
        cbox_biquadf_process(&v->filter_right, &v->filter_coeffs, right);
        if (is_4pole(v->layer))
            cbox_biquadf_process(&v->filter_right2, &v->filter_coeffs, right);
    }
    mix_block_into_with_gain(outputs, v->output_pair_no * 2, left, right, 1.0);
    if ((v->send1bus > 0 && v->send1gain != 0) || (v->send2bus > 0 && v->send2gain != 0))
    {
        if (v->send1bus > 0 && v->send1gain != 0)
        {
            int oofs = m->module.aux_offset + (v->send1bus - 1) * 2;
            mix_block_into_with_gain(outputs, oofs, left, right, v->send1gain);
        }
        if (v->send2bus > 0 && v->send2gain != 0)
        {
            int oofs = m->module.aux_offset + (v->send2bus - 1) * 2;
            mix_block_into_with_gain(outputs, oofs, left, right, v->send2gain);
        }
    }
    if (v->gen.mode == spt_inactive)
        sampler_voice_inactivate(v, FALSE);
}

