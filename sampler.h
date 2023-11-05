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

#ifndef CBOX_SAMPLER_H
#define CBOX_SAMPLER_H

#include "biquad-float.h"
#include "envelope.h"
#include "module.h"
#include "onepole-float.h"
#include "prefetch_pipe.h"
#include "sampler_layer.h"
#include "sampler_prg.h"
#include "wavebank.h"
#include <stdint.h>

#define MAX_SAMPLER_VOICES 128
#define MAX_SAMPLER_PREVOICES 128
#define SAMPLER_NO_LOOP ((uint32_t)-1)

#define CBOX_SAMPLER_ERROR cbox_sampler_error_quark()

enum CboxSamplerError
{
    CBOX_SAMPLER_ERROR_FAILED,
    CBOX_SAMPLER_ERROR_INVALID_LAYER,
    CBOX_SAMPLER_ERROR_INVALID_WAVEFORM,
    CBOX_SAMPLER_ERROR_NO_PROGRAMS
};

struct sampler_noteinitfunc;
struct sampler_voice;
struct sampler_prevoice;

#define GET_RT_FROM_sampler_channel(channel) ((channel)->module->module.rt)

#define MAX_KEYSWITCH_GROUPS 16

struct sampler_channel
{
    struct sampler_module *module;
    int pitchwheel;
    uint32_t switchmask[4];
    uint32_t sustainmask[4];
    uint32_t sostenutomask[4];
    int previous_note, first_note_vel;
    struct sampler_program *program;
    struct sampler_voice *voices_running;
    int active_voices, active_prevoices;
    uint8_t prev_note_velocity[128];
    uint8_t poly_pressure[128];
    uint32_t prev_note_start_time[128];
    int channel_volume_cc, channel_pan_cc;
    int output_shift;
    uint32_t poly_pressure_mask;
    uint8_t intcc[smsrc_perchan_count];
    float floatcc[smsrc_perchan_count];
    uint8_t last_polyaft, last_chanaft;
    uint8_t keyswitch_state[MAX_KEYSWITCH_GROUPS];
    uint8_t keyswitch_lastkey[MAX_KEYSWITCH_GROUPS];
};

struct sampler_lfo
{
    uint32_t phase, delta, xdelta;
    uint32_t age, delay, fade;
    int32_t wave;
    float random_value;
};

struct sampler_gen
{
    enum sampler_player_type mode;
    int16_t *sample_data;
    int16_t *scratch;

    uint64_t bigpos, bigdelta;
    uint64_t virtpos, virtdelta;
    uint32_t loop_start, loop_end;
    uint32_t cur_sample_end;
    float lgain, rgain;
    float last_lgain, last_rgain;
    float fadein_counter;
    uint64_t fadein_pos;

    // In-memory mode only
    uint32_t loop_overlap;
    float loop_overlap_step;
    float stretching_jump;
    float stretching_crossfade;
    uint32_t play_count, loop_count;
    int16_t scratch_bandlimited[2 * MAX_INTERPOLATION_ORDER * 2];

    // Streaming mode only
    int16_t *streaming_buffer;
    uint32_t consumed, consumed_credit, streaming_buffer_frames;
    gboolean prefetch_only_loop, in_streaming_buffer;
};

struct sampler_prevoice
{
    struct sampler_prevoice *prev, *next;
    struct sampler_layer_data *layer_data;
    struct sampler_channel *channel;
    int note, vel;
    int age;
    double sync_trigger_time, sync_initial_time, sync_beats;
    float delay_computed;
};

struct sampler_filter
{
    struct cbox_biquadf_coeffs filter_coeffs, filter_coeffs_extra;
    struct cbox_biquadf_coeffs *second_filter;
    struct cbox_biquadf_state filter_left[3], filter_right[3];
};

struct sampler_voice
{
    struct sampler_voice *prev, *next;
    struct sampler_layer_data *layer;
    // Note: may be NULL when program is being deleted
    struct sampler_program *program;
    struct cbox_waveform *last_waveform;
    struct sampler_gen gen;
    struct cbox_prefetch_pipe *current_pipe;
    int note;
    int vel;
    int released, released_with_sustain, released_with_sostenuto, captured_sostenuto;
    int off_by;
    int age;
    float pitch_shift;
    float cutoff_shift, cutoff2_shift;
    float gain_shift, gain_fromvel;
    struct sampler_filter filter, filter2;
    struct cbox_onepolef_state onepole_left, onepole_right;
    struct cbox_onepolef_coeffs onepole_coeffs;
    struct sampler_channel *channel;
    struct cbox_envelope amp_env, filter_env, pitch_env;
    struct sampler_lfo amp_lfo, filter_lfo, pitch_lfo;
    enum sampler_loop_mode loop_mode;
    int output_pair_no;
    int send1bus, send2bus;
    float send1gain, send2gain;
    int serial_no;
    struct cbox_envelope_shape vel_envs[3], cc_envs[3]; // amp, filter, pitch
    struct cbox_biquadf_state eq_left[3], eq_right[3];
    struct cbox_biquadf_coeffs eq_coeffs[3];
    gboolean layer_changed;
    int last_level;
    uint64_t last_level_min_rate;
    uint32_t last_eq_bitmask;
    float reloffset;
    uint32_t offset;
    int off_vel;
};

