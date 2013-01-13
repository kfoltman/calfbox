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
#include "sampler_impl.h"
#include "sfzloader.h"
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_RELEASED_GROUPS 4

static float sine_wave[2049];

GQuark cbox_sampler_error_quark()
{
    return g_quark_from_string("cbox-sampler-error-quark");
}

CBOX_CLASS_DEFINITION_ROOT(sampler_program)

static void sampler_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs);
static void sampler_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len);
static void sampler_destroyfunc(struct cbox_module *module);

GSList *skip_inactive_layers(struct sampler_program *prg, struct sampler_channel *c, GSList *next_layer, int note, int vel, float random)
{
    int ch = (c - c->module->channels) + 1;
    for(;next_layer;next_layer = g_slist_next(next_layer))
    {
        struct sampler_layer *l = next_layer->data;
        if (!l->waveform)
            continue;
        if (l->sw_last != -1)
        {
            if (note >= l->sw_lokey && note <= l->sw_hikey)
                l->last_key = note;
        }
        if (note >= l->lokey && note <= l->hikey && vel >= l->lovel && vel <= l->hivel && ch >= l->lochan && ch <= l->hichan && random >= l->lorand && random < l->hirand)
        {
            if (!l->use_keyswitch || 
                ((l->sw_last == -1 || l->sw_last == l->last_key) &&
                 (l->sw_down == -1 || (c->switchmask[l->sw_down >> 5] & (1 << (l->sw_down & 31)))) &&
                 (l->sw_up == -1 || !(c->switchmask[l->sw_up >> 5] & (1 << (l->sw_up & 31)))) &&
                 (l->sw_previous == -1 || l->sw_previous == c->previous_note)))
            {
                gboolean play = l->current_seq_position == 1;
                l->current_seq_position++;
                if (l->current_seq_position >= l->seq_length)
                    l->current_seq_position = 1;
                if (play)
                    return next_layer;
            }
        }
    }
    return NULL;
}

static void lfo_init(struct sampler_lfo *lfo, struct sampler_lfo_params *lfop, int srate)
{
    lfo->phase = 0;
    lfo->age = 0;
    lfo->delta = (uint32_t)(lfop->freq * 65536.0 * 65536.0 * CBOX_BLOCK_SIZE / srate);
    lfo->delay = (uint32_t)(lfop->delay * srate);
    lfo->fade = (uint32_t)(lfop->fade * srate);
}


static void sampler_voice_release(struct sampler_voice *v, gboolean is_polyaft);

static void sampler_start_voice(struct sampler_module *m, struct sampler_channel *c, struct sampler_voice *v, struct sampler_layer *l, int note, int vel, int *exgroups, int *pexgroupcount)
{
    uint32_t end = l->waveform->info.frames;
    if (l->end != 0)
        end = (l->end == -1) ? 0 : l->end;
    v->last_waveform = l->waveform;
    v->cur_sample_end = end;
    if (end > l->waveform->info.frames)
        end = l->waveform->info.frames;
    
    double pitch = ((note - l->pitch_keycenter) * l->pitch_keytrack + l->tune + l->transpose * 100);
    
    v->output_pair_no = l->output % m->output_pairs;
    v->serial_no = m->serial_no;
    v->pos = l->offset;
    if (l->offset_random)
        v->pos += ((uint32_t)(rand() + (rand() << 16))) % l->offset_random;
    if (v->pos >= end)
        v->pos = end;
    float delay = l->delay;
    if (l->delay_random)
        delay += rand() * (1.0 / RAND_MAX) * l->delay_random;
    if (delay > 0)
        v->delay = (int)(delay * m->module.srate);
    else
        v->delay = 0;
    v->frac_pos = 0;
    v->loop_start = l->loop_start;
    v->loop_overlap = l->loop_overlap;
    v->loop_overlap_step = 1.0 / l->loop_overlap;    
    v->gain = l->volume_linearized * (1.0 + (l->velcurve[vel] - 1.0) * l->amp_veltrack * 0.01);
    v->note = note;
    v->vel = vel;
    v->mode = l->waveform->info.channels == 2 ? spt_stereo16 : spt_mono16;
    v->filter = l->filter;
    v->base_freq = l->freq;
    v->pitch = pitch;
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
    v->last_lgain = 0;
    v->last_rgain = 0;
    if (l->cutoff != -1)
        v->cutoff = l->cutoff * pow(2.0, (vel * l->fil_veltrack / 127.0 + (note - l->fil_keycenter) * l->fil_keytrack) / 1200.0);
    else
        v->cutoff = -1;
    v->resonance = l->resonance_linearized;
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
    lfo_init(&v->amp_lfo, &l->amp_lfo_params, m->module.srate);
    lfo_init(&v->filter_lfo, &l->filter_lfo_params, m->module.srate);
    lfo_init(&v->pitch_lfo, &l->pitch_lfo_params, m->module.srate);
    
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

}

