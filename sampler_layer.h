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

#include "dom.h"
#include "wavebank.h"
#include <stdio.h>
#include <stdint.h>

struct sampler_program;
struct sampler_voice;
struct sampler_noteinitfunc;
struct sampler_module;

enum sampler_player_type
{
    spt_inactive,
    spt_mono16,
    spt_stereo16,
    spt_finished
};

enum sampler_loop_mode
{
    slm_unknown,
    slm_no_loop,
    slm_one_shot,
    slm_loop_continuous,
    slm_loop_sustain, // unsupported
    slm_one_shot_chokeable,
    slmcount
};

#define ENUM_VALUES_sampler_loop_mode(MACRO) \
    MACRO("no_loop", slm_no_loop) \
    MACRO("one_shot", slm_one_shot)  \
    MACRO("loop_continuous", slm_loop_continuous) \
    MACRO("loop_sustain", slm_loop_sustain) \
    MACRO("one_shot_chokeable", slm_one_shot_chokeable) 

enum sampler_off_mode
{
    som_unknown,
    som_normal,
    som_fast
};

#define ENUM_VALUES_sampler_off_mode(MACRO) \
    MACRO("normal", som_normal) \
    MACRO("fast", som_fast)  

enum sampler_trigger
{
    stm_attack,
    stm_release,
    stm_first,
    stm_legato,
};

#define ENUM_VALUES_sampler_trigger(MACRO) \
    MACRO("attack", stm_attack) \
    MACRO("release", stm_release) \
    MACRO("first", stm_first) \
    MACRO("legato", stm_legato)

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
    sft_lp12nr,
    sft_hp12nr,
    sft_lp24nr,
    sft_hp24nr,
    sft_lp24hybrid,
};

#define ENUM_VALUES_sampler_filter_type(MACRO) \
    MACRO("lpf_2p", sft_lp12) \
    MACRO("hpf_2p", sft_hp12) \
    MACRO("bpf_2p", sft_bp6)  \
    MACRO("lpf_4p", sft_lp24) \
    MACRO("hpf_4p", sft_hp24) \
    MACRO("bpf_4p", sft_bp12) \
    MACRO("lpf_1p", sft_lp6)  \
    MACRO("hpf_1p", sft_hp6)  \
    MACRO("lpf_2p_nores", sft_lp12nr)  \
    MACRO("hpf_2p_nores", sft_hp12nr)  \
    MACRO("lpf_4p_nores", sft_lp24nr)  \
    MACRO("hpf_4p_nores", sft_hp24nr)  \
    MACRO("lpf_4p_hybrid", sft_lp24hybrid)  \

enum sampler_xf_curve
{
    stxc_power,
    stxc_gain,
};

#define ENUM_VALUES_sampler_xf_curve(MACRO) \
    MACRO("power", stxc_power) \
    MACRO("gain", stxc_gain)

#define ENUM_LIST(MACRO) \
    MACRO(sampler_loop_mode) \
    MACRO(sampler_off_mode) \
    MACRO(sampler_trigger) \
    MACRO(sampler_filter_type) \
    MACRO(sampler_xf_curve) \

#define MAKE_FROM_TO_STRING_EXTERN(enumtype) \
    extern const char *enumtype##_to_string(enum enumtype value); \
    extern gboolean enumtype##_from_string(const char *name, enum enumtype *value);

ENUM_LIST(MAKE_FROM_TO_STRING_EXTERN)

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
    smdest_tonectl,
    smdest_pitchlfo_freq,
    smdest_fillfo_freq,
    smdest_amplfo_freq,

    smdest_eq1_freq,
    smdest_eq1_bw,
    smdest_eq1_gain,
    smdest_eq2_freq,
    smdest_eq2_bw,
    smdest_eq2_gain,
    smdest_eq3_freq,
    smdest_eq3_bw,
    smdest_eq3_gain,

    smdestcount,
};

struct sampler_modulation
{
    enum sampler_modsrc src;
    enum sampler_modsrc src2;
    enum sampler_moddest dest;
    float amount;
    int flags:31;
    unsigned int has_value:1;
};

typedef void (*SamplerNoteInitFunc)(struct sampler_noteinitfunc *nif, struct sampler_voice *voice);

struct sampler_noteinitfunc
{
    SamplerNoteInitFunc notefunc;
    int variant:31;
    unsigned int has_value:1;
    float param;
    // XXXKF no destructor for now - might not be necessary
};

