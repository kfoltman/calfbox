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

struct sampler_channel;

CBOX_EXTERN_CLASS(sampler_program)

// Runtime layer lists; in future, I might something more clever, like a tree
struct sampler_rll
{
    GSList *layers; // of sampler_layer
    GSList *layers_release; // of sampler_layer
    GSList *layers_oncc;
    uint32_t cc_trigger_bitmask[4]; // one bit per CC
};

struct sampler_ctrlinit
{
    uint8_t controller;
    uint8_t value;
};

union sampler_ctrlinit_union {
    gpointer ptr;
    struct sampler_ctrlinit cinit;
} u;


struct sampler_program
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;

    struct sampler_module *module;
    gchar *name;
    int prog_no;
    struct sampler_layer *default_group;
    GSList *groups;
    GSList *all_layers;
    GSList *ctrl_init_list;
    struct sampler_rll *rll;
    gchar *sample_dir; // can be empty, cannot be NULL
    gchar *source_file; // can be empty, cannot be NULL
    int in_use;
    gboolean deleting;
};

extern struct sampler_rll *sampler_rll_new_from_program(struct sampler_program *prg);
extern void sampler_rll_destroy(struct sampler_rll *rll);

extern GSList *sampler_program_get_next_layer(struct sampler_program *prg, struct sampler_channel *c, GSList *next_layer, int note, int vel, float random);
extern struct sampler_program *sampler_program_new(struct sampler_module *m, int prog_no, const char *name, const char *sample_dir);
extern struct sampler_program *sampler_program_new_from_cfg(struct sampler_module *m, const char *cfg_section, const char *name, int pgm_id, GError **error);
extern void sampler_program_add_layer(struct sampler_program *prg, struct sampler_layer *l);
extern void sampler_program_delete_layer(struct sampler_program *prg, struct sampler_layer *l);
extern void sampler_program_add_group(struct sampler_program *prg, struct sampler_layer *l);
extern void sampler_program_add_controller_init(struct sampler_program *prg, uint8_t controller, uint8_t value);
extern void sampler_program_remove_controller_init(struct sampler_program *prg, uint8_t controller, int which);
extern void sampler_program_update_layers(struct sampler_program *prg);

#endif
