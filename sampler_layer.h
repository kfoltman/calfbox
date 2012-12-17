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

struct sampler_program;
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
    slmcount
};

typedef enum sample_loop_mode sample_loop_mode_t;

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
    MACRO(uint32_t, offset, 0) \
    MACRO(uint32_t, offset_random, 0) \
    MACRO(uint32_t, loop_start, -1) \
    MACRO(uint32_t, loop_end, -1) \
    MACRO(uint32_t, sample_end, -1) \
    MACRO(uint32_t, loop_evolve, -1) \
    MACRO(uint32_t, loop_overlap, -1) \
    MACRO(sample_loop_mode_t, loop_mode, slm_unknown) \
    MACRO##_dBamp(float, volume, 0) \
    MACRO(float, pan, 0) \
    MACRO(float, tune, 0) \
    MACRO(int, transpose, 0) \
    MACRO(int, lochan, 1) \
    MACRO(int, hichan, 16) \
    MACRO(midi_note_t, key, -1) \
    MACRO(midi_note_t, lokey, 0) \
    MACRO(midi_note_t, hikey, 127) \
    MACRO(midi_note_t, pitch_keycenter, 60) \
    MACRO(int, pitch_keytrack, 100) \
    MACRO(midi_note_t, fil_keycenter, 60) \
    MACRO(int, fil_keytrack, 0) \
    MACRO(int, fil_veltrack, 0) \
    MACRO(int, lovel, 0) \
    MACRO(int, hivel, 127) \
    MACRO(int, velcurve_quadratic, -1) \
    MACRO(float, cutoff, 21000) \
    MACRO##_dBamp(float, resonance, 0) \
    MACRO(midi_note_t, sw_lokey, 0) \
    MACRO(midi_note_t, sw_hikey, 127) \
    MACRO(midi_note_t, sw_last, -1) \
    MACRO(midi_note_t, sw_down, -1) \
    MACRO(midi_note_t, sw_up, -1) \
    MACRO(midi_note_t, sw_previous, -1) \
    MACRO(int, seq_position, 0) \
    MACRO(int, seq_length, 1) \
    MACRO(int, effect1bus, 1) \
    MACRO(int, effect2bus, 2) \
    MACRO(float, effect1, 0) \
    MACRO(float, effect2, 0) \
    MACRO(float, delay, 0) \
    MACRO(float, delay_random, 0) \
    MACRO(int, output, 0) \
    MACRO(int, group, 0) \
    MACRO(int, off_by, 0) \
    MACRO##_dahdsr(amp_env, ampeg, 0) \
    MACRO##_dahdsr(filter_env, fileg, 1) \
    MACRO##_dahdsr(pitch_env, pitcheg, 2) \
    MACRO##_lfo(amp_lfo, amplfo, 0) \
    MACRO##_lfo(filter_lfo, fillfo, 1) \
    MACRO##_lfo(pitch_lfo, pitchlfo, 2) \

// XXXKF: consider making send1gain the dBamp type... except it's
// a linear percentage value in SFZ spec - bit weird!

#define DAHDSR_FIELDS(MACRO, ...) \
    MACRO(start, 0, ## __VA_ARGS__) \
    MACRO(delay, 1, ## __VA_ARGS__) \
    MACRO(attack, 2, ## __VA_ARGS__) \
    MACRO(hold, 3, ## __VA_ARGS__) \
    MACRO(decay, 4, ## __VA_ARGS__) \
    MACRO(sustain, 5, ## __VA_ARGS__) \
    MACRO(release, 6, ## __VA_ARGS__) \
    
#define LFO_FIELDS(MACRO, ...) \
    MACRO(freq, 0, ## __VA_ARGS__) \
    MACRO(delay, 1, ## __VA_ARGS__) \
    MACRO(fade, 2, ## __VA_ARGS__) \
    
#define PROC_SUBSTRUCT_HAS_FIELD(name, index, param) \
    unsigned int name:1;
#define PROC_SUBSTRUCT_RESET_HAS_FIELD(name, index, param) \
    l->has_##param.name = 0;
struct sampler_dahdsr_has_fields
{
    DAHDSR_FIELDS(PROC_SUBSTRUCT_HAS_FIELD, name)
};

struct sampler_lfo_has_fields
{
    LFO_FIELDS(PROC_SUBSTRUCT_HAS_FIELD, name)
};

#define PROC_FIELDS_TO_STRUCT(type, name, def_value) \
    type name;
#define PROC_FIELDS_TO_STRUCT_dBamp(type, name, def_value) \
    type name; \
    type name##_linearized;
#define PROC_FIELDS_TO_STRUCT_dahdsr(name, parname, index) \
    struct cbox_dahdsr name; \
    struct cbox_envelope_shape name##_shape;
#define PROC_FIELDS_TO_STRUCT_lfo(name, parname, index) \
    struct sampler_lfo_params name##_params; \

#define PROC_HAS_FIELD(type, name, def_value) \
    unsigned int has_##name:1;
#define PROC_HAS_FIELD_dBamp(type, name, def_value) \
    PROC_HAS_FIELD(type, name, def_value)

#define PROC_HAS_FIELD_dahdsr(name, parname, index) \
    struct sampler_dahdsr_has_fields has_##name;

#define PROC_HAS_FIELD_lfo(name, parname, index) \
    struct sampler_lfo_has_fields has_##name;

struct sampler_layer
{
    struct sampler_program *parent_program;
    struct sampler_layer *parent_group;
    int child_count;
    enum sample_player_type mode;
    enum sampler_filter_type filter;
    struct cbox_waveform *waveform;
    int16_t *sample_data;

    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_STRUCT)
    SAMPLER_FIXED_FIELDS(PROC_HAS_FIELD)

    float freq;
    int use_keyswitch;
    int last_key;
    float velcurve[128];
    
    GSList *modulations;
    GSList *nifs;
};

extern struct sampler_layer *sampler_layer_new(struct sampler_program *parent_program, struct sampler_layer *parent_group);
extern struct sampler_layer *sampler_layer_new_from_section(struct sampler_module *m, struct sampler_program *parent_program, const char *cfg_section);
extern void sampler_layer_set_waveform(struct sampler_layer *l, struct cbox_waveform *waveform);
extern void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_set_modulation1(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc, int variant, float param);
extern void sampler_layer_load_overrides(struct sampler_layer *l, const char *cfg_section);
extern void sampler_layer_finalize(struct sampler_layer *l, struct sampler_module *m);
extern gboolean sampler_layer_apply_param(struct sampler_layer *l, const char *key, const char *value, GError **error);
extern gchar *sampler_layer_to_string(struct sampler_layer *l);
extern void sampler_layer_dump(struct sampler_layer *l, FILE *f);
extern void sampler_layer_destroy(struct sampler_layer *l);

extern void sampler_nif_vel2pitch(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_vel2env(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_cc2delay(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_addrandom(struct sampler_noteinitfunc *nif, struct sampler_voice *v);

extern enum sampler_filter_type sampler_filter_type_from_string(const char *name);

#endif