enum sampler_noteinitfunc_envelope_variant
{
    snif_env_delay = 0,
    snif_env_attack = 1,
    snif_env_hold = 2,
    snif_env_decay = 3,
    snif_env_sustain = 4,
    snif_env_release = 5,
};

struct sampler_lfo_params
{
    float freq;
    float delay;
    float fade;
    int wave;
};

struct sampler_eq_params
{
    float freq;
    float bw;
    float gain;
    float effective_freq;
    float vel2freq;
    float vel2gain;
};

typedef int midi_note_t;

/*
 * Transforms:
 * notransform - self-explanatory
 * dBamp - amplitude/gain stored as dB
 */

#define SAMPLER_FIXED_FIELDS(MACRO) \
    MACRO##_string(sample) \
    MACRO(uint32_t, offset, 0) \
    MACRO(uint32_t, offset_random, 0) \
    MACRO(uint32_t, loop_start, 0) \
    MACRO(uint32_t, loop_end, 0) \
    MACRO(uint32_t, end, 0) \
    MACRO(uint32_t, loop_overlap, (uint32_t)-1) \
    MACRO##_enum(sampler_loop_mode, loop_mode, slm_unknown) \
    MACRO##_enum(sampler_trigger, trigger, stm_attack) \
    MACRO##_dBamp(float, volume, 0) \
    MACRO(float, pan, 0) \
    MACRO(float, position, 0) \
    MACRO(float, width, 100) \
    MACRO(float, tune, 0) \
    MACRO(int, transpose, 0) \
    MACRO(int, lochan, 1) \
    MACRO(int, hichan, 16) \
    MACRO(float, lorand, 0) \
    MACRO(float, hirand, 1) \
    MACRO(midi_note_t, key, -1) \
    MACRO(midi_note_t, lokey, 0) \
    MACRO(midi_note_t, hikey, 127) \
    MACRO(midi_note_t, pitch_keycenter, 60) \
    MACRO(int, pitch_keytrack, 100) \
    MACRO(midi_note_t, fil_keycenter, 60) \
    MACRO(int, fil_keytrack, 0) \
    MACRO(midi_note_t, amp_keycenter, 60) \
    MACRO(int, amp_keytrack, 0) \
    MACRO(int, fil_veltrack, 0) \
    MACRO(int, amp_veltrack, 100) \
    MACRO(int, pitch_veltrack, 0) \
    MACRO(int, lovel, 0) \
    MACRO(int, hivel, 127) \
    MACRO(int, lobend, -8192) \
    MACRO(int, hibend, 8192) \
    MACRO(int, velcurve_quadratic, -1) \
    MACRO##_enum(sampler_filter_type, fil_type, sft_lp12) \
    MACRO##_enum(sampler_off_mode, off_mode, som_unknown) \
    MACRO(float, cutoff, -1) \
    MACRO##_dBamp(float, resonance, 0) \
    MACRO(midi_note_t, sw_lokey, 0) \
    MACRO(midi_note_t, sw_hikey, 127) \
    MACRO(midi_note_t, sw_last, -1) \
    MACRO(midi_note_t, sw_down, -1) \
    MACRO(midi_note_t, sw_up, -1) \
    MACRO(midi_note_t, sw_previous, -1) \
    MACRO(int, seq_position, 1) \
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
    MACRO(int, count, 0) \
    MACRO(int, bend_up, 200) \
    MACRO(int, bend_down, 200) \
    MACRO(int, bend_step, 1) \
    MACRO(int, timestretch, 0) \
    MACRO(float, timestretch_jump, 500) \
    MACRO(float, timestretch_crossfade, 100) \
    MACRO(float, rt_decay, 0) \
    MACRO(float, tonectl, 0) \
    MACRO(float, tonectl_freq, 0) \
    MACRO(float, reloffset, 0) \
    MACRO(float, xfin_lokey, 0) \
    MACRO(float, xfin_hikey, 0) \
    MACRO(float, xfout_lokey, 127) \
    MACRO(float, xfout_hikey, 127) \
    MACRO##_enum(sampler_xf_curve, xf_keycurve, stxc_power) \
    MACRO(float, xfin_lovel, 0) \
    MACRO(float, xfin_hivel, 0) \
    MACRO(float, xfout_lovel, 127) \
    MACRO(float, xfout_hivel, 127) \
    MACRO##_enum(sampler_xf_curve, xf_velcurve, stxc_power) \
    MACRO##_dahdsr(amp_env, ampeg, 0) \
    MACRO##_dahdsr(filter_env, fileg, 1) \
    MACRO##_dahdsr(pitch_env, pitcheg, 2) \
    MACRO##_lfo(amp_lfo, amplfo, 0) \
    MACRO##_lfo(filter_lfo, fillfo, 1) \
    MACRO##_lfo(pitch_lfo, pitchlfo, 2) \
    MACRO##_eq(eq1, eq1, 0) \
    MACRO##_eq(eq2, eq2, 1) \
    MACRO##_eq(eq3, eq3, 2) \
    MACRO##_ccrange(on_) \
    MACRO##_ccrange() \

