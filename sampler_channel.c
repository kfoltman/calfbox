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
#include "engine.h"
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

static inline void set_cc_int(struct sampler_channel *c, uint32_t cc, uint8_t value)
{
    c->intcc[cc] = value;
    c->floatcc[cc] = value * (1.0f / 127.f);
}

static inline void set_cc_float(struct sampler_channel *c, uint32_t cc, float value)
{
    c->intcc[cc] = (uint8_t)(value * 127) & 127;
    c->floatcc[cc] = value;
}

void sampler_channel_reset_keyswitches(struct sampler_channel *c)
{
    if (c->program && c->program->rll)
    {
        memset(c->keyswitch_state, 255, sizeof(c->keyswitch_state));
        for (uint32_t i = 0; i < c->program->rll->keyswitch_group_count; ++i)
        {
            const struct sampler_keyswitch_group *ksg = c->program->rll->keyswitch_groups[i];
            if (ksg->def_value == 255)
                c->keyswitch_state[i] = ksg->key_offsets[0];
            else
                c->keyswitch_state[i] = ksg->key_offsets[ksg->def_value];
        }
    }
}

void sampler_channel_init(struct sampler_channel *c, struct sampler_module *m)
{
    c->module = m;
    c->voices_running = NULL;
    c->active_voices = 0;
    c->active_prevoices = 0;
    c->pitchwheel = 0;
    c->output_shift = 0;
    for (int i = 0; i < smsrc_perchan_count; ++i)
    {
        c->intcc[i] = 0;
        c->floatcc[i] = 0;
    }
    c->poly_pressure_mask = 0;
    
    // default to maximum and pan=centre if MIDI mixing disabled
    if (m->disable_mixer_controls)
    {
        c->channel_volume_cc = 16383;
        c->channel_pan_cc = 8192;
    }
    else
    {
        sampler_channel_process_cc(c, 7, 100);
        sampler_channel_process_cc(c, 7 + 32, 0);
        sampler_channel_process_cc(c, 10, 64);
        sampler_channel_process_cc(c, 10 + 32, 0);
    }
    set_cc_int(c, 11, 127);
    set_cc_int(c, 71, 64);
    set_cc_int(c, 74, 64);
    set_cc_float(c, smsrc_alternate, 0);
    c->previous_note = -1;
    c->last_polyaft = 0;
    c->first_note_vel = 100;
    c->program = NULL;
    sampler_channel_set_program_RT(c, m->program_count ? m->programs[0] : NULL);
    memset(c->switchmask, 0, sizeof(c->switchmask));
    memset(c->sustainmask, 0, sizeof(c->sustainmask));
    memset(c->sostenutomask, 0, sizeof(c->sostenutomask));
}

void sampler_channel_process_cc(struct sampler_channel *c, int cc, int val)
{
    struct sampler_module *m = c->module;
    // Handle CC triggering.
    if (c->program && c->program->rll && c->program->rll->layers_oncc)
    {
        struct sampler_rll *rll = c->program->rll;
        if ((rll->cc_trigger_bitmask[cc >> 5] & (1 << (cc & 31))))
        {
            int old_value = c->intcc[cc];
            for (GSList *p = rll->layers_oncc; p; p = p->next)
            {
                struct sampler_layer *layer = p->data;
                assert(layer->runtime);
                // Default (compatible) behaviour means the region will trigger
                // on every CC that has value within the specified range.
                // XXXKF add a switch for only triggering the region on
                // transition between in-range and out-of-range.
                gboolean compatible_oncc_behaviour = TRUE;

                if (layer->runtime->on_cc.cc_number == cc &&
                    (val >= layer->runtime->on_cc.locc && val <= layer->runtime->on_cc.hicc) &&
                    (compatible_oncc_behaviour || !(old_value >= layer->runtime->on_cc.locc && old_value <= layer->runtime->on_cc.hicc)))
                {
                    struct sampler_voice *v = m->voices_free;
                    if (!v)
                        break;
                    struct sampler_released_groups exgroups;
                    sampler_released_groups_init(&exgroups);
                    sampler_voice_start(v, c, layer->runtime, layer->runtime->pitch_keycenter, 127, &exgroups);
                    sampler_channel_release_groups(c, -1, &exgroups);
                }
            }
        }
    }
    int was_enabled = c->intcc[cc] >= 64;
    int enabled = val >= 64;
    switch(cc)
    {
        case 10:
        case 10 + 32:
            set_cc_int(c, cc, val);
            if (!c->module->disable_mixer_controls)
                c->channel_pan_cc = sampler_channel_addcc(c, 10);
            break;
        case 7:
        case 7 + 32:
            set_cc_int(c, cc, val);
            if (!c->module->disable_mixer_controls)
                c->channel_volume_cc = sampler_channel_addcc(c, 7);
            break;
        case 64:
            if (was_enabled && !enabled)
            {
                sampler_channel_stop_sustained(c);
            }
            break;
        case 66:
            if (was_enabled && !enabled)
                sampler_channel_stop_sostenuto(c);
            else if (!was_enabled && enabled)
                sampler_channel_capture_sostenuto(c);
            break;
        
        case 120:
        case 123:
            sampler_channel_stop_all(c);
            break;
        case 121:
            // Recommended Practice (RP-015) Response to Reset All Controllers
            // http://www.midi.org/techspecs/rp15.php
            sampler_channel_process_cc(c, 64, 0);
            sampler_channel_process_cc(c, 66, 0);
            set_cc_int(c, 11, 127);
            set_cc_int(c, 1, 0);
            set_cc_float(c, smsrc_alternate, 0);
            c->pitchwheel = 0;
            c->last_chanaft = 0;
            c->poly_pressure_mask = 0;
            c->last_polyaft = 0;
            sampler_channel_reset_keyswitches(c);
            return;
    }
    if (cc < smsrc_perchan_count)
        set_cc_int(c, cc, val);
}