void sampler_start_note(struct sampler_module *m, struct sampler_channel *c, int note, int vel)
{
    float random = rand() * 1.0 / (RAND_MAX + 1.0);
    c->switchmask[note >> 5] |= 1 << (note & 31);
    struct sampler_program *prg = c->program;
    if (!prg)
        return;
    GSList *next_layer = skip_inactive_layers(prg, c, prg->layers, note, vel, random);
    if (!next_layer)
    {
        c->previous_note = note;
        return;
    }
    
    // this might perhaps be optimized by mapping the group identifiers to flat-array indexes
    // but I'm not going to do that until someone gives me an SFZ worth doing that work ;)
    int exgroups[MAX_RELEASED_GROUPS], exgroupcount = 0;
    
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        if (m->voices[i].mode == spt_inactive)
        {
            struct sampler_voice *v = &m->voices[i];
            struct sampler_layer *l = next_layer->data;
            sampler_start_voice(m, c, v, l, note, vel, exgroups, &exgroupcount);
            next_layer = skip_inactive_layers(prg, c, g_slist_next(next_layer), note, vel, random);
            if (!next_layer)
                break;
        }
    }
    c->previous_note = note;
    if (exgroupcount)
    {
        for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
        {
            struct sampler_voice *v = &m->voices[i];
            if (v->mode == spt_inactive)
                continue;

            for (int j = 0; j < exgroupcount; j++)
            {
                if (v->off_by == exgroups[j] && v->note != note)
                {
                    if (v->layer->off_mode == som_fast)
                    {
                        v->released = 1;
                        cbox_envelope_go_to(&v->amp_env, 15);
                    }
                    else
                    {
                        v->released = 1;
                    }
                    break;
                }
            }
        }
    }
}

void sampler_voice_release(struct sampler_voice *v, gboolean is_polyaft)
{
    if ((v->loop_mode == slm_one_shot_chokeable) != is_polyaft)
        return;
    if (v->delay >= CBOX_BLOCK_SIZE)
    {
        v->released = 1;
        v->mode = spt_inactive;
        return;
    }
    
    if (v->loop_mode != slm_one_shot)
        v->released = 1;
    else
        v->loop_start = -1; // should be guaranteed by layer settings anyway
}

void sampler_stop_note(struct sampler_module *m, struct sampler_channel *c, int note, int vel, gboolean is_polyaft)
{
    c->switchmask[note >> 5] &= ~(1 << (note & 31));
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        if (v->mode != spt_inactive && v->channel == c && v->note == note)
        {
            if (v->captured_sostenuto)
                v->released_with_sostenuto = 1;
            else if (c->cc[64] >= 64)
                v->released_with_sustain = 1;
            else
                sampler_voice_release(v, is_polyaft);
        }
    }
}

void sampler_stop_sustained(struct sampler_module *m, struct sampler_channel *c)
{
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        if (v->mode != spt_inactive && v->channel == c && v->released_with_sustain)
        {
            sampler_voice_release(v, FALSE);
            v->released_with_sustain = 0;
        }
    }
}

void sampler_stop_sostenuto(struct sampler_module *m, struct sampler_channel *c)
{
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        if (v->mode != spt_inactive && v->channel == c && v->released_with_sostenuto)
        {
            sampler_voice_release(v, FALSE);
            v->released_with_sostenuto = 0;
            // XXXKF unsure what to do with sustain
        }
    }
}

void sampler_capture_sostenuto(struct sampler_module *m, struct sampler_channel *c)
{
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        if (v->mode != spt_inactive && v->channel == c && !v->released && v->loop_mode != slm_one_shot && v->loop_mode != slm_one_shot_chokeable)
        {
            // XXXKF unsure what to do with sustain
            v->captured_sostenuto = 1;
        }
    }
}

void sampler_stop_all(struct sampler_module *m, struct sampler_channel *c)
{
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        if (v->mode != spt_inactive && v->channel == c)
        {
            sampler_voice_release(v, v->loop_mode == slm_one_shot_chokeable);
            v->released_with_sustain = 0;
            v->released_with_sostenuto = 0;
            v->captured_sostenuto = 0;
        }
    }
}

void sampler_steal_voice(struct sampler_module *m)
{
    int max_age = 0, voice_found = -1;
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        if (v->mode == spt_inactive)
            continue;
        if (v->amp_env.cur_stage == 15)
            continue;
        int age = m->serial_no - v->serial_no;
        if (v->loop_start == -1)
            age += (int)(v->pos * 100.0 / v->cur_sample_end);
        else
        if (v->released)
            age += 10;
        if (age > max_age)
        {
            max_age = age;
            voice_found = i;
        }
    }
    if (voice_found != -1)
    {
        m->voices[voice_found].released = 1;
        cbox_envelope_go_to(&m->voices[voice_found].amp_env, 15);        
    }
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

    float v = sine_wave[iphase] + (sine_wave[iphase + 1] - sine_wave[iphase]) * frac;
    if (lfo->fade && lfo->age < lfo->delay + lfo->fade)
    {
        v *= (lfo->age - lfo->delay) * 1.0 / lfo->fade;
        lfo->age += CBOX_BLOCK_SIZE;
    }

    return v;
}

