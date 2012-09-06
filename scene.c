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
#include "config-api.h"
#include "errors.h"
#include "instr.h"
#include "layer.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "scene.h"
#include <assert.h>
#include <glib.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_scene)

static gboolean cbox_layer_process_cmd(struct cbox_layer *layer, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!(cbox_execute_on(fb, NULL, "/enable", "i", error, (int)layer->enabled) && 
            cbox_execute_on(fb, NULL, "/instrument_name", "s", error, layer->instrument->module->instance_name) && 
            cbox_execute_on(fb, NULL, "/consume", "i", error, (int)layer->consume) && 
            cbox_execute_on(fb, NULL, "/ignore_scene_transpose", "i", error, (int)layer->ignore_scene_transpose) && 
            cbox_execute_on(fb, NULL, "/disable_aftertouch", "i", error, (int)layer->disable_aftertouch) && 
            cbox_execute_on(fb, NULL, "/transpose", "i", error, (int)layer->transpose) && 
            cbox_execute_on(fb, NULL, "/fixed_note", "i", error, (int)layer->fixed_note) && 
            cbox_execute_on(fb, NULL, "/low_note", "i", error, (int)layer->low_note) && 
            cbox_execute_on(fb, NULL, "/high_note", "i", error, (int)layer->high_note) && 
            cbox_execute_on(fb, NULL, "/in_channel", "i", error, layer->in_channel + 1) && 
            cbox_execute_on(fb, NULL, "/out_channel", "i", error, layer->out_channel + 1)))
            return FALSE;
        return TRUE;
    }
    else if (!strcmp(subcmd, "/enable") && !strcmp(cmd->arg_types, "i"))
    {
        layer->enabled = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/consume") && !strcmp(cmd->arg_types, "i"))
    {
        layer->consume = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/ignore_scene_transpose") && !strcmp(cmd->arg_types, "i"))
    {
        layer->ignore_scene_transpose = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/disable_aftertouch") && !strcmp(cmd->arg_types, "i"))
    {
        layer->disable_aftertouch = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/transpose") && !strcmp(cmd->arg_types, "i"))
    {
        layer->transpose = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/fixed_note") && !strcmp(cmd->arg_types, "i"))
    {
        layer->fixed_note = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/low_note") && !strcmp(cmd->arg_types, "i"))
    {
        layer->low_note = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/high_note") && !strcmp(cmd->arg_types, "i"))
    {
        layer->low_note = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(subcmd, "/in_channel") && !strcmp(cmd->arg_types, "i"))
    {
        layer->in_channel = *(int *)cmd->arg_values[0] - 1;
        return TRUE;
    }
    else if (!strcmp(subcmd, "/out_channel") && !strcmp(cmd->arg_types, "i"))
    {
        layer->out_channel = *(int *)cmd->arg_values[0] - 1;
        return TRUE;
    }
    else // otherwise, treat just like an command on normal (non-aux) output
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

static gboolean cbox_aux_bus_process_cmd(struct cbox_aux_bus *aux_bus, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        return cbox_module_slot_process_cmd(&aux_bus->module, fb, cmd, subcmd, error);
    }
    else 
        return cbox_module_slot_process_cmd(&aux_bus->module, fb, cmd, subcmd, error);
}

static gboolean cbox_scene_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_scene *s = ct->user_data;
    const char *subcommand = NULL;
    int index = 0;
    
    if (!strcmp(cmd->command, "/transpose") && !strcmp(cmd->arg_types, "i"))
    {
        s->transpose = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_scene *scene = CBOX_CREATE_OTHER_CLASS(s, cbox_scene);
        if (!scene)
            return FALSE;
        if (!cbox_scene_load(scene, (const gchar *)cmd->arg_values[0], error))
            return FALSE;
        struct cbox_scene *old_scene = cbox_rt_set_scene(s->rt, scene);
        CBOX_DELETE(old_scene);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/new") && !strcmp(cmd->arg_types, ""))
    {
        struct cbox_scene *scene = CBOX_CREATE_OTHER_CLASS(s, cbox_scene);
        if (!scene) // not really expected
            return FALSE;
        struct cbox_scene *old_scene = cbox_rt_set_scene(s->rt, scene);
        CBOX_DELETE(old_scene);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/add_layer") && !strcmp(cmd->arg_types, "is"))
    {
        int pos = *(int *)cmd->arg_values[0];
        if (pos < 0 || pos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d or 0 for append)", pos, s->layer_count);
            return FALSE;
        }
        if (pos == 0)
            pos = s->layer_count;
        else
            pos--;
        struct cbox_layer *layer = cbox_layer_load(s->rt, (const gchar *)cmd->arg_values[1], error);
        if (!layer)
            return FALSE;
        if (!cbox_scene_insert_layer(s, layer, pos, error))
        {
            cbox_layer_destroy(layer);
            return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/add_instrument") && !strcmp(cmd->arg_types, "is"))
    {
        int pos = *(int *)cmd->arg_values[0];
        if (pos < 0 || pos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d or 0 for append)", pos, s->layer_count);
            return FALSE;
        }
        if (pos == 0)
            pos = s->layer_count;
        else
            pos--;
        struct cbox_layer *layer = cbox_layer_new(s->rt, (const gchar *)cmd->arg_values[1], error);
        if (!layer)
            return FALSE;
        if (!cbox_scene_insert_layer(s, layer, pos, error))
        {
            cbox_layer_destroy(layer);
            return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/delete_layer") && !strcmp(cmd->arg_types, "i"))
    {
        int pos = *(int *)cmd->arg_values[0];
        if (pos < 0 || pos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d or 0 for last)", pos, s->layer_count);
            return FALSE;
        }
        if (pos == 0)
            pos = s->layer_count - 1;
        else
            pos--;
        cbox_layer_destroy(cbox_scene_remove_layer(s, pos));
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/move_layer") && !strcmp(cmd->arg_types, "ii"))
    {
        int oldpos = *(int *)cmd->arg_values[0];
        if (oldpos < 1 || oldpos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d)", oldpos, s->layer_count);
            return FALSE;
        }
        int newpos = *(int *)cmd->arg_values[1];
        if (newpos < 1 || newpos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d)", newpos, s->layer_count);
            return FALSE;
        }
        cbox_scene_move_layer(s, oldpos - 1, newpos - 1);
        return TRUE;
    }
    else if (cbox_parse_path_part(cmd, "/layer/", &subcommand, &index, 1, s->layer_count, error))
    {
        if (!subcommand)
            return FALSE;
        return cbox_layer_process_cmd(s->layers[index - 1], fb, cmd, subcommand, error);
    }
    else if (cbox_parse_path_part(cmd, "/aux/", &subcommand, &index, 1, s->aux_bus_count, error))
    {
        if (!subcommand)
            return FALSE;
        return cbox_aux_bus_process_cmd(s->aux_buses[index - 1], fb, cmd, subcommand, error);
    }
    else if (!strcmp(cmd->command, "/load_aux") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_aux_bus *bus = cbox_scene_get_aux_bus(s, (const char *)cmd->arg_values[0], error);
        if (!bus)
            return FALSE;
        cbox_aux_bus_unref(bus);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/delete_aux") && !strcmp(cmd->arg_types, "i"))
    {
        int pos = *(int *)cmd->arg_values[0];
        if (pos < 0 || pos > s->aux_bus_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d or 0 for last)", pos, s->aux_bus_count);
            return FALSE;
        }
        if (pos == 0)
            pos = s->aux_bus_count - 1;
        else
            pos--;
        cbox_aux_bus_destroy(cbox_scene_remove_aux_bus(s, pos));
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!cbox_execute_on(fb, NULL, "/name", "s", error, s->name) || 
            !cbox_execute_on(fb, NULL, "/title", "s", error, s->title) ||
            !cbox_execute_on(fb, NULL, "/transpose", "i", error, s->transpose))
            return FALSE;
        
        for (int i = 0; i < s->layer_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/layer", "i", error, i + 1))
                return FALSE;
        }
        for (int i = 0; i < s->instrument_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/instrument", "ss", error, s->instruments[i]->module->instance_name, s->instruments[i]->module->engine_name))
                return FALSE;
        }
        for (int i = 0; i < s->aux_bus_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/aux", "is", error, i + 1, s->aux_buses[i]->name))
                return FALSE;
        }
        return TRUE;
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

gboolean cbox_scene_load(struct cbox_scene *s, const char *name, GError **error)
{
    const char *cv = NULL;
    int i;
    gchar *section = g_strdup_printf("scene:%s", name);
    
    if (!cbox_config_has_section(section))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No config section for scene '%s'", name);
        goto error;
    }
    
    cbox_scene_clear(s);
    
    assert(s->layers == NULL);
    assert(s->instruments == NULL);
    assert(s->aux_buses == NULL);
    assert(s->layer_count == 0);
    assert(s->instrument_count == 0);
    assert(s->aux_bus_count == 0);
    
    for (i = 1; ; i++)
    {
        struct cbox_layer *l = NULL;
        int j;
        
        gchar *sn = g_strdup_printf("layer%d", i);
        cv = cbox_config_get_string(section, sn);
        g_free(sn);
        
        if (!cv)
            break;
        
        l = cbox_layer_load(s->rt, cv, error);
        if (!l)
            goto error;
        
        if (!cbox_scene_add_layer(s, l, error))
            goto error;
    }
    
    s->transpose = cbox_config_get_int(section, "transpose", 0);
    s->title = g_strdup(cbox_config_get_string_with_default(section, "title", ""));
    g_free(section);
    cbox_command_target_init(&s->cmd_target, cbox_scene_process_cmd, s);
    s->name = g_strdup(name);
    return TRUE;

error:
    g_free(section);
    return FALSE;
}

gboolean cbox_scene_insert_layer(struct cbox_scene *scene, struct cbox_layer *layer, int pos, GError **error)
{
    int i;
    
    struct cbox_instrument *instrument = layer->instrument;
    for (i = 0; i < instrument->aux_output_count; i++)
    {
        assert(!instrument->aux_outputs[i]);
        if (instrument->aux_output_names[i])
        {
            instrument->aux_outputs[i] = cbox_scene_get_aux_bus(scene, instrument->aux_output_names[i], error);
            if (!instrument->aux_outputs[i])
                return FALSE;
        }
    }
    for (i = 0; i < scene->layer_count; i++)
    {
        if (scene->layers[i]->instrument == layer->instrument)
            break;
    }
    if (i == scene->layer_count)
    {
        struct cbox_instrument **instruments = malloc(sizeof(struct cbox_instrument *) * (scene->instrument_count + 1));
        memcpy(instruments, scene->instruments, sizeof(struct cbox_instrument *) * scene->instrument_count);
        instruments[scene->instrument_count] = layer->instrument;
        layer->instrument->scene = scene;
        free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->instruments, instruments, &scene->instrument_count, scene->instrument_count + 1));        
    }
    struct cbox_layer **layers = malloc(sizeof(struct cbox_layer *) * (scene->layer_count + 1));
    memcpy(layers, scene->layers, sizeof(struct cbox_layer *) * pos);
    layers[pos] = layer;
    memcpy(layers + pos + 1, scene->layers + pos, sizeof(struct cbox_layer *) * (scene->layer_count - pos));
    
    free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->layers, layers, &scene->layer_count, scene->layer_count + 1));
    return TRUE;
}

gboolean cbox_scene_add_layer(struct cbox_scene *scene, struct cbox_layer *layer, GError **error)
{
    return cbox_scene_insert_layer(scene, layer, scene->layer_count, error);
}

struct cbox_layer *cbox_scene_remove_layer(struct cbox_scene *scene, int pos)
{
    struct cbox_layer *removed = scene->layers[pos];
    struct cbox_layer **layers = malloc(sizeof(struct cbox_layer *) * (scene->layer_count - 1));
    memcpy(layers, scene->layers, sizeof(struct cbox_layer *) * pos);
    memcpy(layers + pos, scene->layers + pos + 1, sizeof(struct cbox_layer *) * (scene->layer_count - pos - 1));
    free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->layers, layers, &scene->layer_count, scene->layer_count - 1));
    cbox_instrument_unref_aux_buses(removed->instrument);
    
    return removed;
}

void cbox_scene_move_layer(struct cbox_scene *scene, int oldpos, int newpos)
{
    if (oldpos == newpos)
        return;
    struct cbox_layer **layers = malloc(sizeof(struct cbox_layer *) * scene->layer_count);
    for (int i = 0; i < scene->layer_count; i++)
    {
        int s;
        if (i == newpos)
            s = oldpos;
        else
        {
            if (oldpos < newpos)
                s = (i < oldpos || i > newpos) ? i : i + 1;
            else
                s = (i < newpos || i > oldpos) ? i : i - 1;
        }
        layers[i] = scene->layers[s];
    }
    free(cbox_rt_swap_pointers(scene->rt, (void **)&scene->layers, layers));
}

gboolean cbox_scene_remove_instrument(struct cbox_scene *scene, struct cbox_instrument *instrument)
{
    assert(instrument->scene == scene);
    int pos;
    for (pos = 0; pos < scene->instrument_count; pos++)
    {
        if (scene->instruments[pos] == instrument)
        {
            struct cbox_instrument **instruments = malloc(sizeof(struct cbox_instrument *) * (scene->instrument_count - 1));
            memcpy(instruments, scene->instruments, sizeof(struct cbox_instrument *) * pos);
            memcpy(instruments + pos, scene->instruments + pos + 1, sizeof(struct cbox_instrument *) * (scene->instrument_count - pos - 1));
            free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->instruments, instruments, &scene->instrument_count, scene->instrument_count - 1));
            
            instrument->scene = NULL;
            return TRUE;
        }
    }
    return FALSE;
}

