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

#ifndef CBOX_SAMPLER_PRG_H
#define CBOX_SAMPLER_PRG_H

#include "cmd.h"
#include "dom.h"

struct cbox_tarfile;
struct sampler_channel;

CBOX_EXTERN_CLASS(sampler_program)

#define MAX_MIDI_CURVES 32

struct sampler_keyswitch_group
{
    uint8_t lo, hi, num_used, def_value;
    uint32_t group_offset;
    uint8_t key_offsets[];
};

// Runtime layer lists; in future, I might something more clever, like a tree
struct sampler_rll
{
    GSList *layers_oncc;
    uint32_t cc_trigger_bitmask[4]; // one bit per CC
    uint8_t lokey, hikey;
    uint8_t ranges_by_key[128];
    uint32_t layers_by_range_count;
    GSList **layers_by_range, **release_layers_by_range, **key_release_layers_by_range;
    struct sampler_keyswitch_group **keyswitch_groups;
    uint32_t keyswitch_group_count;
    uint32_t keyswitch_key_count;
    uint32_t num_release_layers, num_key_release_layers;
};

struct sampler_rll_iterator
{
    struct sampler_channel *channel;
    int note, vel;
    float random;
    gboolean is_first;
    enum sampler_trigger release_mode;
    GSList *next_layer;
    struct sampler_rll *rll;
    uint32_t next_keyswitch_index;
};

struct sampler_ctrlinit
{
    uint16_t controller;
    uint8_t value;
};

union sampler_ctrlinit_union {
    gpointer ptr;
    struct sampler_ctrlinit cinit;
};

struct sampler_ctrllabel {
    uint16_t controller;
    gchar *label;
};

struct sampler_pitchlabel {
    uint16_t pitch;
    gchar *label;
};

struct sampler_outputlabel {
    uint16_t pitch;
    gchar *label;
};

struct sampler_program
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;

    struct sampler_module *module;
    gchar *name;
    int prog_no;
    struct sampler_layer *global;
    GSList *all_layers;
    GSList *ctrl_init_list;
    GSList *ctrl_label_list;
    GSList *pitch_label_list;
    GSList *output_label_list;
    struct sampler_rll *rll;
    gchar *sample_dir; // can be empty, cannot be NULL
    gchar *source_file; // can be empty, cannot be NULL
    int in_use;
    struct cbox_tarfile *tarfile;
    gboolean deleting;
    gboolean auto_update_layers;
    struct sampler_midi_curve *curves[MAX_MIDI_CURVES];
    float *interpolated_curves[MAX_MIDI_CURVES];
};

extern struct sampler_rll *sampler_rll_new_from_program(struct sampler_program *prg);
extern void sampler_rll_destroy(struct sampler_rll *rll);

extern void sampler_rll_iterator_init(struct sampler_rll_iterator *iter, struct sampler_rll *rll, struct sampler_channel *c, int note, int vel, float random, gboolean is_first, enum sampler_trigger release_mode);
extern struct sampler_layer *sampler_rll_iterator_next(struct sampler_rll_iterator *iter);

extern struct sampler_program *sampler_program_new(struct sampler_module *m, int prog_no, const char *name, struct cbox_tarfile *tarfile, const char *sample_dir, GError **error);
extern struct sampler_program *sampler_program_new_from_cfg(struct sampler_module *m, const char *cfg_section, const char *name, int pgm_id, GError **error);
extern void sampler_program_add_layer(struct sampler_program *prg, struct sampler_layer *l);
extern void sampler_program_delete_layer(struct sampler_program *prg, struct sampler_layer *l);
extern void sampler_program_add_group(struct sampler_program *prg, struct sampler_layer *l);
extern void sampler_program_add_controller_init(struct sampler_program *prg, uint16_t controller, uint8_t value);
extern void sampler_program_add_controller_label(struct sampler_program *prg, uint16_t controller, gchar *label); // keeps ownership
extern void sampler_program_add_pitch_label(struct sampler_program *prg, uint16_t pitch, gchar *label); // keeps ownership
extern void sampler_program_add_output_label(struct sampler_program *prg, uint16_t pitch, gchar *label); // keeps ownership
extern void sampler_program_remove_controller_init(struct sampler_program *prg, uint16_t controller, int which);
extern void sampler_program_update_layers(struct sampler_program *prg);
extern struct sampler_program *sampler_program_clone(struct sampler_program *prg, struct sampler_module *m, int prog_no, GError **error);

#endif
