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
#include <glib.h>

struct cbox_layer *cbox_layer_load(const char *name, GError **error)
{
    struct cbox_layer *l = malloc(sizeof(struct cbox_layer));
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
    instr = cbox_instruments_get_by_name(cv, TRUE, error);
    if (!instr)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Cannot get instrument %s for layer %s: ", cv, name);
        goto error;
    }
    l->instrument = instr;

    l->low_note = 0;
    l->high_note = 127;
    
    cv = cbox_config_get_string(section, "low_note");
    if (cv)
        l->low_note = note_from_string(cv);
    
    cv = cbox_config_get_string(section, "high_note");
    if (cv)
        l->high_note = note_from_string(cv);
    
    l->transpose = cbox_config_get_int(section, "transpose", 0);
    l->fixed_note = cbox_config_get_int(section, "fixed_note", -1);
    l->in_channel = cbox_config_get_int(section, "in_channel", 0) - 1;
    l->out_channel = cbox_config_get_int(section, "out_channel", 0) - 1;
    l->disable_aftertouch = !cbox_config_get_int(section, "aftertouch", 1);
    l->invert_sustain = cbox_config_get_int(section, "invert_sustain", 0);
    l->consume = cbox_config_get_int(section, "consume", 0);
    l->ignore_scene_transpose = cbox_config_get_int(section, "ignore_scene_transpose", 0);
    l->instrument->refcount++;
    
    g_free(section);
    
    return l;

error:
    g_free(section);
    free(l);
    return NULL;
}

extern struct cbox_layer *cbox_layer_new(const char *module_name, GError **error)
{
    struct cbox_layer *l = malloc(sizeof(struct cbox_layer));
    const char *cv = NULL;
    struct cbox_instrument *instr = NULL;
    
    instr = cbox_instruments_get_by_name(module_name, TRUE, error);
    if (!instr)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Cannot get instrument %s for new layer: ", module_name);
        goto error;
    }

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
    l->instrument->refcount++;
    
    return l;

error:
    free(l);
    return NULL;
}

void cbox_layer_destroy(struct cbox_layer *layer)
{
    if (!--(layer->instrument->refcount))
    {
        cbox_instrument_destroy(layer->instrument);
    }
    free(layer);
}
