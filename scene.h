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

#define MAX_AUXBUSES_PER_SCENE 8

struct cbox_aux_bus;
struct cbox_instrument;

struct cbox_scene
{
    struct cbox_command_target cmd_target;
    gchar *name;
    gchar *title;
    
    struct cbox_layer **layers;
    int layer_count;
    struct cbox_instrument **instruments;
    int instrument_count;
    struct cbox_aux_bus *aux_buses[MAX_AUXBUSES_PER_SCENE];
    int aux_bus_count;
    int transpose;
};

extern struct cbox_scene *cbox_scene_new();
extern gboolean cbox_scene_add_layer(struct cbox_scene *scene, struct cbox_layer *layer, GError **error);
extern gboolean cbox_scene_insert_layer(struct cbox_scene *scene, struct cbox_layer *layer, int pos, GError **error);
extern struct cbox_layer *cbox_scene_remove_layer(struct cbox_scene *scene, int pos);
extern void cbox_scene_move_layer(struct cbox_scene *scene, int oldpos, int newpos);
extern struct cbox_scene *cbox_scene_load(const char *section, GError **error);
extern gboolean cbox_scene_remove_instrument(struct cbox_scene *scene, struct cbox_instrument *instrument);
extern struct cbox_aux_bus *cbox_scene_get_aux_bus(struct cbox_scene *scene, const char *name, GError **error);
extern void cbox_scene_destroy(struct cbox_scene *scene);

#endif
