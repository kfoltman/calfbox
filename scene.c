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

#include "app.h"
#include "auxbus.h"
#include "config-api.h"
#include "errors.h"
#include "instr.h"
#include "layer.h"
#include "midi.h"
#include "module.h"
#include "scene.h"
#include <assert.h>
#include <glib.h>

static gboolean cbox_layer_process_cmd(struct cbox_layer *layer, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, GError **error)
{
    if (!strcmp(subcmd, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!(cbox_execute_on(fb, NULL, "/enable", "i", error, layer->enabled) && 
            cbox_execute_on(fb, NULL, "/instrument_name", "s", error, layer->instrument->module->instance_name)))
            return FALSE;
        return TRUE;
    }
    else if (!strcmp(subcmd, "/enable") && !strcmp(cmd->arg_types, "i"))
    {
        layer->enabled = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else // otherwise, treat just like an command on normal (non-aux) output
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
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
        struct cbox_scene *scene = cbox_scene_load((const gchar *)cmd->arg_values[0], error);
        if (!scene)
            return FALSE;
        struct cbox_scene *old_scene = cbox_rt_set_scene(app.rt, scene);
        cbox_scene_destroy(old_scene);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load_layer") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_layer *layer = cbox_layer_load((const gchar *)cmd->arg_values[0], error);
        if (!layer)
            return FALSE;
        struct cbox_scene *scene = cbox_scene_new();
        if (!scene) // not really expected
            return FALSE;
        if (!cbox_scene_add_layer(scene, layer, error))
        {
            cbox_scene_destroy(scene);
            cbox_layer_destroy(layer);
            return FALSE;
        }
        struct cbox_scene *old_scene = cbox_rt_set_scene(app.rt, scene);
        cbox_scene_destroy(old_scene);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load_instrument") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_layer *layer = cbox_layer_new((const gchar *)cmd->arg_values[0], error);
        if (!layer)
            return FALSE;
        struct cbox_scene *scene = cbox_scene_new();
        if (!scene) // not really expected
            return FALSE;
        if (!cbox_scene_add_layer(scene, layer, error))
        {
            cbox_scene_destroy(scene);
            cbox_layer_destroy(layer);
            return FALSE;
        }
        struct cbox_scene *old_scene = cbox_rt_set_scene(app.rt, scene);
        cbox_scene_destroy(old_scene);
        return TRUE;
    }
    else if (cbox_parse_path_part(cmd, "/layer/", &subcommand, &index, 1, s->layer_count, error))
    {
        if (!subcommand)
            return FALSE;
        return cbox_layer_process_cmd(s->layers[index - 1], fb, cmd, subcommand, error);
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

struct cbox_scene *cbox_scene_load(const char *name, GError **error)
{
    struct cbox_scene *s = malloc(sizeof(struct cbox_scene));
    const char *cv = NULL;
    int i;
    gchar *section = g_strdup_printf("scene:%s", name);
    
    if (!cbox_config_has_section(section))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No config section for scene '%s'", name);
        goto error;
    }
    
    s->layers = NULL;
    s->layer_count = 0;
    s->instrument_count = 0;
    s->aux_bus_count = 0;
    
    for (i = 1; ; i++)
    {
        struct cbox_layer *l = NULL;
        int j;
        
        gchar *sn = g_strdup_printf("layer%d", i);
        cv = cbox_config_get_string(section, sn);
        g_free(sn);
        
        if (!cv)
            break;
        
        l = cbox_layer_load(cv, error);
        if (!l)
        {
            cbox_scene_destroy(s);
            return NULL;
        }
        
        if (!cbox_scene_add_layer(s, l, error))
        {
            cbox_scene_destroy(s);
            return NULL;
        }
    }
    
    s->transpose = cbox_config_get_int(section, "transpose", 0);
    s->title = g_strdup(cbox_config_get_string_with_default(section, "title", ""));
    g_free(section);
    cbox_command_target_init(&s->cmd_target, cbox_scene_process_cmd, s);
    s->name = g_strdup(name);
    return s;

error:
    g_free(section);
    free(s);
    return NULL;
}

struct cbox_scene *cbox_scene_new()
{
    struct cbox_scene *s = malloc(sizeof(struct cbox_scene));
    s->name = g_strdup("");
    s->title = g_strdup("");
    s->layers = NULL;
    s->layer_count = 0;
    s->instrument_count = 0;
    s->aux_bus_count = 0;
    cbox_command_target_init(&s->cmd_target, cbox_scene_process_cmd, s);
    s->transpose = 0;
    return s;
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
        scene->instruments[scene->instrument_count++] = layer->instrument;
        layer->instrument->scene = scene;
    }
    struct cbox_layer **layers = malloc(sizeof(struct cbox_layer *) * (scene->layer_count + 1));
    memcpy(layers, scene->layers, sizeof(struct cbox_layer *) * pos);
    layers[pos] = layer;
    memcpy(layers + pos + 1, scene->layers + pos, sizeof(struct cbox_layer *) * (scene->layer_count - pos));
    
    free(cbox_rt_swap_pointers(app.rt, (void **)&scene->layers, layers));
    scene->layer_count++;
    return TRUE;
}

gboolean cbox_scene_add_layer(struct cbox_scene *scene, struct cbox_layer *layer, GError **error)
{
    return cbox_scene_insert_layer(scene, layer, scene->layer_count, error);
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
    if (scene->aux_bus_count == MAX_AUXBUSES_PER_SCENE)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Aux bus limit exceeded for effect '%s'", name);
        return NULL;
    }
    struct cbox_aux_bus *bus = cbox_aux_bus_load(name, error);
    if (!bus)
        return NULL;
    bus->refcount++;
    scene->aux_buses[scene->aux_bus_count++] = bus;
    return bus;
}

void cbox_scene_destroy(struct cbox_scene *scene)
{
    int i;
    for (i = 0; i < scene->layer_count; i++)
        cbox_layer_destroy(scene->layers[i]);
    for (i = 0; i < scene->instrument_count; i++)
    {
        for (int j = 0; j < scene->instruments[i]->aux_output_count; j++)
        {
            if (scene->instruments[i]->aux_outputs[j])
                cbox_aux_bus_unref(scene->instruments[i]->aux_outputs[j]);
        }
    }
            
    for (int i = 0; i < scene->aux_bus_count; i++)
        cbox_aux_bus_destroy(scene->aux_buses[i]);
    free(scene);
}
