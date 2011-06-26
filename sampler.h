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

#ifndef CBOX_SAMPLER_H
#define CBOX_SAMPLER_H

#include "biquad-float.h"
#include "envelope.h"
#include "module.h"
#include <glib.h>
#include <sndfile.h>
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

struct sampler_layer
{
    enum sample_player_type mode;
    struct sampler_waveform *waveform;
    int16_t *sample_data;
    uint32_t sample_offset;
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t sample_end;
    float gain;
    float pan;
    float freq;
    float tune;
    float note_scaling;
    int min_note, max_note, root_note;
    int min_vel, max_vel;
    int transpose;
    float cutoff, resonance, fileg_depth, pitcheg_depth;
    struct cbox_dahdsr amp_env, filter_env, pitch_env;
    struct cbox_envelope_shape amp_env_shape, filter_env_shape, pitch_env_shape;
    enum sample_loop_mode loop_mode;
    float velcurve[128];
    int velcurve_quadratic, fil_veltrack;
    int exclusive_group, off_by;
};

struct sampler_program
{
    gchar *name;
    int prog_no;
    struct sampler_layer **layers;
    int layer_count;
};

struct sampler_channel
{
    float pitchbend;
    float pbrange;
    int sustain, sostenuto;
    int volume, pan, expression, modulation, cutoff_ctl, resonance_ctl;
    struct sampler_program *program;
};

struct sampler_voice
{
    enum sample_player_type mode;
    struct sampler_layer *layer;
    int16_t *sample_data;
    uint32_t pos, delta, loop_start, loop_end, sample_end;
    uint32_t frac_pos, frac_delta;
    int note;
    int vel;
    int released, released_with_sustain, released_with_sostenuto, captured_sostenuto;
    int off_by;
    float freq;
    float gain;
    float pan;
    float lgain, rgain;
    float last_lgain, last_rgain;
    float cutoff, resonance, fileg_depth, pitcheg_depth;
    struct cbox_biquadf_state filter_left, filter_right;
    struct cbox_biquadf_coeffs filter_coeffs;
    struct sampler_channel *channel;
    struct cbox_envelope amp_env, filter_env, pitch_env;
    enum sample_loop_mode loop_mode;
};

struct sampler_waveform
{
    int16_t *data;
    SF_INFO info;
};

struct sampler_module
{
    struct cbox_module module;

    int srate;
    struct sampler_voice voices[MAX_SAMPLER_VOICES];
    struct sampler_channel channels[16];
    struct sampler_program **programs;
    int program_count;
};

extern void sampler_layer_init(struct sampler_layer *l);
extern void sampler_layer_set_waveform(struct sampler_layer *l, struct sampler_waveform *waveform);
extern void sampler_load_layer_overrides(struct sampler_layer *l, struct sampler_module *m, const char *cfg_section);
extern void sampler_layer_finalize(struct sampler_layer *l, struct sampler_module *m);
extern struct sampler_waveform *sampler_waveform_new_from_file(const char *context_name, const char *filename, GError **error);
extern GQuark cbox_sampler_error_quark();

#endif