void sampler_channel_release_groups(struct sampler_channel *c, int note, struct sampler_released_groups *exgroups)
{
    if (exgroups->group_count || exgroups->low_groups)
    {
        FOREACH_VOICE(c->voices_running, v)
        {
            if (v->off_by && v->note != note)
            {
                if (sampler_released_groups_check(exgroups, v->off_by))
                {
                    v->released = 1;
                    if (v->layer->off_mode == som_fast)
                        cbox_envelope_go_to(&v->amp_env, 15);
                }
            }
        }
    }
}

void sampler_channel_start_note(struct sampler_channel *c, int note, int vel, gboolean is_release_trigger)
{
    struct sampler_module *m = c->module;
    float random = rand() * 1.0 / (RAND_MAX + 1.0);

    set_cc_float(c, smsrc_alternate, c->intcc[smsrc_alternate] ? 0.f : 1.f);
    set_cc_float(c, smsrc_random_unipolar, random); // is that a per-voice or per-channel value? is it generated per region or per note?

    gboolean is_first = FALSE;
    if (!is_release_trigger)
    {
        c->switchmask[note >> 5] |= 1 << (note & 31);
        c->prev_note_velocity[note] = vel;
        c->prev_note_start_time[note] = m->current_time;
        is_first = TRUE;
        FOREACH_VOICE(c->voices_running, v)
        {
            if (!v->released && v->layer->trigger != stm_release)
            {
                is_first = FALSE;
                break;
            }
        }
    }
    struct sampler_program *prg = c->program;
    if (!prg || !prg->rll || prg->deleting)
        return;

    if (!is_release_trigger)
    {
        for (uint32_t i = 0; i < prg->rll->keyswitch_group_count; ++i)
        {
            const struct sampler_keyswitch_group *ks = prg->rll->keyswitch_groups[i];
            if (note >= ks->lo && note <= ks->hi)
                c->keyswitch_state[i] = ks->key_offsets[note - ks->lo];
        }
    }

    struct sampler_rll_iterator iter;
    sampler_rll_iterator_init(&iter, prg->rll, c, note, vel, random, is_first, is_release_trigger);

    struct sampler_layer *layer = sampler_rll_iterator_next(&iter);
    if (!layer)
    {
        if (!is_release_trigger)
            c->previous_note = note;
        return;
    }
    struct sampler_layer_data *layers[MAX_SAMPLER_VOICES];
    struct sampler_layer_data *delayed_layers[MAX_SAMPLER_PREVOICES];
    int lcount = 0, dlcount = 0;
    struct sampler_voice *free_voice = m->voices_free;
    struct sampler_prevoice *free_prevoice = m->prevoices_free;
    int fvcount = 0, fpcount = 0;
    
    while(layer && lcount < MAX_SAMPLER_VOICES + MAX_SAMPLER_PREVOICES)
    {
        if (free_voice)
        {
            free_voice = free_voice->next;
            fvcount++;
        }
        if (free_prevoice)
        {
            free_prevoice = free_prevoice->next;
            fpcount++;
        }
        assert(layer->runtime);
        if (layer->runtime->use_prevoice)
            delayed_layers[dlcount++] = layer->runtime;
        else
            layers[lcount++] = layer->runtime;
        layer = sampler_rll_iterator_next(&iter);
    }

    struct sampler_released_groups exgroups;
    sampler_released_groups_init(&exgroups);
    // If running out of polyphony, do not start the note if all the regions cannot be played
    if (lcount <= fvcount && dlcount <= fpcount)
    {
        // this might perhaps be optimized by mapping the group identifiers to flat-array indexes
        // but I'm not going to do that until someone gives me an SFZ worth doing that work ;)

        for (int i = 0; i < lcount; ++i)
        {
            struct sampler_layer_data *l = layers[i];
            int velc = (!is_first && l->vel_mode == svm_previous) ? c->first_note_vel : vel;
            sampler_voice_start(m->voices_free, c, l, note, velc, &exgroups);
        }
        for (int i = 0; i < dlcount; ++i)
        {
            struct sampler_layer_data *l = delayed_layers[i];
            int velc = (!is_first && l->vel_mode == svm_previous) ? c->first_note_vel : vel;
            sampler_prevoice_start(m->prevoices_free, c, l, note, velc);
        }
    }
    if (!is_release_trigger)
        c->previous_note = note;
    if (is_first)
        c->first_note_vel = vel;
    sampler_channel_release_groups(c, note, &exgroups);
}

