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

void sampler_channel_init(struct sampler_channel *c, struct sampler_module *m)
{
    c->module = m;
    c->voices_running = NULL;
    c->active_voices = 0;
    c->pitchwheel = 0;
    c->output_shift = 0;
    memset(c->cc, 0, sizeof(c->cc));
    memset(c->poly_pressure, 0, sizeof(c->poly_pressure));
    
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
    c->cc[11] = 127;
    c->cc[71] = 64;
    c->cc[74] = 64;
    c->previous_note = -1;
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
        if (!(rll->cc_trigger_bitmask[cc >> 5] & (1 << (cc & 31))))
            return;
        int old_value = c->cc[cc];
        for (GSList *p = rll->layers_oncc; p; p = p->next)
        {
            struct sampler_layer *layer = p->data;
            assert(layer->runtime);
            // Only trigger on transition between 'out of range' and 'in range' values.
            // XXXKF I'm not sure if it's what is expected here, but don't have
            // the reference implementation handy.
            if (layer->runtime->on_cc_number == cc && 
                (val >= layer->runtime->on_locc && val <= layer->runtime->on_hicc) &&
                !(old_value >= layer->runtime->on_locc && old_value <= layer->runtime->on_hicc))
            {
                struct sampler_voice *v = m->voices_free;
                if (!v)
                    break;
                int exgroups[MAX_RELEASED_GROUPS], exgroupcount = 0;
                sampler_voice_start(v, c, layer->runtime, layer->runtime->pitch_keycenter, 127, exgroups, &exgroupcount);
                sampler_channel_release_groups(c, -1, exgroups, exgroupcount);
            }
        }        
    }
    int was_enabled = c->cc[cc] >= 64;
    int enabled = val >= 64;
    switch(cc)
    {
        case 10:
        case 10 + 32:
            c->cc[cc] = val;
            if (!c->module->disable_mixer_controls)
                c->channel_pan_cc = sampler_channel_addcc(c, 10);
            break;
        case 7:
        case 7 + 32:
            c->cc[cc] = val;
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
            c->cc[11] = 127;
            c->cc[1] = 0;
            c->pitchwheel = 0;
            c->cc[smsrc_chanaft] = 0;
            // XXXKF implement lazy clearing by groups of 4 or something
            memset(c->poly_pressure, 0, sizeof(c->poly_pressure));
            return;
    }
    if (cc < 120)
        c->cc[cc] = val;
}

void sampler_channel_release_groups(struct sampler_channel *c, int note, int exgroups[MAX_RELEASED_GROUPS], int exgroupcount)
{
    if (exgroupcount)
    {
        FOREACH_VOICE(c->voices_running, v)
        {
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

void sampler_channel_start_note(struct sampler_channel *c, int note, int vel, gboolean is_release_trigger)
{
    struct sampler_module *m = c->module;
    float random = rand() * 1.0 / (RAND_MAX + 1.0);
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
    GSList *next_layer = sampler_program_get_next_layer(prg, c, !is_release_trigger ? prg->rll->layers : prg->rll->layers_release, note, vel, random, is_first);
    if (!next_layer)
    {
        if (!is_release_trigger)
            c->previous_note = note;
        return;
    }
    
    // this might perhaps be optimized by mapping the group identifiers to flat-array indexes
    // but I'm not going to do that until someone gives me an SFZ worth doing that work ;)
    int exgroups[MAX_RELEASED_GROUPS], exgroupcount = 0;
    
    FOREACH_VOICE(m->voices_free, v)
    {
        struct sampler_layer *l = next_layer->data;
        // Maybe someone forgot to call sampler_update_layer?
        assert(l->runtime);
        sampler_voice_start(v, c, l->runtime, note, vel, exgroups, &exgroupcount);
        next_layer = sampler_program_get_next_layer(prg, c, g_slist_next(next_layer), note, vel, random, is_first);
        if (!next_layer)
            break;
    }
    if (!is_release_trigger)
        c->previous_note = note;
    sampler_channel_release_groups(c, note, exgroups, exgroupcount);
}

void sampler_channel_start_release_triggered_voices(struct sampler_channel *c, int note)
{
    if (c->program && c->program->rll && c->program->rll->layers_release)
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
    FOREACH_VOICE(c->voices_running, v)
    {
        if (v->note == note && v->layer->trigger != stm_release)
        {
            if (v->captured_sostenuto)
                v->released_with_sostenuto = 1;
            else if (c->cc[64] >= 64)
                v->released_with_sustain = 1;
            else
                sampler_voice_release(v, is_polyaft);
                
        }
    }
    if (c->cc[64] < 64)
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
    if (c->program && c->program->rll && c->program->rll->layers_release)
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
    if (c->program && c->program->rll && c->program->rll->layers_release)
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
    if (c->program)
        c->program->in_use--;    
    c->program = prg;
    if (prg)
    {
        for(GSList *p = prg->ctrl_init_list; p; p = p->next)
        {
            union sampler_ctrlinit_union u;
            u.ptr = p->data;
            // printf("Setting controller %d -> %d\n", u.cinit.controller, u.cinit.value);
            c->cc[u.cinit.controller] = u.cinit.value;
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
    sampler_channel_set_program_RT(c, m->programs[0]);
}

