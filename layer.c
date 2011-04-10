/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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
#include "instr.h"
#include "layer.h"
#include "midi.h"
#include <glib.h>

struct cbox_layer *cbox_layer_load(const char *name)
{
    struct cbox_layer *l = malloc(sizeof(struct cbox_layer));
    const char *cv = NULL;
    struct cbox_module *m = NULL;
    gchar *section = g_strdup_printf("layer:%s", name);
    
    if (!cbox_config_has_section(section))
    {
        g_error("Missing section for layer %s", name);
        goto error;
    }
    
    cv = cbox_config_get_string(section, "instrument");
    if (!cv)
    {
        g_error("Instrument not specified for layer %s", name);
        goto error;
    }
    m = cbox_instruments_get_by_name(cv);
    if (!m)
    {
        g_error("Missing instrument %s for layer %s", cv, name);
        goto error;
    }
    l->output = m;

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
    
    g_free(section);
    
    return l;

error:
    g_free(section);
    free(l);
    return NULL;
}

extern struct cbox_layer *cbox_layer_new(const char *module_name)
{
    struct cbox_layer *l = malloc(sizeof(struct cbox_layer));
    const char *cv = NULL;
    struct cbox_module *m = NULL;
    
    m = cbox_instruments_get_by_name(module_name);
    if (!m)
    {
        g_error("Missing instrument %s for new layer", module_name);
        goto error;
    }

    l->output = m;
    l->low_note = 0;
    l->high_note = 127;
    
    l->transpose = 0;
    l->fixed_note = -1;
    l->in_channel = -1;
    l->out_channel = -1;
    
    return l;

error:
    free(l);
    return NULL;
}
