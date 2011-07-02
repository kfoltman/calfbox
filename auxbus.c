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

struct cbox_aux_bus *cbox_aux_bus_load(const char *name, GError **error)
{
    struct cbox_module *module = cbox_module_new_from_fx_preset(name, error);
    if (!module)
        return NULL;
    
    struct cbox_aux_bus *p = malloc(sizeof(struct cbox_aux_bus));    
    p->name = g_strdup(name);
    p->module = module;
    p->refcount = 0;
    
    return p;
}

void cbox_aux_bus_unref(struct cbox_aux_bus *bus)
{
    assert(bus->refcount > 0);
    if (!--bus->refcount)
    {
        cbox_module_destroy(bus->module);
        bus->module = NULL;
    }
}

void cbox_aux_bus_destroy(struct cbox_aux_bus *bus)
{
    assert(!bus->refcount);
    assert(!bus->module);
    g_free(bus->name);
    free(bus);
}