void sampler_channel_start_release_triggered_voices(struct sampler_channel *c, int note)
{
    if (c->program && c->program->rll && c->program->rll->has_release_layers)
    {
        if (c->prev_note_velocity[note])
        {
            sampler_channel_start_note(c, note, c->prev_note_velocity[note], TRUE);
            c->prev_note_velocity[note] = 0;
        }
    }    
}

void sampler_channel_stop_note(struct sampler_channel *c, int note, int vel, gboolean is_polyaft)
{
    c->switchmask[note >> 5] &= ~(1 << (note & 31));
    FOREACH_PREVOICE(c->module->prevoices_running, pv)
    {
        if (pv->note == note)
            sampler_prevoice_unlink(&c->module->prevoices_running, pv);
    }
    FOREACH_VOICE(c->voices_running, v)
    {
        if (v->note == note && v->layer->trigger != stm_release)
        {
            v->off_vel = vel;
            if (v->captured_sostenuto)
                v->released_with_sostenuto = 1;
            else if (c->intcc[64] >= 64)
                v->released_with_sustain = 1;
            else
                sampler_voice_release(v, is_polyaft);
                
        }
    }
    if (c->intcc[64] < 64)
        sampler_channel_start_release_triggered_voices(c, note);
    else
        c->sustainmask[note >> 5] |= (1 << (note & 31));
}

void sampler_channel_stop_sustained(struct sampler_channel *c)
{
    FOREACH_VOICE(c->voices_running, v)
    {
        if (v->channel == c && v->released_with_sustain && v->layer->trigger != stm_release)
        {
            sampler_voice_release(v, FALSE);
            v->released_with_sustain = 0;
        }
    }
    // Start release layers for the newly released keys
    if (c->program && c->program->rll && c->program->rll->has_release_layers)
    {
        for (int i = 0; i < 128; i++)
        {
            if (c->sustainmask[i >> 5] & (1 << (i & 31)))
                sampler_channel_start_release_triggered_voices(c, i);
        }
    }
    memset(c->sustainmask, 0, sizeof(c->sustainmask));
}

void sampler_channel_stop_sostenuto(struct sampler_channel *c)
{
    FOREACH_VOICE(c->voices_running, v)
    {
        if (v->released_with_sostenuto && v->layer->trigger != stm_release)
        {
            sampler_channel_start_release_triggered_voices(c, v->note);
            sampler_voice_release(v, FALSE);
            v->released_with_sostenuto = 0;
            // XXXKF unsure what to do with sustain
        }
    }
    // Start release layers for the newly released keys
    if (c->program && c->program->rll && c->program->rll->has_release_layers)
    {
        for (int i = 0; i < 128; i++)
        {
            if (c->sostenutomask[i >> 5] & (1 << (i & 31)))
                sampler_channel_start_release_triggered_voices(c, i);
        }
    }
    memset(c->sostenutomask, 0, sizeof(c->sostenutomask));
}

