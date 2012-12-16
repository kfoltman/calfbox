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

#ifndef CBOX_SAMPLER_LAYER_H
#define CBOX_SAMPLER_LAYER_H

#include <stdio.h>
#include <stdint.h>

struct sampler_voice;
struct sampler_noteinitfunc;
struct sampler_module;

enum sample_player_type
{
    spt_inactive,
    spt_mono16,
    spt_stereo16
};

enum sample_loop_mode
{
    slm_unknown,
    slm_no_loop,
    slm_one_shot,
    slm_loop_continuous,
    slm_loop_sustain, // unsupported
};

enum sampler_filter_type
{
    sft_unknown,
    sft_lp12,
    sft_hp12,
    sft_bp6,
    sft_lp24,
    sft_hp24,
    sft_bp12,
    sft_lp6,
    sft_hp6,
};

enum sampler_modsrc
{
    smsrc_cc0 = 0,
    smsrc_chanaft = 120,

    // those are per-note, not per-channel
    smsrc_vel,
    smsrc_polyaft,
    smsrc_pitch,
    smsrc_pitchenv,
    smsrc_filenv,
    smsrc_ampenv,
    smsrc_pitchlfo,
    smsrc_fillfo,
    smsrc_amplfo,
    smsrc_none,
    
    smsrccount,
    smsrc_perchan_offset = 0,
    smsrc_perchan_count = smsrc_vel,
    smsrc_pernote_offset = smsrc_vel,
    smsrc_pernote_count = smsrccount - smsrc_pernote_offset,
};

enum sampler_moddest
{
    smdest_gain,
    smdest_pitch,
    smdest_cutoff,
    smdest_resonance,
    
    smdestcount
};

struct sampler_modulation
{
    enum sampler_modsrc src;
    enum sampler_modsrc src2;
    enum sampler_moddest dest;
    float amount;
    int flags;
};

typedef void (*SamplerNoteInitFunc)(struct sampler_noteinitfunc *nif, struct sampler_voice *voice);

struct sampler_noteinitfunc
{
    SamplerNoteInitFunc notefunc;
    int variant;
    float param;
    // XXXKF no destructor for now - might not be necessary
};

struct sampler_lfo_params
{
    float freq;
    float delay;
    float fade;
};

typedef int midi_note_t;

/*
 * Transforms:
 * notransform - self-explanatory
 * dBamp - amplitude/gain stored as dB
 */

#define SAMPLER_FIXED_FIELDS(MACRO) \
    MACRO(uint32_t, sample_offset, 0) \
    MACRO(uint32_t, sample_offset_random, 0) \
    MACRO(uint32_t, loop_start, -1) \
    MACRO(uint32_t, loop_end, -1) \
    MACRO(uint32_t, sample_end, -1) \
    MACRO(uint32_t, loop_evolve, -1) \
    MACRO(uint32_t, loop_overlap, -1) \
    MACRO##_dBamp(float, volume, 0) \
    MACRO(float, pan, 0) \
    MACRO(float, tune, 0) \
    MACRO(int, transpose, 0) \
    MACRO(int, min_chan, 1) \
    MACRO(int, max_chan, 16) \
    MACRO(midi_note_t, lokey, 0) \
    MACRO(midi_note_t, hikey, 127) \
    MACRO(midi_note_t, pitch_keycenter, 60) \
    MACRO(int, pitch_keytrack, 100) \
    MACRO(midi_note_t, fil_keycenter, 60) \
    MACRO(int, fil_keytrack, 0) \
    MACRO(int, fil_veltrack, 0) \
    MACRO(int, min_vel, 0) \
    MACRO(int, max_vel, 127) \
    MACRO(int, velcurve_quadratic, -1) \
    MACRO(float, cutoff, 21000) \
    MACRO(float, resonance, 0.707) \
    MACRO(midi_note_t, sw_lokey, 0) \
    MACRO(midi_note_t, sw_hikey, 127) \
    MACRO(midi_note_t, sw_last, -1) \
    MACRO(midi_note_t, sw_down, -1) \
    MACRO(midi_note_t, sw_up, -1) \
    MACRO(midi_note_t, sw_previous, -1) \
    MACRO(int, seq_pos, 0) \
    MACRO(int, seq_length, 1) \
    MACRO(int, send1bus, 1) \
    MACRO(int, send2bus, 2) \
    MACRO(float, send1gain, 0) \
    MACRO(float, send2gain, 0) \
    MACRO(float, delay, 0) \
    MACRO(float, delay_random, 0) \
    MACRO(int, output, 0) \
    MACRO(int, exclusive_group, 0) \
    MACRO(int, off_by, 0) \
    MACRO##_dahdsr(amp_env, ampeg) \
    MACRO##_dahdsr(filter_env, fileg) \
    MACRO##_dahdsr(pitch_env, pitcheg) \
    MACRO##_lfo(amp_lfo, amplfo) \
    MACRO##_lfo(filter_lfo, fillfo) \
    MACRO##_lfo(pitch_lfo, pitchlfo) \

// XXXKF: consider making send1gain the dBamp type

#define PROC_FIELDS_TO_STRUCT(type, name, def_value) \
    type name;
#define PROC_FIELDS_TO_STRUCT_dBamp(type, name, def_value) \
    type name; \
    type name##_linearized;
#define PROC_FIELDS_TO_STRUCT_dahdsr(name, parname) \
    struct cbox_dahdsr name; \
    struct cbox_envelope_shape name##_shape;
#define PROC_FIELDS_TO_STRUCT_lfo(name, parname) \
    struct sampler_lfo_params name##_params; \

struct sampler_layer
{
    enum sample_player_type mode;
    enum sampler_filter_type filter;
    struct cbox_waveform *waveform;
    int16_t *sample_data;

    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_STRUCT)

    float freq;
    int use_keyswitch;
    int last_key;
    enum sample_loop_mode loop_mode;
    float velcurve[128];
    
    GSList *modulations;
    GSList *nifs;
};

extern void sampler_layer_init(struct sampler_layer *l);
extern void sampler_layer_load(struct sampler_layer *l, struct sampler_module *m, const char *cfg_section, struct cbox_waveform *waveform);
extern void sampler_layer_set_waveform(struct sampler_layer *l, struct cbox_waveform *waveform);
extern void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_set_modulation1(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc, int variant, float param);
extern void sampler_load_layer_overrides(struct sampler_layer *l, struct sampler_module *m, const char *cfg_section);
extern void sampler_layer_clone(struct sampler_layer *dst, const struct sampler_layer *src);
extern void sampler_layer_finalize(struct sampler_layer *l, struct sampler_module *m);
extern void sampler_layer_dump(struct sampler_layer *l, FILE *f);

extern void sampler_nif_vel2pitch(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_vel2env(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_cc2delay(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_addrandom(struct sampler_noteinitfunc *nif, struct sampler_voice *v);

extern enum sampler_filter_type sampler_filter_type_from_string(const char *name);

#endif