gboolean cbox_scene_insert_aux_bus(struct cbox_scene *scene, struct cbox_aux_bus *aux_bus)
{
    struct cbox_aux_bus **aux_buses = malloc(sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count + 1));
    memcpy(aux_buses, scene->aux_buses, sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count));
    aux_buses[scene->aux_bus_count] = aux_bus;
    free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->aux_buses, aux_buses, &scene->aux_bus_count, scene->aux_bus_count + 1));
    return TRUE;
}

struct cbox_aux_bus *cbox_scene_remove_aux_bus(struct cbox_scene *scene, int pos)
{
    struct cbox_aux_bus *removed = scene->aux_buses[pos];
    for (int i = 0; i < scene->instrument_count; i++)
        cbox_instrument_disconnect_aux_bus(scene->instruments[i], removed);
    
    struct cbox_aux_bus **aux_buses = malloc(sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count - 1));
    memcpy(aux_buses, scene->aux_buses, sizeof(struct cbox_aux_bus *) * pos);
    memcpy(aux_buses + pos, scene->aux_buses + pos + 1, sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count - pos - 1));
    free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->aux_buses, aux_buses, &scene->aux_bus_count, scene->aux_bus_count - 1));
    
    return removed;
}

struct cbox_aux_bus *cbox_scene_get_aux_bus(struct cbox_scene *scene, const char *name, GError **error)
{
    for (int i = 0; i < scene->aux_bus_count; i++)
    {
        if (!strcmp(scene->aux_buses[i]->name, name))
        {
            scene->aux_buses[i]->refcount++;
            return scene->aux_buses[i];
        }
    }
    struct cbox_aux_bus *bus = cbox_aux_bus_load(name, scene->rt, error);
    if (!bus)
        return NULL;
    cbox_scene_insert_aux_bus(scene, bus);
    bus->refcount++;
    return bus;
}

