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
#include "config-api.h"
#include "errors.h"
#include "instr.h"
#include "layer.h"
#include "midi.h"
#include "module.h"
#include "scene.h"
#include <glib.h>

static gboolean cbox_scene_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_scene *s = ct->user_data;
    
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
    
    s->layer_count = 0;
    s->instrument_count = 0;
    
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
        
        cbox_scene_add_layer(s, l);        
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
    s->layer_count = 0;
    s->instrument_count = 0;
    cbox_command_target_init(&s->cmd_target, cbox_scene_process_cmd, s);
    s->transpose = 0;
    return s;
}

void cbox_scene_add_layer(struct cbox_scene *scene, struct cbox_layer *layer)
{
    int i;
    
    for (i = 0; i < scene->layer_count; i++)
    {
        if (scene->layers[i]->instrument == layer->instrument)
            break;
    }
    if (i == scene->layer_count)
        scene->instruments[scene->instrument_count++] = layer->instrument;
    scene->layers[scene->layer_count++] = layer;
}

void cbox_scene_destroy(struct cbox_scene *scene)
{
    int i;
    
    for (i = 0; i < scene->layer_count; i++)
    {
        free(scene->layers[i]);
    }
    free(scene);
}
