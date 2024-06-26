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

// arbitrary value that doesn't collide with a useful range
#define SAMPLER_CURVE_GAP -100000
#define NO_HI_BPM_VALUE 10000
#define CC_COUNT 128
#define EXT_CC_COUNT 143
#define MAX_FLEX_LFOS 32

struct sampler_program;
struct sampler_voice;
struct sampler_prevoice;
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

enum sampler_vel_mode
{
    svm_unknown,
    svm_current,
    svm_previous
};

#define ENUM_VALUES_sampler_vel_mode(MACRO) \
    MACRO("current", svm_current) \
    MACRO("previous", svm_previous)

enum sampler_trigger
{
    stm_attack,
    stm_release,
    stm_first,
    stm_legato,
    stm_release_key,
};

#define ENUM_VALUES_sampler_trigger(MACRO) \
    MACRO("attack", stm_attack) \
    MACRO("release", stm_release) \
    MACRO("release_key", stm_release_key) \
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
    sft_lp36,
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
    MACRO("lpf_6p", sft_lp36) \

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
    MACRO(sampler_vel_mode) \
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

    smsrc_pitchbend = 128,
    smsrc_chanaft_sfz2 = 129,
    smsrc_lastpolyaft = 130, // ?
    smsrc_noteonvel = 131,
    smsrc_noteoffvel = 132,
    smsrc_keynotenum = 133,
    smsrc_keynotegate = 134,
    smsrc_random_unipolar = 135,
    smsrc_random_bipolar = 136,
    smsrc_alternate = 137,
    smsrc_keydelta = 140,
    smsrc_keydelta_abs = 141,
    smsrc_tempo = 142,

    // those are per-note, not per-channel
    smsrc_vel,
    smsrc_chanaft,
    smsrc_polyaft,
    smsrc_pitch,
    smsrc_pitchenv,
    smsrc_filenv,
    smsrc_ampenv,
    smsrc_pitchlfo,
    smsrc_fillfo,
    smsrc_amplfo,
    smsrc_none,
    smsrc_lfo00,
    smsrc_lfoend = smsrc_lfo00 + MAX_FLEX_LFOS,
    
    smsrccount = smsrc_lfoend,
    smsrc_perchan_offset = 0,
    smsrc_perchan_count = smsrc_vel,
    smsrc_pernote_offset = smsrc_vel,
    smsrc_pernote_count = smsrccount - smsrc_pernote_offset,

    smsrc_ampeg = smsrc_ampenv,
    smsrc_fileg = smsrc_filenv,
    smsrc_pitcheg = smsrc_pitchenv,
};

#define IS_SMSRC_FLEXLFO(src) ((src) >= smsrc_lfo00 && (src) < smsrc_lfoend)
#define SMSRC_FLEXLFO_NUM(src) ((src) - smsrc_lfo00)
#define SMSRC_FLEXLFO_BY_NUM(src) ((src) + smsrc_lfo00)

enum sampler_moddest
{
    smdest_gain,
    smdest_pitch,
    smdest_cutoff,
    smdest_resonance,
    smdest_tonectl,
    smdest_pan,
    smdest_amplitude,
    smdest_cutoff2,
    smdest_resonance2,
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

    smdest_from_amplfo = smdest_gain,
    smdest_from_fillfo = smdest_cutoff,
    smdest_from_pitchlfo = smdest_pitch,
    smdest_from_ampeg = smdest_gain,
    smdest_from_fileg = smdest_cutoff,
    smdest_from_pitcheg = smdest_pitch,

    smdest_ampeg_stage = 0x80,
    smdest_fileg_stage = 0x90,
    smdest_pitcheg_stage = 0xA0,

    smdest_eg_stage_start = 0x80,
    smdest_eg_stage_end = 0xAF,
};

struct sampler_modulation_key
{
    enum sampler_modsrc src;
    enum sampler_modsrc src2;
    enum sampler_moddest dest;
};

struct sampler_modulation_value
{
    float amount;
    float smooth;
    float step;
    unsigned int curve_id:12;
    unsigned int has_amount:1;
    unsigned int has_curve:1;
    unsigned int has_smooth:1;
    unsigned int has_step:1;
};

