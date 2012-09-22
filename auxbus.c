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
#include "scene.h"
#include <assert.h>
#include <glib.h>

extern gboolean cbox_scene_insert_aux_bus(struct cbox_scene *scene, struct cbox_aux_bus *aux_bus);
extern void cbox_scene_remove_aux_bus(struct cbox_scene *scene, struct cbox_aux_bus *bus);

CBOX_CLASS_DEFINITION_ROOT(cbox_aux_bus)

static gboolean cbox_aux_bus_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_aux_bus *aux_bus = ct->user_data;
    struct cbox_rt *rt = (struct cbox_rt *)cbox_document_get_service(CBOX_GET_DOCUMENT(aux_bus), "rt");
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        return cbox_execute_on(fb, NULL, "/name", "s", error, aux_bus->name) && 
            CBOX_OBJECT_DEFAULT_STATUS(aux_bus, fb, error);
    }
    else 
    if (!strncmp(cmd->command, "/slot/", 6) && !strcmp(cmd->arg_types, ""))
    {
        return cbox_module_slot_process_cmd(&aux_bus->module, fb, cmd, cmd->command + 5, rt, error);
    }
    else 
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

struct cbox_aux_bus *cbox_aux_bus_load(struct cbox_scene *scene, const char *name, struct cbox_rt *rt, GError **error)
{
    struct cbox_module *module = cbox_module_new_from_fx_preset(name, rt, error);
    if (!module)
        return NULL;
    
    struct cbox_aux_bus *p = malloc(sizeof(struct cbox_aux_bus));    
    CBOX_OBJECT_HEADER_INIT(p, cbox_aux_bus, CBOX_GET_DOCUMENT(scene));
    cbox_command_target_init(&p->cmd_target, cbox_aux_bus_process_cmd, p);
    p->name = g_strdup(name);
    p->owner = scene;
    p->module = module;
    p->refcount = 0;
    // XXXKF this work up to buffer size of 8192 floats, this should be determined from JACK settings and updated when
    // JACK buffer size changes
    p->input_bufs[0] = malloc(8192 * sizeof(float));
    p->input_bufs[1] = malloc(8192 * sizeof(float));
    p->output_bufs[0] = malloc(8192 * sizeof(float));
    p->output_bufs[1] = malloc(8192 * sizeof(float));
    CBOX_OBJECT_REGISTER(p);
    cbox_scene_insert_aux_bus(scene, p);
    
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

void cbox_aux_bus_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_aux_bus *bus = CBOX_H2O(objhdr);
    if (bus->owner)
    {
        cbox_scene_remove_aux_bus(bus->owner, bus);
        bus->owner = NULL;
    }
    CBOX_DELETE(bus->module);
    bus->module = NULL;
    assert(!bus->refcount);
    g_free(bus->name);
    free(bus->input_bufs[0]);
    free(bus->input_bufs[1]);
    free(bus);
}

