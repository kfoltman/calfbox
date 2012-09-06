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

#include "errors.h"
#include "pattern.h"
#include "pattern-maker.h"
#include <glib.h>
#include <smf.h>

struct event_entry
{
    uint32_t time;
    uint8_t data[4];
};

static gint event_entry_compare(gconstpointer a, gconstpointer b, gpointer unused)
{
    const struct event_entry *ea = a, *eb = b;
    
    if (ea->time < eb->time)
        return -1;
    if (ea->time == eb->time && ea->data[0] < eb->data[0])
        return -1;
    if (ea->time == eb->time && ea->data[0] == eb->data[0] && ea->data[1] < eb->data[1])
        return -1;
    if (ea->time == eb->time && ea->data[0] == eb->data[0] && ea->data[1] == eb->data[1])
        return 0;
    return +1;
}

static void event_entry_destroy(gpointer p)
{
    struct event_entry *e = p;
    free(p);
}

struct cbox_midi_pattern_maker
{
    GTree *events;
};

struct cbox_midi_pattern_maker *cbox_midi_pattern_maker_new()
{
    struct cbox_midi_pattern_maker *maker = malloc(sizeof(struct cbox_midi_pattern_maker));
    maker->events = g_tree_new_full(event_entry_compare, NULL, event_entry_destroy, NULL);
    return maker;
}


void cbox_midi_pattern_maker_add(struct cbox_midi_pattern_maker *maker, uint32_t time, uint8_t cmd, uint8_t val1, uint8_t val2)
{
    struct event_entry *e = malloc(sizeof(struct event_entry));
    e->time = time;
    e->data[0] = cmd;
    e->data[1] = val1;
    e->data[2] = val2;
    
    g_tree_insert(maker->events, e, NULL);
}

void cbox_midi_pattern_maker_add_mem(struct cbox_midi_pattern_maker *maker, uint32_t time, const uint8_t *src, uint32_t len)
{
    if (len > 3)
    {
        g_warning("Event size %d not supported yet, ignoring", (int)len);
        return;
    }
    struct event_entry *e = malloc(sizeof(struct event_entry));
    e->time = time;
    memcpy(e->data, src, len);
    
    g_tree_insert(maker->events, e, NULL);
}

struct traverse_state
{
    struct cbox_midi_event *events;
    int pos;
};

static gboolean traverse_func(gpointer key, gpointer value, gpointer pstate)
{
    struct traverse_state *state = pstate;
    struct event_entry *e = key;
    struct cbox_midi_event *event = &state->events[state->pos++];
    event->time = e->time;
    event->size = midi_cmd_size(e->data[0]);
    memcpy(event->data_inline, &e->data[0], 3);
    return FALSE;
}

struct cbox_midi_pattern *cbox_midi_pattern_maker_create_pattern(struct cbox_midi_pattern_maker *maker, gchar *name)
{
    struct cbox_midi_pattern *p = malloc(sizeof(struct cbox_midi_pattern));
    p->name = name;
    p->event_count = g_tree_nnodes(maker->events);
    p->events = malloc(sizeof(struct cbox_midi_event[1]) * p->event_count);
    
    struct traverse_state st = { p->events, 0 };
    
    g_tree_foreach(maker->events, traverse_func, &st);
    
    return p;
}

gboolean cbox_midi_pattern_maker_load_smf(struct cbox_midi_pattern_maker *maker, const char *filename, int *length, GError **error)
{
    smf_t *smf = smf_load(filename);
    if (!smf)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot load SMF file '%s'", filename);
        return FALSE;
    }
    
    int ppqn = smf->ppqn;
    smf_event_t *event = NULL;
    while ((event = smf_get_next_event(smf)) != NULL) {
        if (smf_event_is_metadata(event))
                continue;

        cbox_midi_pattern_maker_add_mem(maker, event->time_pulses * 1.0 * PPQN / ppqn, event->midi_buffer, event->midi_buffer_length);
    }
    if (length)
        *length = smf_get_length_pulses(smf) * 1.0 * PPQN / ppqn;
    smf_delete(smf);
     
    return TRUE;
}

void cbox_midi_pattern_maker_destroy(struct cbox_midi_pattern_maker *maker)
{
    g_tree_destroy(maker->events);
}


