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

#ifndef CBOX_LAYER_H
#define CBOX_LAYER_H

#include "dom.h"
#include "midi.h"
#include <glib.h>
#include <stdint.h>

struct cbox_module;
struct cbox_rt;

CBOX_EXTERN_CLASS(cbox_layer)

struct cbox_layer
{
    CBOX_OBJECT_HEADER()
    struct cbox_scene *scene;
    struct cbox_instrument *instrument;
    struct cbox_command_target cmd_target;
    gboolean enabled;
    int8_t in_channel; // -1 for Omni
    int8_t out_channel; // -1 for Omni
    uint8_t low_note;
    uint8_t high_note;
    int8_t transpose;
    int8_t fixed_note;
    gboolean disable_aftertouch;
    gboolean invert_sustain;
    gboolean consume;
    gboolean ignore_scene_transpose;
    gboolean ignore_program_changes;
    gboolean external_output_set;
    struct cbox_uuid external_output;
    struct cbox_midi_buffer output_buffer;
    struct cbox_midi_merger *external_merger;
};

extern struct cbox_layer *cbox_layer_new(struct cbox_scene *scene);
extern struct cbox_layer *cbox_layer_new_with_instrument(struct cbox_scene *scene, const char *instrument_name, GError **error);
extern struct cbox_layer *cbox_layer_new_from_config(struct cbox_scene *scene, const char *instrument_name, GError **error);
extern gboolean cbox_layer_load(struct cbox_layer *layer, const char *name, GError **error);
extern void cbox_layer_set_instrument(struct cbox_layer *layer, struct cbox_instrument *instrument);
extern void cbox_layer_destroy(struct cbox_layer *layer);

#endif
