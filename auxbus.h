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

#ifndef CBOX_AUXBUS_H
#define CBOX_AUXBUS_H

#include "dom.h"
#include "module.h"

struct cbox_scene;

CBOX_EXTERN_CLASS(cbox_aux_bus)

struct cbox_aux_bus
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    struct cbox_scene *owner;

    gchar *name;
    struct cbox_module *module;
    int refcount;
    
    float *input_bufs[2];
    float *output_bufs[2];
};

extern struct cbox_aux_bus *cbox_aux_bus_load(struct cbox_scene *scene, const char *name, struct cbox_rt *rt, GError **error);
extern void cbox_aux_bus_ref(struct cbox_aux_bus *bus);
extern void cbox_aux_bus_unref(struct cbox_aux_bus *bus);

#endif
