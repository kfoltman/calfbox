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

#ifndef CBOX_SCENE_H
#define CBOX_SCENE_H

#include "cmd.h"
#include "dom.h"
#include "mididest.h"

CBOX_EXTERN_CLASS(cbox_scene)

struct cbox_aux_bus;
struct cbox_instrument;
struct cbox_midi_buffer;
struct cbox_recording_source;
struct cbox_song_playback;

struct cbox_scene
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    gchar *name;
    gchar *title;
    
    GHashTable *instrument_hash;
    struct cbox_rt *rt;
    struct cbox_layer **layers;
    int layer_count;
    struct cbox_instrument **instruments;
    int instrument_count;
    struct cbox_aux_bus **aux_buses;
    int aux_bus_count;
    int transpose;
    int owns_rt;
    struct cbox_song_playback *spb;
    struct cbox_midi_merger scene_input_merger;
    struct cbox_midi_buffer midibuf_total;

    struct cbox_recording_source *rec_mono_inputs, *rec_mono_outputs;
    struct cbox_recording_source *rec_stereo_inputs, *rec_stereo_outputs;
};

extern struct cbox_scene *cbox_scene_new(struct cbox_document *document, struct cbox_rt *rt, int owns_rt);
extern gboolean cbox_scene_add_layer(struct cbox_scene *scene, struct cbox_layer *layer, GError **error);
extern gboolean cbox_scene_insert_layer(struct cbox_scene *scene, struct cbox_layer *layer, int pos, GError **error);
extern struct cbox_layer *cbox_scene_remove_layer(struct cbox_scene *scene, int pos);
extern void cbox_scene_move_layer(struct cbox_scene *scene, int oldpos, int newpos);
extern gboolean cbox_scene_load(struct cbox_scene *scene, const char *section, GError **error);
extern gboolean cbox_scene_remove_instrument(struct cbox_scene *scene, struct cbox_instrument *instrument);
extern struct cbox_aux_bus *cbox_scene_get_aux_bus(struct cbox_scene *scene, const char *name, int allow_load, GError **error);
extern void cbox_scene_render(struct cbox_scene *scene, uint32_t nframes, float *output_buffers[]);
extern void cbox_scene_clear(struct cbox_scene *scene);
extern struct cbox_instrument *cbox_scene_get_instrument_by_name(struct cbox_scene *scene, const char *name, gboolean load, GError **error);
extern struct cbox_instrument *cbox_scene_create_instrument(struct cbox_scene *scene, const char *instrument_name, const char *engine_name, GError **error);


#endif
