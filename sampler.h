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
    
    uint32_t pos, delta, loop_start, loop_end, cur_sample_end;
    uint32_t frac_pos, frac_delta;
    uint32_t loop_overlap;
    float loop_overlap_step;
    float lgain, rgain;
    float last_lgain, last_rgain;
};

struct sampler_voice
{
    struct sampler_voice *prev, *next;
    struct sampler_layer_data *layer;
    // Note: may be NULL when program is being deleted
    struct sampler_program *program;
    struct cbox_waveform *last_waveform;
    struct sampler_gen gen;
    int note;
    int vel;
    int released, released_with_sustain, released_with_sostenuto, captured_sostenuto;
    int off_by;
    int delay;
    int age;
    int play_count;
    float pitch_shift;
    float cutoff_shift;
    float gain_shift, gain_fromvel;
    struct cbox_biquadf_state filter_left, filter_right;
    struct cbox_biquadf_state filter_left2, filter_right2;
    struct cbox_biquadf_coeffs filter_coeffs;
    struct sampler_channel *channel;
    struct cbox_envelope amp_env, filter_env, pitch_env;
    struct sampler_lfo amp_lfo, filter_lfo, pitch_lfo;
    enum sampler_loop_mode loop_mode;
    int output_pair_no;
    int send1bus, send2bus;
    float send1gain, send2gain;
    int serial_no;
    struct cbox_envelope_shape dyn_envs[3]; // amp, filter, pitch
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
};

extern GQuark cbox_sampler_error_quark(void);

extern gboolean sampler_select_program(struct sampler_module *m, int channel, const gchar *preset, GError **error);
extern void sampler_update_layer(struct sampler_module *m, struct sampler_layer *l);
extern void sampler_update_program_layers(struct sampler_module *m, struct sampler_program *prg);
extern void sampler_unselect_program(struct sampler_module *m, struct sampler_program *prg);
// This function may only be called from RT thread!
extern void sampler_channel_set_program(struct sampler_channel *c, struct sampler_program *prg);

#endif