static inline float clip01(float v)
{
    if (v < 0.f)
        return 0;
    if (v > 1.f)
        return 1;
    return v;
}

static gboolean is_4pole(struct sampler_voice *v)
{
    if (v->cutoff == -1)
        return FALSE;
    return v->filter == sft_lp24 || v->filter == sft_hp24 || v->filter == sft_bp12;
}

static gboolean is_tail_finished(struct sampler_voice *v)
{
    if (v->cutoff == -1)
        return TRUE;
    double eps = 1.0 / 65536.0;
    if (cbox_biquadf_is_audible(&v->filter_left, eps))
        return FALSE;
    if (cbox_biquadf_is_audible(&v->filter_right, eps))
        return FALSE;
    if (is_4pole(v))
    {
        if (cbox_biquadf_is_audible(&v->filter_left2, eps))
            return FALSE;
        if (cbox_biquadf_is_audible(&v->filter_right2, eps))
            return FALSE;
    }
    
    return TRUE;
}

static inline int addcc(struct sampler_channel *c, int cc_no)
{
    return (((int)c->cc[cc_no]) << 7) + c->cc[cc_no + 32];
}

void sampler_voice_process(struct sampler_voice *v, struct sampler_module *m, cbox_sample_t **outputs)
{
    // if it's a DAHD envelope without sustain, consider the note finished
    if (v->amp_env.cur_stage == 4 && v->amp_env.shape->stages[3].end_value == 0)
        cbox_envelope_go_to(&v->amp_env, 15);                

    struct sampler_channel *c = v->channel;
    
    if (v->delay >= CBOX_BLOCK_SIZE)
    {
        v->delay -= CBOX_BLOCK_SIZE;
        return;
    }
    if (v->last_waveform != v->layer->waveform)
    {
        v->last_waveform = v->layer->waveform;
        if (v->layer->waveform)
        {
            v->mode = v->layer->waveform->info.channels == 2 ? spt_stereo16 : spt_mono16;
            v->cur_sample_end = v->layer->waveform->info.frames;
        }
        else
        {
            v->mode = spt_inactive;
            return;
        }        
    }
    // XXXKF I'm sacrificing sample accuracy for delays for now
    v->delay = 0;
    
    float modsrcs[smsrc_pernote_count];
    modsrcs[smsrc_vel - smsrc_pernote_offset] = v->vel * (1.0 / 127.0);
    modsrcs[smsrc_pitch - smsrc_pernote_offset] = v->pitch * (1.0 / 100.0);
    modsrcs[smsrc_polyaft - smsrc_pernote_offset] = 0; // XXXKF not supported yet
    modsrcs[smsrc_pitchenv - smsrc_pernote_offset] = cbox_envelope_get_next(&v->pitch_env, v->released) * 0.01f;
    modsrcs[smsrc_filenv - smsrc_pernote_offset] = cbox_envelope_get_next(&v->filter_env, v->released) * 0.01f;
    modsrcs[smsrc_ampenv - smsrc_pernote_offset] = cbox_envelope_get_next(&v->amp_env, v->released) * 0.01f;

    modsrcs[smsrc_amplfo - smsrc_pernote_offset] = lfo_run(&v->amp_lfo);
    modsrcs[smsrc_fillfo - smsrc_pernote_offset] = lfo_run(&v->filter_lfo);
    modsrcs[smsrc_pitchlfo - smsrc_pernote_offset] = lfo_run(&v->pitch_lfo);
    
    if (v->amp_env.cur_stage < 0)
    {
        if (v->cutoff == -1 || is_tail_finished(v))
        {
            v->mode = spt_inactive;
            return;
        }
    }           
    
    float moddests[smdestcount];
    moddests[smdest_gain] = 0;
    moddests[smdest_pitch] = v->pitch + c->pitchbend;
    moddests[smdest_cutoff] = 0;
    moddests[smdest_resonance] = 0;
    GSList *mod = v->layer->modulations;
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
    double freq = v->base_freq * cent2factor(moddests[smdest_pitch]) ;
    uint64_t freq64 = freq * 65536.0 * 65536.0 / m->module.srate;
    v->delta = freq64 >> 32;
    v->frac_delta = freq64 & 0xFFFFFFFF;
    float gain = modsrcs[smsrc_ampenv - smsrc_pernote_offset] * v->gain * addcc(c, 7) * addcc(c, 11) / (maxv * maxv);
    if (moddests[smdest_gain] != 0.0)
        gain *= dB2gain(moddests[smdest_gain]);
    float pan = (v->layer->pan + 100) * (1.0 / 200.0) + (addcc(c, 10) * 1.0 / maxv - 0.5) * 2;
    if (pan < 0)
        pan = 0;
    if (pan > 1)
        pan = 1;
    v->lgain = gain * (1 - pan)  / 32768.0;
    v->rgain = gain * pan / 32768.0;
    if (v->cutoff != -1)
    {
        float cutoff = v->cutoff * cent2factor(moddests[smdest_cutoff]);
        if (cutoff < 20)
            cutoff = 20;
        if (cutoff > m->module.srate * 0.45)
            cutoff = m->module.srate * 0.45;
        //float resonance = v->resonance*pow(32.0,c->cc[71]/maxv);
        float resonance = v->resonance * dB2gain(moddests[smdest_resonance]);
        if (resonance < 0.7)
            resonance = 0.7;
        if (resonance > 32)
            resonance = 32;
        // XXXKF this is found experimentally and probably far off from correct formula
        if (is_4pole(v))
            resonance = sqrt(resonance / 0.707) * 0.5;
        switch(v->filter)
        {
        case sft_lp12:
            cbox_biquadf_set_lp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_hp12:
            cbox_biquadf_set_hp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_bp6:
            cbox_biquadf_set_bp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_lp6:
            cbox_biquadf_set_1plp(&v->filter_coeffs, cutoff, m->module.srate);
            break;
        case sft_hp6:
            cbox_biquadf_set_1php(&v->filter_coeffs, cutoff, m->module.srate);
            break;
        case sft_lp24:
            cbox_biquadf_set_lp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_hp24:
            cbox_biquadf_set_hp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        case sft_bp12:
            cbox_biquadf_set_bp_rbj(&v->filter_coeffs, cutoff, resonance, m->module.srate);
            break;
        default:
            assert(0);
        }
    }
    
    float left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
    float *tmp_outputs[2] = {left, right};
    uint32_t samples = 0;
    samples = sampler_voice_sample_playback(v, tmp_outputs);
    for (int i = samples; i < CBOX_BLOCK_SIZE; i++)
        left[i] = right[i] = 0.f;
    if (v->cutoff != -1)
    {
        cbox_biquadf_process(&v->filter_left, &v->filter_coeffs, left);
        if (is_4pole(v))
            cbox_biquadf_process(&v->filter_left2, &v->filter_coeffs, left);
        cbox_biquadf_process(&v->filter_right, &v->filter_coeffs, right);
        if (is_4pole(v))
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
    
    v->last_lgain = v->lgain;
    v->last_rgain = v->rgain;
}

void sampler_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct sampler_module *m = (struct sampler_module *)module;
    
    //float channels[2][CBOX_BLOCK_SIZE];
    
    for (int c = 0; c < m->output_pairs + m->aux_pairs; c++)
    {
        int oo = 2 * c;
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
            outputs[oo][i] = outputs[oo + 1][i] = 0.f;
    }
    
    int vcount = 0, vrel = 0;
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        
        if (v->mode != spt_inactive)
        {
            sampler_voice_process(v, m, outputs);

            if (v->amp_env.cur_stage == 15)
                vrel++;
            vcount++;
        }
    }
    m->active_voices = vcount;
    if (vcount - vrel > m->max_voices)
        sampler_steal_voice(m);
    m->serial_no++;
}

