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

#define MAX_LAYERS_PER_SCENE 16
#define MAX_MODULES_PER_SCENE 16

struct cbox_instrument;

struct cbox_scene
{
    struct cbox_layer *layers[MAX_LAYERS_PER_SCENE];
    int layer_count;
    struct cbox_instrument *instruments[MAX_MODULES_PER_SCENE];
    int instrument_count;
};

extern struct cbox_scene *cbox_scene_new();
extern void cbox_scene_add_layer(struct cbox_scene *scene, struct cbox_layer *layer);
extern struct cbox_scene *cbox_scene_load(const char *section, GError **error);
extern struct cbox_scene *cbox_scene_new_for_instrument(const char *name);
extern void cbox_scene_destroy(struct cbox_scene *scene);

#endif
