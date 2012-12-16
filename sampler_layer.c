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
#include <glib.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

void sampler_layer_set_modulation1(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_moddest dest, float amount, int flags)
{
    sampler_layer_set_modulation(l, src, smsrc_none, dest, amount, flags);
}

void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags)
{
    GSList *p = l->modulations;
    while(p)
    {
        struct sampler_modulation *sm = p->data;
        if (sm->src == src && sm->src2 == src2 && sm->dest == dest)
        {
            sm->amount = amount;
            sm->flags = flags;
            return;
        }
        p = g_slist_next(p);
    }
    struct sampler_modulation *sm = g_malloc0(sizeof(struct sampler_modulation));
    sm->src = src;
    sm->src2 = src2;
    sm->dest = dest;
    sm->amount = amount;
    sm->flags = flags;
    l->modulations = g_slist_prepend(l->modulations, sm);
}

void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc, int variant, float param)
{
    struct sampler_noteinitfunc *nif = malloc(sizeof(struct sampler_noteinitfunc));
    nif->notefunc = notefunc;
    nif->variant = variant;
    nif->param = param;
    l->nifs = g_slist_prepend(l->nifs, nif);
}

static void lfo_params_clear(struct sampler_lfo_params *lfop)
{
    lfop->freq = 0.f;
    lfop->delay = 0.f;
    lfop->fade = 0.f;
}

#define PROC_FIELDS_INITIALISER(type, name, def_value) \
    l->name = def_value;
#define PROC_FIELDS_INITIALISER_dBamp(type, name, def_value) \
    l->name = def_value; \
    l->name##_linearized = -1;

void sampler_layer_init(struct sampler_layer *l)
{
    l->waveform = NULL;
    l->sample_data = NULL;
    
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_INITIALISER)
    
    l->sample_end = 0;
    l->freq = 44100;
    l->mode = spt_mono16;
    l->filter = sft_lp12;
    l->use_keyswitch = 0;
    l->last_key = 0;
    cbox_dahdsr_init(&l->filter_env);
    cbox_dahdsr_init(&l->pitch_env);
    cbox_dahdsr_init(&l->amp_env);
    l->loop_mode = slm_unknown;
    l->velcurve[0] = 0;
    l->velcurve[127] = 1;
    for (int i = 1; i < 127; i++)
        l->velcurve[i] = -1;
    l->velcurve_quadratic = -1; // not known yet
    l->fil_veltrack = 0;
    l->exclusive_group = 0;
    l->off_by = 0;
    lfo_params_clear(&l->amp_lfo_params);
    lfo_params_clear(&l->filter_lfo_params);
    lfo_params_clear(&l->pitch_lfo_params);
    l->modulations = NULL;
    l->nifs = NULL;
    sampler_layer_set_modulation(l, 74, smsrc_none, smdest_cutoff, 9600, 2);
    sampler_layer_set_modulation(l, 71, smsrc_none, smdest_resonance, 12, 2);
    sampler_layer_set_modulation(l, 1, smsrc_pitchlfo, smdest_pitch, 100, 0);
}

void sampler_layer_clone(struct sampler_layer *dst, const struct sampler_layer *src)
{
    memcpy(dst, src, sizeof(struct sampler_layer));
    if (dst->waveform)
        cbox_waveform_ref(dst->waveform);
    dst->modulations = g_slist_copy(dst->modulations);
    for(GSList *mod = dst->modulations; mod; mod = mod->next)
    {
        gpointer dst = g_malloc(sizeof(struct sampler_modulation));
        memcpy(dst, mod->data, sizeof(struct sampler_modulation));
        mod->data = dst;
    }
    dst->nifs = g_slist_copy(dst->nifs);
    for(GSList *nif = dst->nifs; nif; nif = nif->next)
    {
        gpointer dst = g_malloc(sizeof(struct sampler_noteinitfunc));
        memcpy(dst, nif->data, sizeof(struct sampler_noteinitfunc));
        nif->data = dst;
    }
}

void sampler_layer_set_waveform(struct sampler_layer *l, struct cbox_waveform *waveform)
{
    l->waveform = waveform;
    l->sample_data = waveform ? waveform->data : NULL;
    l->freq = (waveform && waveform->info.samplerate) ? waveform->info.samplerate : 44100;
    l->loop_end = waveform ? waveform->info.frames : 0;
    l->sample_end = waveform ? waveform->info.frames : 0;
    l->mode = waveform && waveform->info.channels == 2 ? spt_stereo16 : spt_mono16;
}