void sampler_channel_capture_sostenuto(struct sampler_channel *c)
{
    FOREACH_VOICE(c->voices_running, v)
    {
        if (!v->released && v->loop_mode != slm_one_shot && v->loop_mode != slm_one_shot_chokeable && !v->layer->count)
        {
            // XXXKF unsure what to do with sustain
            v->captured_sostenuto = 1;
            c->sostenutomask[v->note >> 5] |= (1 << (v->note & 31));
        }
    }
}

void sampler_channel_stop_all(struct sampler_channel *c)
{
    FOREACH_VOICE(c->voices_running, v)
    {
        sampler_voice_release(v, v->loop_mode == slm_one_shot_chokeable);
        v->released_with_sustain = 0;
        v->released_with_sostenuto = 0;
        v->captured_sostenuto = 0;
    }
}

void sampler_channel_set_program_RT(struct sampler_channel *c, struct sampler_program *prg)
{
    FOREACH_PREVOICE(c->module->prevoices_running, pv)
    {
        if (pv->channel == c)
        {
            sampler_prevoice_unlink(&c->module->prevoices_running, pv);
            sampler_prevoice_link(&c->module->prevoices_free, pv);            
        }
    }
    if (c->program)
        c->program->in_use--;    
    c->program = prg;
    if (prg)
    {
        assert(prg->rll);
        sampler_channel_reset_keyswitches(c);
        for(GSList *p = prg->ctrl_init_list; p; p = p->next)
        {
            union sampler_ctrlinit_union u;
            u.ptr = p->data;
            // printf("Setting controller %d -> %d\n", u.cinit.controller, u.cinit.value);
            set_cc_int(c, u.cinit.controller, u.cinit.value);
        }
        c->program->in_use++;
    }
}

#define sampler_channel_set_program_args(ARG) ARG(struct sampler_program *, prg)

DEFINE_RT_VOID_FUNC(sampler_channel, c, sampler_channel_set_program)
{
    sampler_channel_set_program_RT(c, prg);
}

void sampler_channel_program_change(struct sampler_channel *c, int program)
{
    struct sampler_module *m = c->module;
    // XXXKF replace with something more efficient
    for (uint32_t i = 0; i < m->program_count; i++)
    {
        // XXXKF support banks
        if (m->programs[i]->prog_no == program)
        {
            sampler_channel_set_program_RT(c, m->programs[i]);
            return;
        }
    }
    g_warning("Unknown program %d", program);
    if (m->program_count)
        sampler_channel_set_program_RT(c, m->programs[0]);
}

float sampler_channel_get_expensive_cc(struct sampler_channel *c, struct sampler_voice *v, struct sampler_prevoice *pv, int cc_no)
{
    switch(cc_no)
    {
        case smsrc_pitchbend:
            return c->pitchwheel / 8191.f;
        case smsrc_lastpolyaft: // how this is defined? is it last or is it current voice's?
            return sampler_channel_get_poly_pressure(c, v ? v->note : (pv ? pv->note : 0));
        case smsrc_noteonvel:
            return v ? v->vel / 127.0 : (pv ? pv->vel / 127.0 : 0);
        case smsrc_noteoffvel:
            return v ? v->off_vel / 127.0 : 0;
        case smsrc_keynotenum:
            return v ? v->note / 127.0 : (pv ? pv->note / 127.0 : 0);
        case smsrc_keynotegate:
            return c->switchmask[0] || c->switchmask[1] || c->switchmask[2] || c->switchmask[3]; // XXXKF test interactions with sustain/sostenuto
        case smsrc_chanaft_sfz2:
            return c->last_chanaft / 127.0;
        case smsrc_random_unipolar:
        case smsrc_alternate:
            return c->floatcc[cc_no];
        case smsrc_random_bipolar:
            return -1 + 2 * c->floatcc[smsrc_random_unipolar];
        case smsrc_keydelta: // not supported yet
            return 0;
        case smsrc_keydelta_abs:
            return 0;
        case smsrc_tempo:
            return c->module->module.engine->master->tempo; // XXXKF what scale???
        default:
            assert(0);
            return 0.f;
    }
}