struct sampler_module
{
    struct cbox_module module;

    struct sampler_voice *voices_free, voices_all[MAX_SAMPLER_VOICES];
    struct sampler_prevoice *prevoices_free, prevoices_all[MAX_SAMPLER_PREVOICES], *prevoices_running;
    struct sampler_channel channels[16];
    struct sampler_program **programs;
    uint32_t program_count;
    int active_voices, max_voices;
    int active_prevoices;
    int serial_no;
    int output_pairs, aux_pairs;
    uint32_t current_time;
    gboolean deleting;
    int disable_mixer_controls;
    struct cbox_prefetch_stack *pipe_stack;
    struct cbox_sincos sincos[12800];
};

#define MAX_RELEASED_GROUPS 16

struct sampler_released_groups
{
    // Groups 1-32 use a bitmask
    uint32_t low_groups;
    int group_count;
    int groups[MAX_RELEASED_GROUPS];
};

static inline void sampler_released_groups_init(struct sampler_released_groups *groups)
{
    groups->low_groups = 0;
    groups->group_count = 0;
}

static inline gboolean sampler_released_groups_check(struct sampler_released_groups *groups, int group)
{
    if (group <= 32)
        return (groups->low_groups >> (group - 1)) & 1;
    for (int j = 0; j < groups->group_count; j++)
    {
        if (groups->groups[j] == group)
            return TRUE;
    }
    return FALSE;
}

static inline void sampler_released_groups_add(struct sampler_released_groups *groups, int group)
{
    if (group <= 32)
    {
        groups->low_groups |= (1 << (group - 1));
        return;
    }
    if (groups->group_count >= MAX_RELEASED_GROUPS)
        return;
    if (!sampler_released_groups_check(groups, group))
        groups->groups[groups->group_count++] = group;
}

extern GQuark cbox_sampler_error_quark(void);

extern void sampler_register_program(struct sampler_module *m, struct sampler_program *pgm);
extern gboolean sampler_select_program(struct sampler_module *m, int channel, const gchar *preset, GError **error);
extern void sampler_unselect_program(struct sampler_module *m, struct sampler_program *prg);
extern double sampler_get_current_beat(struct sampler_module *m);

extern void sampler_channel_init(struct sampler_channel *c, struct sampler_module *m);
// This function may only be called from RT thread!
extern void sampler_channel_set_program_RT(struct sampler_channel *c, struct sampler_program *prg);
// ... and this one is RT-safe
extern void sampler_channel_set_program(struct sampler_channel *c, struct sampler_program *prg);
extern void sampler_channel_start_note(struct sampler_channel *c, int note, int vel, enum sampler_trigger is_release_trigger);
extern void sampler_channel_stop_note(struct sampler_channel *c, int note, int vel, gboolean is_polyaft);
extern void sampler_channel_program_change(struct sampler_channel *c, int program);
extern void sampler_channel_stop_sustained(struct sampler_channel *c);
extern void sampler_channel_stop_sostenuto(struct sampler_channel *c);
extern void sampler_channel_capture_sostenuto(struct sampler_channel *c);
extern void sampler_channel_release_groups(struct sampler_channel *c, int note, struct sampler_released_groups *exgroups);
extern void sampler_channel_stop_all(struct sampler_channel *c);
extern void sampler_channel_process_cc(struct sampler_channel *c, int cc, int val);
extern void sampler_channel_reset_keyswitches(struct sampler_channel *c);

extern void sampler_voice_start(struct sampler_voice *v, struct sampler_channel *c, struct sampler_layer_data *l, int note, int vel, struct sampler_released_groups *exgroups);
extern void sampler_voice_start_silent(struct sampler_layer_data *l, struct sampler_released_groups *exgroups);
extern void sampler_voice_release(struct sampler_voice *v, gboolean is_polyaft);
extern void sampler_voice_process(struct sampler_voice *v, struct sampler_module *m, cbox_sample_t **outputs);
extern void sampler_voice_link(struct sampler_voice **pv, struct sampler_voice *v);
extern void sampler_voice_unlink(struct sampler_voice **pv, struct sampler_voice *v);
extern void sampler_voice_inactivate(struct sampler_voice *v, gboolean expect_active);
extern void sampler_voice_update_params_from_layer(struct sampler_voice *v);
extern float sampler_channel_get_expensive_cc(struct sampler_channel *c, struct sampler_voice *v, struct sampler_prevoice *pv, int cc_no);

extern void sampler_prevoice_start(struct sampler_prevoice *pv, struct sampler_channel *c, struct sampler_layer_data *l, int note, int vel);
extern int sampler_prevoice_process(struct sampler_prevoice *pv, struct sampler_module *m);
extern void sampler_prevoice_link(struct sampler_prevoice **pv, struct sampler_prevoice *v);
extern void sampler_prevoice_unlink(struct sampler_prevoice **pv, struct sampler_prevoice *v);