void cbox_scene_clear(struct cbox_scene *scene)
{
    int i;
    g_free(scene->name);
    g_free(scene->title);
    scene->name = g_strdup("");
    scene->title = g_strdup("");
    while(scene->layer_count > 0)
        cbox_layer_destroy(cbox_scene_remove_layer(scene, 0));
            
    while(scene->aux_bus_count > 0)
        cbox_aux_bus_destroy(scene->aux_buses[--scene->aux_bus_count]);
}

struct cbox_objhdr *cbox_scene_newfunc(struct cbox_class *class_ptr, struct cbox_document *document)
{
    struct cbox_scene *s = malloc(sizeof(struct cbox_scene));
    CBOX_OBJECT_HEADER_INIT(s, cbox_scene, document);
    s->rt = (struct cbox_rt *)cbox_document_get_service(document, "rt");
    s->name = g_strdup("");
    s->title = g_strdup("");
    s->layers = NULL;
    s->aux_buses = NULL;
    s->instruments = NULL;
    s->layer_count = 0;
    s->instrument_count = 0;
    s->aux_bus_count = 0;
    cbox_command_target_init(&s->cmd_target, cbox_scene_process_cmd, s);
    s->transpose = 0;
    CBOX_RETURN_OBJECT(s);
}

static void cbox_scene_destroyfunc(struct cbox_objhdr *scene_obj)
{
    struct cbox_scene *scene = (struct cbox_scene *)scene_obj;
    cbox_scene_clear(scene);
    g_free(scene->name);
    g_free(scene->title);
    assert(scene->instrument_count == 0);
    free(scene->layers);
    free(scene->aux_buses);
    free(scene->instruments);
    free(scene);
}
