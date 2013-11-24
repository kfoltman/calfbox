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
#include "prefetch_pipe.h"
#include "sampler_layer.h"
#include "sampler_prg.h"
#include "wavebank.h"
#include <stdint.h>

#define MAX_SAMPLER_VOICES 128

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

#define GET_RT_FROM_sampler_channel(channel) ((channel)->module->module.rt)

struct sampler_channel
{
    struct sampler_module *module;
    int pitchwheel;
    uint32_t switchmask[4];
    uint32_t sustainmask[4];
    uint32_t sostenutomask[4];
    int previous_note;
    uint8_t cc[smsrc_perchan_count];
    struct sampler_program *program;
    struct sampler_voice *voices_running;
    int active_voices;
    uint8_t prev_note_velocity[128];
    uint32_t prev_note_start_time[128];
    int channel_volume_cc, channel_pan_cc;
};

struct sampler_lfo
{
    uint32_t phase, delta;
    uint32_t age, delay, fade;
};

struct sampler_gen
{
    enum sampler_player_type mode;
    int16_t *sample_data;
    int16_t *scratch;
    
    uint64_t bigpos, bigdelta;
    uint32_t loop_start, loop_end;
    uint32_t cur_sample_end;
    float lgain, rgain;
    float last_lgain, last_rgain;

    // In-memory mode only
    uint32_t loop_overlap;
    float loop_overlap_step;
    uint32_t play_count, loop_count;
    int16_t scratch_bandlimited[2 * MAX_INTERPOLATION_ORDER * 2];
    
    // Streaming mode only
    int16_t *streaming_buffer;
    uint32_t consumed, consumed_credit, streaming_buffer_frames;
    gboolean prefetch_only_loop, in_streaming_buffer;
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
    int delay;
    int age;
    float pitch_shift;
    float cutoff_shift;
    float gain_shift, gain_fromvel;
    struct cbox_biquadf_state filter_left, filter_right;
    struct cbox_biquadf_state filter_left2, filter_right2;
    struct cbox_biquadf_coeffs filter_coeffs, filter_coeffs_extra;
    struct sampler_channel *channel;
    struct cbox_envelope amp_env, filter_env, pitch_env;
    struct sampler_lfo amp_lfo, filter_lfo, pitch_lfo;
    enum sampler_loop_mode loop_mode;
    int output_pair_no;
    int send1bus, send2bus;
    float send1gain, send2gain;
    int serial_no;
    struct cbox_envelope_shape dyn_envs[3]; // amp, filter, pitch
    struct cbox_biquadf_state eq_left[3], eq_right[3];
    struct cbox_biquadf_coeffs eq_coeffs[3];
    gboolean layer_changed;
    int last_level;
    uint64_t last_level_min_rate;
    uint32_t last_eq_bitmask;
};

struct sampler_module
{
    struct cbox_module module;

    struct sampler_voice *voices_free, voices_all[MAX_SAMPLER_VOICES];
    struct sampler_channel channels[16];
    struct sampler_program **programs;
    int program_count;
    int active_voices, max_voices;
    int serial_no;
    int output_pairs, aux_pairs;
    uint32_t current_time;
    gboolean deleting;
    int disable_mixer_controls;
    struct cbox_prefetch_stack *pipe_stack;
    struct cbox_sincos sincos[12800];
};

#define MAX_RELEASED_GROUPS 4

extern GQuark cbox_sampler_error_quark(void);

extern void sampler_register_program(struct sampler_module *m, struct sampler_program *pgm);
extern gboolean sampler_select_program(struct sampler_module *m, int channel, const gchar *preset, GError **error);
extern void sampler_unselect_program(struct sampler_module *m, struct sampler_program *prg);

extern void sampler_channel_init(struct sampler_channel *c, struct sampler_module *m);
// This function may only be called from RT thread!
extern void sampler_channel_set_program_RT(struct sampler_channel *c, struct sampler_program *prg);
// ... and this one is RT-safe
extern void sampler_channel_set_program(struct sampler_channel *c, struct sampler_program *prg);
extern void sampler_channel_start_note(struct sampler_channel *c, int note, int vel, gboolean is_release_trigger);
extern void sampler_channel_stop_note(struct sampler_channel *c, int note, int vel, gboolean is_polyaft);
extern void sampler_channel_program_change(struct sampler_channel *c, int program);
extern void sampler_channel_stop_sustained(struct sampler_channel *c);
extern void sampler_channel_stop_sostenuto(struct sampler_channel *c);
extern void sampler_channel_capture_sostenuto(struct sampler_channel *c);
extern void sampler_channel_release_groups(struct sampler_channel *c, int note, int exgroups[MAX_RELEASED_GROUPS], int exgroupcount);
extern void sampler_channel_stop_all(struct sampler_channel *c);
extern void sampler_channel_process_cc(struct sampler_channel *c, int cc, int val);

extern void sampler_voice_start(struct sampler_voice *v, struct sampler_channel *c, struct sampler_layer_data *l, int note, int vel, int *exgroups, int *pexgroupcount);
extern void sampler_voice_release(struct sampler_voice *v, gboolean is_polyaft);
extern void sampler_voice_process(struct sampler_voice *v, struct sampler_module *m, cbox_sample_t **outputs);
extern void sampler_voice_link(struct sampler_voice **pv, struct sampler_voice *v);
extern void sampler_voice_unlink(struct sampler_voice **pv, struct sampler_voice *v);
extern void sampler_voice_inactivate(struct sampler_voice *v, gboolean expect_active);

extern float sampler_sine_wave[2049];

static inline int sampler_channel_addcc(struct sampler_channel *c, int cc_no)
{
    return (((int)c->cc[cc_no]) << 7) + c->cc[cc_no + 32];
}

#define FOREACH_VOICE(var, p) \
    for (struct sampler_voice *p = (var), *p##_next = NULL; p && (p##_next = p->next, TRUE); p = p##_next)


#endif
