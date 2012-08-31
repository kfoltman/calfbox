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

#include "auxbus.h"
#include <assert.h>
#include <glib.h>

struct cbox_aux_bus *cbox_aux_bus_load(const char *name, struct cbox_rt *rt, GError **error)
{
    struct cbox_module *module = cbox_module_new_from_fx_preset(name, rt, error);
    if (!module)
        return NULL;
    
    struct cbox_aux_bus *p = malloc(sizeof(struct cbox_aux_bus));    
    p->name = g_strdup(name);
    p->module = module;
    p->refcount = 0;
    // XXXKF this work up to buffer size of 8192 floats, this should be determined from JACK settings and updated when
    // JACK buffer size changes
    p->input_bufs[0] = malloc(8192 * sizeof(float));
    p->input_bufs[1] = malloc(8192 * sizeof(float));
    p->output_bufs[0] = malloc(8192 * sizeof(float));
    p->output_bufs[1] = malloc(8192 * sizeof(float));
    
    return p;
}

void cbox_aux_bus_ref(struct cbox_aux_bus *bus)
{
    ++bus->refcount;
}

void cbox_aux_bus_unref(struct cbox_aux_bus *bus)
{
    assert(bus->refcount > 0);
    --bus->refcount;
}

void cbox_aux_bus_destroy(struct cbox_aux_bus *bus)
{
    cbox_module_destroy(bus->module);
    bus->module = NULL;
    assert(!bus->refcount);
    g_free(bus->name);
    free(bus->input_bufs[0]);
    free(bus->input_bufs[1]);
    free(bus);
}