void sampler_process_cc(struct sampler_module *m, struct sampler_channel *c, int cc, int val)
{
    int was_enabled = c->cc[cc] >= 64;
    int enabled = val >= 64;
    switch(cc)
    {
        case 64:
            if (was_enabled && !enabled)
            {
                sampler_stop_sustained(m, c);
            }
            break;
        case 66:
            if (was_enabled && !enabled)
                sampler_stop_sostenuto(m, c);
            else if (!was_enabled && enabled)
                sampler_capture_sostenuto(m, c);
            break;
        
        case 120:
        case 123:
            sampler_stop_all(m, c);
            break;
        case 121:
            // Recommended Practice (RP-015) Response to Reset All Controllers
            // http://www.midi.org/techspecs/rp15.php
            sampler_process_cc(m, c, 64, 0);
            sampler_process_cc(m, c, 66, 0);
            c->cc[11] = 127;
            c->cc[1] = 0;
            c->pitchbend = 0;
            c->cc[smsrc_chanaft] = 0;
            // XXXKF reset polyphonic pressure values when supported
            return;
    }
    if (cc < 120)
        c->cc[cc] = val;
}

void sampler_program_change(struct sampler_module *m, struct sampler_channel *c, int program)
{
    if (c->program)
        c->program->in_use--;
    // XXXKF replace with something more efficient
    for (int i = 0; i < m->program_count; i++)
    {
        // XXXKF support banks
        if (m->programs[i]->prog_no == program)
        {
            c->program = m->programs[i];
            return;
        }
    }
    g_warning("Unknown program %d", program);
    c->program = m->programs[0];
    c->program->in_use++;
}