extern float sampler_sine_wave[2049];

static inline int sampler_channel_addcc(struct sampler_channel *c, int cc_no)
{
    return (((int)c->intcc[cc_no]) << 7) + c->intcc[cc_no + 32];
}

static inline float sampler_channel_getcc(struct sampler_channel *c, struct sampler_voice *v, int cc_no)
{
    if (cc_no < 128)
        return c->floatcc[cc_no];
    return sampler_channel_get_expensive_cc(c, v, NULL, cc_no);
}

static inline float sampler_program_get_curve_value(struct sampler_program *program, uint32_t curve_id, float val)
{
    if (val < 0)
        val = 0;
    if (val > 1)
        val = 1;
    if (curve_id < MAX_MIDI_CURVES && program->interpolated_curves[curve_id])
    {
        float *curve = program->interpolated_curves[curve_id];
        int vint = floorf(val * 127);
        // Linear interpolation if within bounds
        if (vint < 127)
        {
            float vfrac = val * 127 - vint;
            val = curve[vint] + (curve[vint + 1] - curve[vint]) * vfrac;
        }
        else
            val = curve[vint];
    }
    else
    {
        // possibly wrong implementation of built in curves
        switch(curve_id)
        {
            case 0:
                break;
            case 1:
            case 3:
                // slightly fake bipolar, so that both 63 and 64 are 'neutral' (needs to be somewhat symmetric)
                if (val < 63.f/127.f)
                    val = (curve_id == 3 ? -1 : 1) * -(63.f - 127 * val) / 63.f;
                else if (val <= 64.f/127.f)
                    val = 0;
                else
                    val = (curve_id == 3 ? -1 : 1) * (127 * val - 64) / 63.f;
                break;
            case 2:
                val = 1 - val; break;
            case 4:
                val = val * val;
                break; // maybe, or maybe it's inverted?
            case 5: val = sqrtf(val);
                break; // maybe
            case 6: val = sqrtf(1-val);
                break; // maybe
        }
    }
    return val;
}

static inline float sampler_channel_getcc_mod(struct sampler_channel *c, struct sampler_voice *v, int cc_no, int curve_id, float step)
{
    float val = (cc_no < 128) ? c->floatcc[cc_no] : sampler_channel_get_expensive_cc(c, v, NULL, cc_no);
    if (step)
        val = floorf(0.9999f * val * (step + 1)) / step;
    if (curve_id || c->program->interpolated_curves[0])
        val = sampler_program_get_curve_value(c->program, curve_id, val);
    return val;
}

static inline int sampler_channel_getintcc(struct sampler_channel *c, struct sampler_voice *v, int cc_no)
{
    if (cc_no < 128)
        return c->intcc[cc_no];
    return (int)127 * (sampler_channel_get_expensive_cc(c, v, NULL, cc_no));
}

static inline float sampler_channel_getcc_prevoice(struct sampler_channel *c, struct sampler_prevoice *pv, int cc_no, int curve_id, float step)
{
    float val = (cc_no < 128) ? c->floatcc[cc_no] : sampler_channel_get_expensive_cc(c, NULL, pv, cc_no);
    if (step)
        val = floorf(0.9999f * val * (step + 1)) / step;
    if (curve_id || c->program->interpolated_curves[0])
        val = sampler_program_get_curve_value(c->program, curve_id, val);
    return val;
}

static inline float sampler_channel_get_poly_pressure(struct sampler_channel *c, uint8_t note)
{
    note &= 0x7F;
    return (c->poly_pressure_mask & (1 << (note >> 2))) ? c->poly_pressure[note] * (1.f / 127.f) : 0;;
}

static inline gboolean sampler_cc_range_is_in(const struct sampler_cc_range *range, struct sampler_channel *c)
{
    while(range)
    {
        int ccval = sampler_channel_getintcc(c, NULL, range->key.cc_number);
        if (ccval < range->value.locc || ccval > range->value.hicc)
            return FALSE;
        range = range->next;
    }
    return TRUE;
}

#define FOREACH_VOICE(var, p) \
    for (struct sampler_voice *p = (var), *p##_next = NULL; p && (p##_next = p->next, TRUE); p = p##_next)
#define FOREACH_PREVOICE(var, p) \
    for (struct sampler_prevoice *p = (var), *p##_next = NULL; p && (p##_next = p->next, TRUE); p = p##_next)
#define CANCEL_PREVOICE(var, p) \
    {\
        struct sampler_prevoice *_tmp = (p); \
        if (_tmp->prev) \
            _tmp->prev->next = _tmp->next; \
        else \
            var = _tmp->next; \
        _tmp->prev = _tmp->next = NULL; \
    }

gboolean load_program_at(struct sampler_module *m, const char *cfg_section, const char *name, int prog_no, struct sampler_program **ppgm, GError **error);

#endif