#define SAMPLER_COLL_FIELD_LIST_sampler_modulation(MACRO, ...) \
    MACRO(amount, has_amount, float, 0, ## __VA_ARGS__) \
    MACRO(curve_id, has_curve, uint32_t, 0, ## __VA_ARGS__) \
    MACRO(smooth, has_smooth, float, 0, ## __VA_ARGS__) \
    MACRO(step, has_step, float, 0, ## __VA_ARGS__)

#define SAMPLER_COLL_CHAIN_LIST_sampler_modulation(MACRO, ...) \
    MACRO(modulations, modulation, ## __VA_ARGS__)

//////////////////////////////////////////////////////////////////////////////////////////////////

typedef void (*SamplerNoteInitFunc)(struct sampler_noteinitfunc *nif, struct sampler_voice *voice);
typedef void (*SamplerNoteInitFunc2)(struct sampler_noteinitfunc *nif, struct sampler_prevoice *prevoice);

struct sampler_noteinitfunc_key
{
    union {
        SamplerNoteInitFunc notefunc_voice;
        SamplerNoteInitFunc2 notefunc_prevoice;
    };
    int variant;
};

struct sampler_noteinitfunc_value
{
    float value;
    uint32_t curve_id;
    float step;
    unsigned int has_value:1;
    unsigned int has_curve:1;
    unsigned int has_step:1;
};

enum sampler_noteinitfunc_envelope_variant
{
    snif_env_delay = 0,
    snif_env_attack = 1,
    snif_env_hold = 2,
    snif_env_decay = 3,
    snif_env_sustain = 4,
    snif_env_release = 5,
    snif_env_start = 6,
};

#define SAMPLER_COLL_FIELD_LIST_sampler_noteinitfunc(MACRO, ...) \
    MACRO(value, has_value, float, 0, ## __VA_ARGS__) \
    MACRO(curve_id, has_curve, int, 0, ## __VA_ARGS__) \
    MACRO(step, has_step, float, 0, ## __VA_ARGS__)

#define SAMPLER_COLL_CHAIN_LIST_sampler_noteinitfunc(MACRO, ...) \
    MACRO(voice_nifs, voice_nif, ## __VA_ARGS__) \
    MACRO(prevoice_nifs, prevoice_nif, ## __VA_ARGS__)

//////////////////////////////////////////////////////////////////////////////////////////////////

struct sampler_cc_range_key
{
    uint8_t cc_number;
};

struct sampler_cc_range_value
{
    uint8_t locc;
    uint8_t hicc;
    uint8_t has_locc:1;
    uint8_t has_hicc:1;
};

#define SAMPLER_COLL_FIELD_LIST_sampler_cc_range(MACRO, ...) \
    MACRO(locc, has_locc, uint8_t, 0, ## __VA_ARGS__) \
    MACRO(hicc, has_hicc, uint8_t, 127, ## __VA_ARGS__)

#define SAMPLER_COLL_CHAIN_LIST_sampler_cc_range(MACRO, ...) \
    MACRO(on_cc, on_cc, ## __VA_ARGS__) \
    MACRO(cc, cc, ## __VA_ARGS__) \
    MACRO(xfin_cc, xfin_cc, ## __VA_ARGS__) \
    MACRO(xfout_cc, xfout_cc, ## __VA_ARGS__)

//////////////////////////////////////////////////////////////////////////////////////////////////

struct sampler_flex_lfo_key
{
    uint32_t id;
};

struct sampler_flex_lfo_value
{
    // Note: layout/order must be identical to sampler_lfo_params
    float freq;
    float delay;
    float fade;
    int wave;

    uint8_t has_freq:1;
    uint8_t has_delay:1;
    uint8_t has_fade:1;
    uint8_t has_wave:1;
    uint8_t has_phase:1;
    uint8_t has_count:1;
    
    float phase;
    int count;
};

#define SAMPLER_COLL_FIELD_LIST_sampler_flex_lfo(MACRO, ...) \
    MACRO(freq, has_freq, float, 0, ## __VA_ARGS__) \
    MACRO(delay, has_delay, float, 0, ## __VA_ARGS__) \
    MACRO(fade, has_fade, float, 0, ## __VA_ARGS__) \
    MACRO(wave, has_wave, int, 1, ## __VA_ARGS__) \
    MACRO(phase, has_phase, float, 0, ## __VA_ARGS__) \
    MACRO(count, has_count, int, 0, ## __VA_ARGS__)

#define SAMPLER_COLL_CHAIN_LIST_sampler_flex_lfo(MACRO, ...) \
    MACRO(flex_lfos, flex_lfos, ## __VA_ARGS__)

//////////////////////////////////////////////////////////////////////////////////////////////////

#define SAMPLER_COLL_LIST(MACRO) \
    MACRO(sampler_modulation) \
    MACRO(sampler_noteinitfunc) \
    MACRO(sampler_cc_range) \
    MACRO(sampler_flex_lfo)

#define SAMPLER_COLL_DEFINITION(sname) \
    struct sname \
    { \
        struct sname##_key key; \
        struct sname##_value value; \
        struct sname *next; \
    };

SAMPLER_COLL_LIST(SAMPLER_COLL_DEFINITION)

//////////////////////////////////////////////////////////////////////////////////////////////////

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
    MACRO(float, amplitude, 100) \
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
    MACRO(midi_note_t, lokey, -1) \
    MACRO(midi_note_t, hikey, -1) \
    MACRO(midi_note_t, pitch_keycenter, -1) \
    MACRO(int, pitch_keytrack, 100) \
    MACRO(midi_note_t, fil_keycenter, 60) \
    MACRO(int, fil_keytrack, 0) \
    MACRO(midi_note_t, fil2_keycenter, 60) \
    MACRO(int, fil2_keytrack, 0) \
    MACRO(midi_note_t, amp_keycenter, 60) \
    MACRO(int, amp_keytrack, 0) \
    MACRO(int, fil_veltrack, 0) \
    MACRO(int, fil2_veltrack, 0) \
    MACRO(int, amp_veltrack, 100) \
    MACRO(int, lovel, 0) \
    MACRO(int, hivel, 127) \
    MACRO(int, lobend, -8192) \
    MACRO(int, hibend, 8192) \
    MACRO(int, lochanaft, 0) \
    MACRO(int, hichanaft, 127) \
    MACRO(int, lopolyaft, 0) \
    MACRO(int, hipolyaft, 127) \
    MACRO(float, lobpm, 0.0) \
    MACRO(float, hibpm, NO_HI_BPM_VALUE) \
    MACRO(int, velcurve_quadratic, 1) \
    MACRO##_enum(sampler_filter_type, fil_type, sft_lp12) \
    MACRO##_enum(sampler_filter_type, fil2_type, sft_lp12) \
    MACRO##_enum(sampler_off_mode, off_mode, som_unknown) \
    MACRO##_enum(sampler_vel_mode, vel_mode, svm_current) \
    MACRO(float, cutoff, -1) \
    MACRO##_dBamp(float, resonance, 0) \
    MACRO(float, cutoff2, -1) \
    MACRO##_dBamp(float, resonance2, 0) \
    MACRO(midi_note_t, sw_lokey, 0) \
    MACRO(midi_note_t, sw_hikey, 127) \
    MACRO(midi_note_t, sw_last, -1) \
    MACRO(midi_note_t, sw_down, -1) \
    MACRO(midi_note_t, sw_up, -1) \
    MACRO(midi_note_t, sw_previous, -1) \
    MACRO(midi_note_t, sw_default, -1) \
    MACRO(int, seq_position, 1) \
    MACRO(int, seq_length, 1) \
    MACRO(float, sync_offset, 0) \
    MACRO(int, effect1bus, 1) \
    MACRO(int, effect2bus, 2) \
    MACRO(float, effect1, 0) \
    MACRO(float, effect2, 0) \
    MACRO(float, delay, 0) \
    MACRO(int, output, 0) \
    MACRO(int, group, 0) \
    MACRO(int, off_by, 0) \
    MACRO(int, count, 0) \
    MACRO(int, bend_up, 200) \
    MACRO(int, bend_down, -200) \
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
    MACRO##_ccrange(xfin_cc, xfin_) \
    MACRO##_ccrange(xfout_cc, xfout_) \
    MACRO##_enum(sampler_xf_curve, xf_cccurve, stxc_power) \
    MACRO##_dahdsr(amp_env, ampeg, 0) \
    MACRO##_dahdsr(filter_env, fileg, 1) \
    MACRO##_dahdsr(pitch_env, pitcheg, 2) \
    MACRO##_lfo(amp_lfo, amplfo, 0) \
    MACRO##_lfo(filter_lfo, fillfo, 1) \
    MACRO##_lfo(pitch_lfo, pitchlfo, 2) \
    MACRO##_eq(eq1, eq1, 0) \
    MACRO##_eq(eq2, eq2, 1) \
    MACRO##_eq(eq3, eq3, 2) \
    MACRO##_ccrange(on_cc, on_) \
    MACRO##_ccrange(cc, ) \
    MACRO##_midicurve(amp_velcurve) \

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

struct sampler_midi_curve
{
    float values[128];
    uint8_t has_values[128];
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
#define PROC_FIELDS_TO_STRUCT_ccrange(name, parname) \
    struct sampler_cc_range *name;
#define PROC_FIELDS_TO_STRUCT_midicurve(name) \
    struct sampler_midi_curve name;

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
#define PROC_HAS_FIELD_ccrange(name, parname)
#define PROC_HAS_FIELD_midicurve(name)

CBOX_EXTERN_CLASS(sampler_layer)

enum sampler_layer_mod_bitmasks
{
    slmb_ampeg_cc = 0x01,
    slmb_fileg_cc = 0x02,
    slmb_pitcheg_cc = 0x04,
};

struct sampler_layer_computed
{
    // computed values:
    struct cbox_waveform *eff_waveform;
    enum sampler_loop_mode eff_loop_mode;
    uint32_t eff_loop_start, eff_loop_end;
    float eff_freq;
    gboolean eff_use_keyswitch:1;
    gboolean eff_use_simple_trigger_logic:1;
    gboolean eff_use_xfcc:1;
    gboolean eff_use_prevoice:1;
    gboolean eff_use_channel_mixer:1;
    gboolean eff_use_filter_mods:1;
    gboolean eff_is_silent:1;
    int16_t scratch_loop[2 * MAX_INTERPOLATION_ORDER * 2];
    int16_t scratch_end[2 * MAX_INTERPOLATION_ORDER * 2];
    float resonance_scaled, resonance2_scaled;
    float logcutoff, logcutoff2;
    uint32_t eq_bitmask, mod_bitmask;
    int eff_num_stages, eff_num_stages2;
    uint8_t eff_lokey, eff_hikey, eff_pitch_keycenter;

    float eff_amp_velcurve[128];
    struct sampler_flex_lfo **eff_flex_lfo_by_num; // For O(1) lookup
};

struct sampler_layer_data
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_STRUCT)
    SAMPLER_FIXED_FIELDS(PROC_HAS_FIELD)

    struct sampler_modulation *modulations;
    struct sampler_noteinitfunc *voice_nifs, *prevoice_nifs;
    struct sampler_flex_lfo *flex_lfos;

    struct sampler_layer_computed computed;
};

struct sampler_layer
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    
    struct sampler_layer_data data, *runtime;

    struct sampler_module *module;
    struct sampler_program *parent_program;
    struct sampler_layer *parent, *default_child;

    int current_seq_position;
    
    GHashTable *unknown_keys;
    GHashTable *child_layers;
};

extern struct sampler_layer *sampler_layer_new(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group);
extern struct sampler_layer *sampler_layer_new_from_section(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group, const char *cfg_section);
extern struct sampler_layer *sampler_layer_new_clone(struct sampler_layer *layer, struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group);
extern void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_set_modulation1(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_moddest dest, float amount, int flags);
extern void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc_voice, SamplerNoteInitFunc2 notefunc_prevoice, int variant, float param);
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
extern void sampler_nif_vel2offset(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_vel2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_vel2env(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_cc2offset(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_cc2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v);
extern void sampler_nif_addrandom(struct sampler_noteinitfunc *nif, struct sampler_voice *v);

extern void sampler_nif_cc2delay(struct sampler_noteinitfunc *nif, struct sampler_prevoice *pv);
extern void sampler_nif_addrandomdelay(struct sampler_noteinitfunc *nif, struct sampler_prevoice *pv);
extern void sampler_nif_syncbeats(struct sampler_noteinitfunc *nif, struct sampler_prevoice *pv);

extern void sampler_midi_curve_init(struct sampler_midi_curve *curve);
extern void sampler_midi_curve_interpolate(const struct sampler_midi_curve *curve, float dest[128], float def_start, float def_end, gboolean is_quadratic);

#endif