void sampler_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct sampler_module *m = (struct sampler_module *)module;
    if (len > 0)
    {
        int cmd = data[0] >> 4;
        int chn = data[0] & 15;
        struct sampler_channel *c = &m->channels[chn];
        switch(cmd)
        {
            case 8:
                sampler_stop_note(m, c, data[1], data[2], FALSE);
                break;

            case 9:
                if (data[2] > 0)
                    sampler_start_note(m, c, data[1], data[2]);
                else
                    sampler_stop_note(m, c, data[1], data[2], FALSE);
                break;
            
            case 10:
                // handle chokeable one shot layers
                if (data[2] == 127)
                    sampler_stop_note(m, c, data[1], data[2], TRUE);
                // polyphonic pressure not handled
                break;
            
            case 11:
                sampler_process_cc(m, c, data[1], data[2]);
                break;

            case 12:
                sampler_program_change(m, c, data[1]);
                break;

            case 13:
                c->cc[smsrc_chanaft] = data[1];
                break;

            case 14:
                c->pitchbend = (data[1] + 128 * data[2] - 8192) * c->pbrange / 8192;
                break;

            }
    }
}

static void init_channel(struct sampler_module *m, struct sampler_channel *c)
{
    c->module = m;
    c->pitchbend = 0;
    c->pbrange = 200; // cents
    memset(c->cc, 0, sizeof(c->cc));
    c->cc[7] = 100;
    c->cc[10] = 64;
    c->cc[11] = 127;
    c->cc[71] = 64;
    c->cc[74] = 64;
    c->previous_note = -1;
    c->program = m->program_count ? m->programs[0] : NULL;
    if (c->program)
        c->program->in_use++;
    memset(c->switchmask, 0, sizeof(c->switchmask));
}

static gboolean sampler_program_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct sampler_program *program = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!(CBOX_OBJECT_DEFAULT_STATUS(program, fb, error)))
            return FALSE;
        return TRUE;
    }
    if (!strcmp(cmd->command, "/regions") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (GSList *p = program->layers; p; p = g_slist_next(p))
        {
            if (!cbox_execute_on(fb, NULL, "/region", "o", error, p->data))
                return FALSE;
        }
        return TRUE;
    }
    else // otherwise, treat just like an command on normal (non-aux) output
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    
}

static struct sampler_program *sampler_program_new(struct sampler_module *m, int prog_no, const char *name, const char *sample_dir)
{
    struct cbox_document *doc = CBOX_GET_DOCUMENT(&m->module);
    struct sampler_program *prg = malloc(sizeof(struct sampler_program));
    memset(prg, 0, sizeof(*prg));
    CBOX_OBJECT_HEADER_INIT(prg, sampler_program, doc);
    cbox_command_target_init(&prg->cmd_target, sampler_program_process_cmd, prg);
    
    prg->prog_no = prog_no;
    prg->name = g_strdup(name);
    prg->sample_dir = g_strdup(sample_dir);
    prg->source_file = NULL;
    prg->layers = NULL;
    CBOX_OBJECT_REGISTER(prg);
    return prg;
}

static struct sampler_program *load_program(struct sampler_module *m, const char *cfg_section, const char *name, int pgm_id, GError **error)
{
    int i;

    g_clear_error(error);
    
    char *name2 = cbox_config_get_string(cfg_section, "name");

    char *sfz_path = cbox_config_get_string(cfg_section, "sfz_path");
    char *spath = cbox_config_get_string(cfg_section, "sample_path");

    struct sampler_program *prg = sampler_program_new(
        m,
        pgm_id != -1 ? pgm_id : cbox_config_get_int(cfg_section, "program", 0),
        name2 ? name2 : name,
        spath ? spath : (sfz_path ? sfz_path : "")
    );
    
    char *sfz = cbox_config_get_string(cfg_section, "sfz");
    if (sfz)
    {
        if (sfz_path)
            prg->source_file = g_build_filename(sfz_path, sfz, NULL);
        else
            prg->source_file = g_strdup(sfz);

        if (sampler_module_load_program_sfz(m, prg, prg->source_file, FALSE, error))
            return prg;
        CBOX_DELETE(prg);
        return NULL;
    }
    
    for (i = 0; ; i++)
    {
        char *where = NULL;
        gchar *s = g_strdup_printf("layer%d", 1 + i);
        const char *layer_section = cbox_config_get_string(cfg_section, s);
        g_free(s);
        if (!layer_section)
            break;
        where = g_strdup_printf("slayer:%s", layer_section);
        
        prg->source_file = g_strdup_printf("config:%s", cfg_section);
        struct sampler_layer *l = sampler_layer_new_from_section(m, prg, where);
        if (!l)
            g_warning("Sample layer '%s' cannot be created - skipping", layer_section);
        else if (!l->waveform)
            g_warning("Sample layer '%s' does not have a waveform - skipping", layer_section);
        else
            prg->layers = g_slist_prepend(prg->layers, l);
        g_free(where);
    }
    prg->layers = g_slist_reverse(prg->layers);
    return prg;
}