// XXXKF: consider making send1gain the dBamp type... except it's
// a linear percentage value in SFZ spec - bit weird!

#define DAHDSR_FIELDS(MACRO, ...) \
    MACRO(start, 0, 0, ## __VA_ARGS__) \
    MACRO(delay, 1, 0, ## __VA_ARGS__) \
    MACRO(attack, 2, 0, ## __VA_ARGS__) \
    MACRO(hold, 3, 0, ## __VA_ARGS__) \
    MACRO(decay, 4, 0, ## __VA_ARGS__) \
    MACRO(sustain, 5, 100, ## __VA_ARGS__) \
    MACRO(release, 6, 0.05, ## __VA_ARGS__) \
    
#define LFO_FIELDS(MACRO, ...) \
    MACRO(freq, 0, 0, ## __VA_ARGS__) \
    MACRO(delay, 1, 0, ## __VA_ARGS__) \
    MACRO(fade, 2, 0, ## __VA_ARGS__) \
    MACRO(wave, 3, 1, ## __VA_ARGS__) \
    
#define EQ_FIELDS(MACRO, ...) \
    MACRO(freq, 0, 0, ## __VA_ARGS__) \
    MACRO(bw, 1, 1, ## __VA_ARGS__) \
    MACRO(gain, 2, 0, ## __VA_ARGS__) \
    MACRO(vel2freq, 3, 0, ## __VA_ARGS__) \
    MACRO(vel2gain, 5, 0, ## __VA_ARGS__) \
    
#define PROC_SUBSTRUCT_HAS_FIELD(name, index, param, def_value) \
    unsigned int name:1;
#define PROC_SUBSTRUCT_RESET_FIELD(name, index, def_value, param, dst) \
    dst->param.name = def_value;
#define PROC_SUBSTRUCT_RESET_HAS_FIELD(name, index, def_value, param, dst) \
    dst->has_##param.name = 0;
#define PROC_SUBSTRUCT_CLONE(name, index, def_value, param, dst, src) \
    dst->param.name = src->param.name; \
    dst->has_##param.name = src->has_##param.name;
#define PROC_SUBSTRUCT_CLONEPARENT(name, index, def_value, param, l) \
    if (!l->has_##param.name) \
        l->param.name = parent ? parent->param.name : def_value;

struct sampler_dahdsr_has_fields
{
    DAHDSR_FIELDS(PROC_SUBSTRUCT_HAS_FIELD, name)
};

struct sampler_lfo_has_fields
{
    LFO_FIELDS(PROC_SUBSTRUCT_HAS_FIELD, name)
};

struct sampler_eq_has_fields
{
    EQ_FIELDS(PROC_SUBSTRUCT_HAS_FIELD, name)
};

#define PROC_FIELDS_TO_STRUCT(type, name, def_value) \
    type name;
#define PROC_FIELDS_TO_STRUCT_string(name) \
    gchar *name; \
    gboolean name##_changed;
#define PROC_FIELDS_TO_STRUCT_dBamp(type, name, def_value) \
    type name; \
    type name##_linearized;
#define PROC_FIELDS_TO_STRUCT_enum(enumtype, name, def_value) \
    enum enumtype name;
#define PROC_FIELDS_TO_STRUCT_dahdsr(name, parname, index) \
    struct cbox_dahdsr name; \
    struct cbox_envelope_shape name##_shape;
#define PROC_FIELDS_TO_STRUCT_lfo(name, parname, index) \
    struct sampler_lfo_params name;
#define PROC_FIELDS_TO_STRUCT_eq(name, parname, index) \
    struct sampler_eq_params name;
#define PROC_FIELDS_TO_STRUCT_ccrange(name) \
    int8_t name##locc; \
    int8_t name##hicc; \
    int8_t name##cc_number;

#define PROC_HAS_FIELD(type, name, def_value) \
    unsigned int has_##name:1;
#define PROC_HAS_FIELD_string(name) \
    unsigned int has_##name:1;
#define PROC_HAS_FIELD_dBamp(type, name, def_value) \
    PROC_HAS_FIELD(type, name, def_value)
#define PROC_HAS_FIELD_enum(enumtype, name, def_value) \
    PROC_HAS_FIELD(type, name, def_value)

#define PROC_HAS_FIELD_dahdsr(name, parname, index) \
    struct sampler_dahdsr_has_fields has_##name;
#define PROC_HAS_FIELD_lfo(name, parname, index) \
    struct sampler_lfo_has_fields has_##name;
#define PROC_HAS_FIELD_eq(name, parname, index) \
    struct sampler_eq_has_fields has_##name;

#define PROC_HAS_FIELD_ccrange(name) \
    unsigned int has_##name##locc:1; \
    unsigned int has_##name##hicc:1;

CBOX_EXTERN_CLASS(sampler_layer)

struct sampler_layer_data
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_STRUCT)
    SAMPLER_FIXED_FIELDS(PROC_HAS_FIELD)    

    float velcurve[128];
    float eff_velcurve[128];

    GSList *modulations;
    GSList *nifs;

    // computed values:
    float eff_freq;
    int eff_use_keyswitch;
    enum sampler_loop_mode eff_loop_mode;
    struct cbox_waveform *eff_waveform;
    int16_t scratch_loop[2 * MAX_INTERPOLATION_ORDER * 2];
    int16_t scratch_end[2 * MAX_INTERPOLATION_ORDER * 2];
    float resonance_scaled;
    float logcutoff;
    uint32_t eq_bitmask;
};

struct sampler_layer
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    
    struct sampler_layer_data data, *runtime;

    struct sampler_module *module;
    struct sampler_program *parent_program;
    struct sampler_layer *parent_group;

    int last_key, current_seq_position;
    
    GHashTable *unknown_keys;
    GHashTable *child_layers;
};

extern struct sampler_layer *sampler_layer_new(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group);
extern struct sampler_layer *sampler_layer_new_from_section(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group, const char *cfg_section);
extern struct sampler_layer *sampler_layer_new_clone(struct sampler_layer *layer, struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group);
extern void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_set_modulation1(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc, int variant, float param);
extern void sampler_layer_load_overrides(struct sampler_layer *l, const char *cfg_section);
extern void sampler_layer_data_finalize(struct sampler_layer_data *l, struct sampler_layer_data *parent, struct sampler_program *p);
extern void sampler_layer_reset_switches(struct sampler_layer *l, struct sampler_module *m);
extern gboolean sampler_layer_apply_param(struct sampler_layer *l, const char *key, const char *value, GError **error);
extern gboolean sampler_layer_unapply_param(struct sampler_layer *l, const char *key, GError **error);
extern gchar *sampler_layer_to_string(struct sampler_layer *l, gboolean show_inherited);
extern void sampler_layer_dump(struct sampler_layer *l, FILE *f);
extern void sampler_layer_update(struct sampler_layer *l);

extern void sampler_layer_data_clone(struct sampler_layer_data *dst, const struct sampler_layer_data *src, gboolean copy_hasattr);
extern void sampler_layer_data_close(struct sampler_layer_data *l);
extern void sampler_layer_data_destroy(struct sampler_layer_data *l);

extern void sampler_nif_vel2pitch(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_vel2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_vel2env(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_cc2delay(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_cc2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_addrandom(struct sampler_noteinitfunc *nif, struct sampler_voice *v);

static inline gboolean sampler_layer_data_is_4pole(struct sampler_layer_data *v)
{
    if (v->cutoff == -1)
        return FALSE;
    return v->fil_type == sft_lp24hybrid || v->fil_type == sft_lp24 || v->fil_type == sft_lp24nr || v->fil_type == sft_hp24 || v->fil_type == sft_hp24nr || v->fil_type == sft_bp12;
}

#endif