#define PROC_FIELDS_FINALISER(type, name, def_value) 
#define PROC_FIELDS_FINALISER_dBamp(type, name, def_value) \
    l->name##_linearized = dB2gain(l->name);

void sampler_layer_finalize(struct sampler_layer *l, struct sampler_module *m)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_FINALISER)

    cbox_envelope_init_dahdsr(&l->amp_env_shape, &l->amp_env, m->module.srate / CBOX_BLOCK_SIZE);
    cbox_envelope_init_dahdsr(&l->filter_env_shape, &l->filter_env,  m->module.srate / CBOX_BLOCK_SIZE);
    cbox_envelope_init_dahdsr(&l->pitch_env_shape, &l->pitch_env,  m->module.srate / CBOX_BLOCK_SIZE);

    if (l->loop_mode == slm_unknown)
        l->loop_mode = l->loop_start == -1 ? slm_no_loop : slm_loop_continuous;
    
    if (l->loop_mode == slm_one_shot || l->loop_mode == slm_no_loop)
        l->loop_start = -1;

    if ((l->loop_mode == slm_loop_continuous || l->loop_mode == slm_loop_sustain) && l->loop_start == -1)
    {
        l->loop_start = 0;
    }

    // if no amp_velcurve_nnn setting, default to quadratic
    if (l->velcurve_quadratic == -1)
        l->velcurve_quadratic = 1;
    // interpolate missing points in velcurve
    int start = 0;
    for (int i = 1; i < 128; i++)
    {
        if (l->velcurve[i] == -1)
            continue;
        float sv = l->velcurve[start];
        float ev = l->velcurve[i];
        if (l->velcurve_quadratic)
        {
            for (int j = start + 1; j < i; j++)
                l->velcurve[j] = sv + (ev - sv) * (j - start) * (j - start) / ((i - start) * (i - start));
        }
        else
        {
            for (int j = start + 1; j < i; j++)
                l->velcurve[j] = sv + (ev - sv) * (j - start) / (i - start);
        }
        start = i;
    }
    l->use_keyswitch = ((l->sw_down != -1) || (l->sw_up != -1) || (l->sw_last != -1) || (l->sw_previous != -1));
    l->last_key = l->sw_lokey;
}