static int get_first_free_program_no(struct sampler_module *m)
{
    int prog_no = -1;
    gboolean found;
    
    // XXXKF this has a N-squared complexity - but I'm not seeing
    // this being used with more than 10 programs at the same time
    // in the near future
    do {
        prog_no++;
        found = FALSE;
        for (int i = 0; i < m->program_count; i++)
        {
            if (m->programs[i]->prog_no == prog_no)
            {
                found = TRUE;
                break;
            }
        }        
    } while(found);
    
    return prog_no;
}

static int find_program(struct sampler_module *m, int prog_no)
{
    for (int i = 0; i < m->program_count; i++)
    {
        if (m->programs[i]->prog_no == prog_no)
            return i;
    }
    return -1;
}

struct release_program_voices_data
{
    struct sampler_module *module;
    
    struct sampler_program *old_pgm, *new_pgm;
};

static int release_program_voices_execute(void *data)
{
    struct release_program_voices_data *rpv = data;
    struct sampler_module *m = rpv->module;
    int finished = 1;
    
    for (int i = 0; i < 16; i++)
    {
        if (m->channels[i].program == rpv->old_pgm || m->channels[i].program == NULL)
            m->channels[i].program = rpv->new_pgm;
    }
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        
        if (v->mode != spt_inactive)
        {
            if (v->program == rpv->old_pgm)
            {
                finished = 0;
                if (v->amp_env.cur_stage != 15)
                {
                    v->released = 1;
                    cbox_envelope_go_to(&v->amp_env, 15);
                }
            }
        }
    }
    
    return finished;
}

void swap_program(struct sampler_module *m, int index, struct sampler_program *pgm)
{
    static struct cbox_rt_cmd_definition release_program_voices = { NULL, release_program_voices_execute, NULL };
    
    struct sampler_program *old_program = cbox_rt_swap_pointers(m->module.rt, (void **)&m->programs[index], pgm);

    struct release_program_voices_data data = {m, old_program, pgm};

    cbox_rt_execute_cmd_sync(m->module.rt, &release_program_voices, &data);
    
    CBOX_DELETE(old_program);
}

static gboolean load_program_at(struct sampler_module *m, const char *cfg_section, const char *name, int prog_no, GError **error)
{
    struct sampler_program *pgm = NULL;
    int index = find_program(m, prog_no);
    pgm = load_program(m, cfg_section, name, prog_no, error);
    if (!pgm)
        return FALSE;
    
    if (index != -1)
    {
        swap_program(m, index, pgm);
        return TRUE;
    }
    
    struct sampler_program **programs = malloc(sizeof(struct sampler_program *) * (m->program_count + 1));
    memcpy(programs, m->programs, sizeof(struct sampler_program *) * m->program_count);
    programs[m->program_count] = pgm;
    free(cbox_rt_swap_pointers_and_update_count(m->module.rt, (void **)&m->programs, programs, &m->program_count, m->program_count + 1));    
    return TRUE;
}

static gboolean load_from_string(struct sampler_module *m, const char *sample_dir, const char *sfz_data, const char *name, int prog_no, GError **error)
{
    int index = find_program(m, prog_no);
    struct sampler_program *pgm = sampler_program_new(m, prog_no, name, sample_dir);
    pgm->source_file = g_strdup("string");
    if (!sampler_module_load_program_sfz(m, pgm, sfz_data, TRUE, error))
    {
        free(pgm);
        return FALSE;
    }

    if (index != -1)
    {
        swap_program(m, index, pgm);
        return TRUE;
    }
    
    struct sampler_program **programs = malloc(sizeof(struct sampler_program *) * (m->program_count + 1));
    memcpy(programs, m->programs, sizeof(struct sampler_program *) * m->program_count);
    programs[m->program_count] = pgm;
    free(cbox_rt_swap_pointers_and_update_count(m->module.rt, (void **)&m->programs, programs, &m->program_count, m->program_count + 1));    
    return TRUE;
}

void sampler_program_destroyfunc(struct cbox_objhdr *hdr_ptr)
{
    struct sampler_program *prg = CBOX_H2O(hdr_ptr);
    for (GSList *p = prg->layers; p; p = g_slist_next(p))
        CBOX_DELETE((struct sampler_layer *)p->data);

    g_free(prg->name);
    g_free(prg->sample_dir);
    g_free(prg->source_file);
    g_slist_free(prg->layers);
    free(prg);
}

