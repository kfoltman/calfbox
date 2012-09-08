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

#include "config-api.h"
#include "errors.h"
#include "instr.h"
#include "layer.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "scene.h"
#include <glib.h>

gboolean cbox_layer_load(struct cbox_layer *layer, struct cbox_instruments *instrument_mgr, const char *name, GError **error)
{
    const char *cv = NULL;
    struct cbox_instrument *instr = NULL;
    gchar *section = g_strdup_printf("layer:%s", name);
    
    if (!cbox_config_has_section(section))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Missing section for layer %s", name);
        goto error;
    }
    
    cv = cbox_config_get_string(section, "instrument");
    if (!cv)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Instrument not specified for layer %s", name);
        goto error;
    }
    instr = cbox_instruments_get_by_name(instrument_mgr, cv, TRUE, error);
    if (!instr)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Cannot get instrument %s for layer %s: ", cv, name);
        goto error;
    }

    layer->enabled = cbox_config_get_int(section, "enabled", TRUE);
    layer->low_note = 0;
    layer->high_note = 127;
    
    cv = cbox_config_get_string(section, "low_note");
    if (cv)
        layer->low_note = note_from_string(cv);
    
    cv = cbox_config_get_string(section, "high_note");
    if (cv)
        layer->high_note = note_from_string(cv);
    
    layer->transpose = cbox_config_get_int(section, "transpose", 0);
    layer->fixed_note = cbox_config_get_int(section, "fixed_note", -1);
    layer->in_channel = cbox_config_get_int(section, "in_channel", 0) - 1;
    layer->out_channel = cbox_config_get_int(section, "out_channel", 0) - 1;
    layer->disable_aftertouch = !cbox_config_get_int(section, "aftertouch", TRUE);
    layer->invert_sustain = cbox_config_get_int(section, "invert_sustain", FALSE);
    layer->consume = cbox_config_get_int(section, "consume", FALSE);
    layer->ignore_scene_transpose = cbox_config_get_int(section, "ignore_scene_transpose", FALSE);
    g_free(section);
    
    cbox_layer_set_instrument(layer, instr);    
    
    return 1;

error:
    cbox_instrument_destroy_if_unused(instr);

    g_free(section);
    return 0;
}

void cbox_layer_set_instrument(struct cbox_layer *layer, struct cbox_instrument *instrument)
{
    if (layer->instrument)
    {
        layer->instrument->refcount--;
        cbox_instrument_destroy_if_unused(layer->instrument);
        layer->instrument = NULL;
    }
    layer->instrument = instrument;
    layer->instrument->refcount++;
}

static gboolean cbox_layer_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

static struct cbox_objhdr *cbox_layer_newfunc(struct cbox_class *class_ptr, struct cbox_document *doc)
{
    struct cbox_layer *l = malloc(sizeof(struct cbox_layer));
    const char *cv = NULL;
    struct cbox_instrument *instr = NULL;
    
    CBOX_OBJECT_HEADER_INIT(l, cbox_layer, doc);
    cbox_command_target_init(&l->cmd_target, cbox_layer_process_cmd, l);
    l->enabled = TRUE;
    l->instrument = instr;
    l->low_note = 0;
    l->high_note = 127;
    
    l->transpose = 0;
    l->fixed_note = -1;
    l->in_channel = -1;
    l->out_channel = -1;
    l->disable_aftertouch = FALSE;
    l->invert_sustain = FALSE;
    l->consume = FALSE;
    l->ignore_scene_transpose = FALSE;
    
    return &l->_obj_hdr;
}

static void cbox_layer_destroyfunc(struct cbox_objhdr *layer_)
{
    struct cbox_layer *layer = (struct cbox_layer *)layer_;
    if (!--(layer->instrument->refcount))
    {
        if (layer->instrument->scene)
            cbox_scene_remove_instrument(layer->instrument->scene, layer->instrument);
        
        cbox_instrument_destroy(layer->instrument);
    }
    free(layer);
}

gboolean cbox_layer_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_layer *layer = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
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
    else if (!strcmp(cmd->command, "/enable") && !strcmp(cmd->arg_types, "i"))
    {
        layer->enabled = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/consume") && !strcmp(cmd->arg_types, "i"))
    {
        layer->consume = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/ignore_scene_transpose") && !strcmp(cmd->arg_types, "i"))
    {
        layer->ignore_scene_transpose = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/disable_aftertouch") && !strcmp(cmd->arg_types, "i"))
    {
        layer->disable_aftertouch = 0 != *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/transpose") && !strcmp(cmd->arg_types, "i"))
    {
        layer->transpose = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/fixed_note") && !strcmp(cmd->arg_types, "i"))
    {
        layer->fixed_note = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/low_note") && !strcmp(cmd->arg_types, "i"))
    {
        layer->low_note = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/high_note") && !strcmp(cmd->arg_types, "i"))
    {
        layer->high_note = *(int *)cmd->arg_values[0];
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/in_channel") && !strcmp(cmd->arg_types, "i"))
    {
        layer->in_channel = *(int *)cmd->arg_values[0] - 1;
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/out_channel") && !strcmp(cmd->arg_types, "i"))
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

CBOX_CLASS_DEFINITION_ROOT(cbox_layer)

struct cbox_layer *cbox_layer_new(struct cbox_scene *scene, const char *instrument_name, GError **error)
{
    struct cbox_instrument *instr;
    instr = cbox_instruments_get_by_name(scene->instrument_mgr, instrument_name, TRUE, error);
    if (!instr)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Cannot get instrument %s for new layer: ", instrument_name);
        goto error;
    }

    struct cbox_layer *layer = (struct cbox_layer *)CBOX_CREATE_OTHER(scene, cbox_layer);
    if (!layer) goto error;
    cbox_layer_set_instrument(layer, instr);
    return layer;

error:
    cbox_instrument_destroy_if_unused(instr);
    return NULL;
}

struct cbox_layer *cbox_layer_load2(struct cbox_scene *scene, const char *instrument_name, GError **error)
{
    struct cbox_layer *layer = (struct cbox_layer *)CBOX_CREATE_OTHER(scene, cbox_layer);
    if (!layer) goto error;

    if (!cbox_layer_load(layer, scene->instrument_mgr, instrument_name, error))
        goto error;

    return layer;

error:
    CBOX_DELETE(layer);
    return NULL;
}