void sampler_load_layer_overrides(struct sampler_layer *l, struct sampler_module *m, const char *cfg_section)
{
    char *imp = cbox_config_get_string(cfg_section, "import");
    if (imp)
        sampler_load_layer_overrides(l, m, imp);
    l->sample_offset = cbox_config_get_int(cfg_section, "offset", l->sample_offset);
    l->sample_offset_random = cbox_config_get_int(cfg_section, "offset_random", l->sample_offset_random);
    l->loop_start = cbox_config_get_int(cfg_section, "loop_start", l->loop_start);
    l->loop_end = cbox_config_get_int(cfg_section, "loop_end", l->loop_end);
    l->loop_evolve = cbox_config_get_int(cfg_section, "loop_evolve", l->loop_evolve);
    l->loop_overlap = cbox_config_get_int(cfg_section, "loop_overlap", l->loop_overlap);
    l->volume = cbox_config_get_float(cfg_section, "volume", l->volume);
    l->pan = cbox_config_get_float(cfg_section, "pan", l->pan);
    l->pitch_keytrack = cbox_config_get_float(cfg_section, "note_scaling", l->pitch_keytrack);
    l->pitch_keycenter = cbox_config_get_int(cfg_section, "root_note", l->pitch_keycenter);
    l->min_note = cbox_config_get_note(cfg_section, "low_note", l->min_note);
    l->max_note = cbox_config_get_note(cfg_section, "high_note", l->max_note);
    l->sw_lokey = cbox_config_get_note(cfg_section, "sw_lokey", l->sw_lokey);
    l->sw_hikey = cbox_config_get_note(cfg_section, "sw_hikey", l->sw_hikey);
    l->sw_last = cbox_config_get_note(cfg_section, "sw_last", l->sw_last);
    l->sw_down = cbox_config_get_note(cfg_section, "sw_down", l->sw_down);
    l->sw_up = cbox_config_get_note(cfg_section, "sw_up", l->sw_up);
    l->sw_previous = cbox_config_get_note(cfg_section, "sw_previous", l->sw_previous);
    l->min_vel = cbox_config_get_int(cfg_section, "low_vel", l->min_vel);
    l->max_vel = cbox_config_get_int(cfg_section, "high_vel", l->max_vel);
    l->seq_pos = cbox_config_get_int(cfg_section, "seq_position", l->seq_pos + 1) - 1;
    l->seq_length = cbox_config_get_int(cfg_section, "seq_length", l->seq_length);
    l->transpose = cbox_config_get_int(cfg_section, "transpose", l->transpose);
    l->tune = cbox_config_get_float(cfg_section, "tune", l->tune);
    cbox_config_get_dahdsr(cfg_section, "amp", &l->amp_env);
    cbox_config_get_dahdsr(cfg_section, "filter", &l->filter_env);
    cbox_config_get_dahdsr(cfg_section, "pitch", &l->pitch_env);
    l->cutoff = cbox_config_get_float(cfg_section, "cutoff", l->cutoff);
    l->resonance = cbox_config_get_float(cfg_section, "resonance", l->resonance);
    l->fil_veltrack = cbox_config_get_float(cfg_section, "fil_veltrack", l->fil_veltrack);
    l->fil_keytrack = cbox_config_get_float(cfg_section, "fil_keytrack", l->fil_keytrack);
    l->fil_keycenter = cbox_config_get_float(cfg_section, "fil_keycenter", l->fil_keycenter);
    if (cbox_config_get_int(cfg_section, "one_shot", 0))
        l->loop_mode = slm_one_shot;
    if (cbox_config_get_int(cfg_section, "loop_sustain", 0))
        l->loop_mode = slm_loop_sustain;
    l->exclusive_group = cbox_config_get_int(cfg_section, "group", l->exclusive_group);
    l->off_by = cbox_config_get_int(cfg_section, "off_by", l->off_by);
    l->output = cbox_config_get_int(cfg_section, "output_pair_no", l->output);
    l->send1bus = cbox_config_get_int(cfg_section, "aux1_bus", l->send1bus);
    l->send2bus = cbox_config_get_int(cfg_section, "aux2_bus", l->send2bus);
    l->send1gain = cbox_config_get_gain(cfg_section, "aux1_gain", l->send1gain);
    l->send2gain = cbox_config_get_gain(cfg_section, "aux2_gain", l->send2gain);
    // l->amp_lfo_depth = cbox_config_get_float(cfg_section, "amp_lfo_depth", l->amp_lfo_depth);
    l->amp_lfo_params.freq = cbox_config_get_float(cfg_section, "amp_lfo_freq", l->amp_lfo_params.freq);
    // l->filter_lfo_depth = cbox_config_get_float(cfg_section, "filter_lfo_depth", l->filter_lfo_depth);
    l->filter_lfo_params.freq = cbox_config_get_float(cfg_section, "filter_lfo_freq", l->filter_lfo_params.freq);
    // l->pitch_lfo_depth = cbox_config_get_float(cfg_section, "pitch_lfo_depth", l->pitch_lfo_depth);
    l->pitch_lfo_params.freq = cbox_config_get_float(cfg_section, "pitch_lfo_freq", l->pitch_lfo_params.freq);
    const char *fil_type = cbox_config_get_string(cfg_section, "fil_type");
    if (fil_type)
    {
        enum sampler_filter_type ft = sampler_filter_type_from_string(fil_type);
        if (ft != sft_unknown)
            l->filter = ft;
        else
            g_warning("Unknown filter type '%s'", fil_type);
    }
    l->delay = cbox_config_get_float(cfg_section, "delay", l->delay);
    l->delay_random = cbox_config_get_float(cfg_section, "delay_random", l->delay_random);
}

void sampler_layer_load(struct sampler_layer *l, struct sampler_module *m, const char *cfg_section, struct cbox_waveform *waveform)
{
    sampler_layer_init(l);
    sampler_layer_set_waveform(l, waveform);
    sampler_load_layer_overrides(l, m, cfg_section);
    sampler_layer_finalize(l, m);
}