gboolean sampler_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct sampler_module *m = (struct sampler_module *)ct->user_data;
    
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (int i = 0; i < 16; i++)
        {
            struct sampler_channel *channel = &m->channels[i];
            gboolean result;
            if (channel->program)
                result = cbox_execute_on(fb, NULL, "/patch", "iis", error, i + 1, channel->program->prog_no, channel->program->name);
            else
                result = cbox_execute_on(fb, NULL, "/patch", "iis", error, i + 1, -1, "");
            if (!result)
                return FALSE;
            if (!(cbox_execute_on(fb, NULL, "/volume", "ii", error, i + 1, addcc(channel, 7)) &&
                cbox_execute_on(fb, NULL, "/pan", "ii", error, i + 1, addcc(channel, 10))))
                return FALSE;
        }
        
        return cbox_execute_on(fb, NULL, "/active_voices", "i", error, m->active_voices) &&
            cbox_execute_on(fb, NULL, "/polyphony", "i", error, MAX_SAMPLER_VOICES) && 
            CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error);
    }
    else
    if (!strcmp(cmd->command, "/patches") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (int i = 0; i < m->program_count; i++)
        {
            struct sampler_program *prog = m->programs[i];
            gboolean result;
            if (!cbox_execute_on(fb, NULL, "/patch", "isoi", error, prog->prog_no, prog->name, prog, prog->in_use))
                return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/polyphony") && !strcmp(cmd->arg_types, "i"))
    {
        int polyphony = CBOX_ARG_I(cmd, 0);
        if (polyphony < 1 || polyphony > MAX_SAMPLER_VOICES)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid polyphony %d (must be between 1 and %d)", polyphony, (int)MAX_SAMPLER_VOICES);
            return FALSE;
        }
        m->max_voices = polyphony;
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/set_patch") && !strcmp(cmd->arg_types, "ii"))
    {
        int channel = CBOX_ARG_I(cmd, 0);
        if (channel < 1 || channel > 16)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid channel %d", channel);
            return FALSE;
        }
        int value = CBOX_ARG_I(cmd, 1);
        struct sampler_program *pgm = NULL;
        for (int i = 0; i < m->program_count; i++)
        {
            if (m->programs[i]->prog_no == value)
            {
                pgm = m->programs[i];
                break;
            }
        }
        cbox_rt_swap_pointers(m->module.rt, (void **)&m->channels[channel - 1].program, pgm);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load_patch") && !strcmp(cmd->arg_types, "iss"))
    {
        return load_program_at(m, CBOX_ARG_S(cmd, 1), CBOX_ARG_S(cmd, 2), CBOX_ARG_I(cmd, 0), error);
    }
    else if (!strcmp(cmd->command, "/load_patch_from_string") && !strcmp(cmd->arg_types, "isss"))
    {
        return load_from_string(m, CBOX_ARG_S(cmd, 1), CBOX_ARG_S(cmd, 2), CBOX_ARG_S(cmd, 3), CBOX_ARG_I(cmd, 0), error);
    }
    else if (!strcmp(cmd->command, "/get_unused_program") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/program_no", "i", error, get_first_free_program_no(m));
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

MODULE_CREATE_FUNCTION(sampler)
{
    int result = 0;
    int i;
    static int inited = 0;
    if (!inited)
    {
        for (int i = 0; i < 2049; i++)
            sine_wave[i] = sin(i * M_PI / 1024.0);
        inited = 1;
    }
    
    int max_voices = cbox_config_get_int(cfg_section, "polyphony", MAX_SAMPLER_VOICES);
    if (max_voices < 1 || max_voices > MAX_SAMPLER_VOICES)
    {
        g_set_error(error, CBOX_SAMPLER_ERROR, CBOX_SAMPLER_ERROR_INVALID_LAYER, "%s: invalid polyphony value", cfg_section);
        return NULL;
    }
    int output_pairs = cbox_config_get_int(cfg_section, "output_pairs", 1);
    if (output_pairs < 1 || output_pairs > 16)
    {
        g_set_error(error, CBOX_SAMPLER_ERROR, CBOX_SAMPLER_ERROR_INVALID_LAYER, "%s: invalid output pairs value", cfg_section);
        return NULL;
    }
    int aux_pairs = cbox_config_get_int(cfg_section, "aux_pairs", 0);
    if (aux_pairs < 0 || aux_pairs > 4)
    {
        g_set_error(error, CBOX_SAMPLER_ERROR, CBOX_SAMPLER_ERROR_INVALID_LAYER, "%s: invalid aux pairs value", cfg_section);
        return NULL;
    }
    
    struct sampler_module *m = malloc(sizeof(struct sampler_module));
    CALL_MODULE_INIT(m, 0, (output_pairs + aux_pairs) * 2, sampler);
    m->output_pairs = output_pairs;
    m->aux_pairs = aux_pairs;
    m->module.aux_offset = m->output_pairs * 2;
    m->module.process_event = sampler_process_event;
    m->module.process_block = sampler_process_block;
    m->programs = NULL;
    m->max_voices = max_voices;
    m->serial_no = 0;
            
    for (i = 0; ; i++)
    {
        gchar *s = g_strdup_printf("program%d", i);
        char *p = cbox_config_get_string(cfg_section, s);
        g_free(s);
        
        if (!p)
        {
            m->program_count = i;
            break;
        }
    }
    m->programs = malloc(sizeof(struct sampler_program *) * m->program_count);
    int success = 1;
    for (i = 0; i < m->program_count; i++)
    {
        gchar *s = g_strdup_printf("program%d", i);
        char *pgm_section = NULL;
        int pgm_id = -1;
        const char *pgm_name = cbox_config_get_string(cfg_section, s);
        g_free(s);
        char *at = strchr(pgm_name, '@');
        if (at)
        {
            pgm_id = atoi(at + 1);
            s = g_strndup(pgm_name, at - pgm_name);
            pgm_section = g_strdup_printf("spgm:%s", s);
            g_free(s);
        }
        else
        {
            pgm_id = i;
            pgm_section = g_strdup_printf("spgm:%s", pgm_name);
        }
        
        m->programs[i] = load_program(m, pgm_section, pgm_section + 5, pgm_id, error);
        g_free(pgm_section);
        if (!m->programs[i])
        {
            success = 0;
            break;
        }
    }
    if (!success)
    {
        // XXXKF free programs/layers, first ensuring that they're fully initialised
        free(m);
        return NULL;
    }
    
    for (i = 0; i < MAX_SAMPLER_VOICES; i++)
        m->voices[i].mode = spt_inactive;
    m->active_voices = 0;
    
    for (i = 0; i < 16; i++)
        init_channel(m, &m->channels[i]);

    return &m->module;
}

void sampler_destroyfunc(struct cbox_module *module)
{
    struct sampler_module *m = (struct sampler_module *)module;
    
    for (int i = 0; i < m->program_count; i++)
        CBOX_DELETE(m->programs[i]);
    free(m->programs);
}

#define MAKE_TO_STRING_CONTENT(name, v) \
    case v: return name;

#define MAKE_FROM_STRING_CONTENT(n, v) \
    if (!strcmp(name, n)) { *value = v; return TRUE; }

#define MAKE_FROM_TO_STRING(enumtype) \
    const char *enumtype##_to_string(enum enumtype value) \
    { \
        switch(value) { \
            ENUM_VALUES_##enumtype(MAKE_TO_STRING_CONTENT) \
            default: return NULL; \
        } \
    } \
    \
    gboolean enumtype##_from_string(const char *name, enum enumtype *value) \
    { \
        ENUM_VALUES_##enumtype(MAKE_FROM_STRING_CONTENT) \
        return FALSE; \
    }

ENUM_LIST(MAKE_FROM_TO_STRING)

//////////////////////////////////////////////////////////////////////////
// Note initialisation functions

void sampler_nif_vel2pitch(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->pitch += nif->param * v->vel * (1.0 / 127.0);
}

void sampler_nif_cc2delay(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->delay += nif->param * v->channel->cc[nif->variant] / 127.0 * v->channel->module->module.srate;
}

void sampler_nif_addrandom(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    float rnd = rand() * 1.0 / RAND_MAX;
    switch(nif->variant)
    {
        case 0:
            v->gain *= dB2gain(rnd * nif->param);
            break;
        case 1:
            v->cutoff *= cent2factor(rnd * nif->param);
            break;
        case 2:
            v->pitch += rnd * nif->param; // this is in cents
            break;
    }
}

void sampler_nif_vel2env(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    int env_type = (nif->variant) >> 4;
    struct cbox_envelope *env = NULL;
    switch(env_type)
    {
        case 0:
            env = &v->amp_env;
            break;
        case 1:
            env = &v->filter_env;
            break;
        case 2:
            env = &v->pitch_env;
            break;
        default:
            assert(0);
    }
    if (env->shape != &v->dyn_envs[env_type])
    {
        memcpy(&v->dyn_envs[env_type], env->shape, sizeof(struct cbox_envelope_shape));
        env->shape = &v->dyn_envs[env_type];
    }
    float param = nif->param * v->vel * (1.0 / 127.0);
    if ((nif->variant & 15) == 4)
        param *= 0.01;
    cbox_envelope_modify_dahdsr(env->shape, nif->variant & 15, param, v->channel->module->module.srate / CBOX_BLOCK_SIZE);
}

//////////////////////////////////////////////////////////////////////////

struct cbox_module_livecontroller_metadata sampler_controllers[] = {
};

struct cbox_module_keyrange_metadata sampler_keyranges[] = {
};

DEFINE_MODULE(sampler, 0, 2)

