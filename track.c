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

#include "master.h"
#include "rt.h"
#include "seq.h"
#include "track.h"
#include <malloc.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_track)

static gboolean cbox_track_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

void cbox_track_item_destroy(struct cbox_track_item *item)
{
    free(item);
}

struct cbox_track *cbox_track_new(struct cbox_document *document)
{
    struct cbox_track *p = malloc(sizeof(struct cbox_track));
    CBOX_OBJECT_HEADER_INIT(p, cbox_track, document);
    
    p->name = g_strdup("Unnamed");
    p->items = NULL;
    p->pb = NULL;

    cbox_command_target_init(&p->cmd_target, cbox_track_process_cmd, p);
    CBOX_OBJECT_REGISTER(p);
    return p;
}

#define CBTI(it) ((struct cbox_track_item *)(it)->data)

void cbox_track_add_item(struct cbox_track *track, uint32_t time, struct cbox_midi_pattern *pattern, uint32_t offset, uint32_t length)
{
    struct cbox_track_item *item = malloc(sizeof(struct cbox_track_item));
    item->time = time;
    item->pattern = pattern;
    item->offset = offset;
    item->length = length;
    
    GList *it = track->items;
    while(it != NULL && CBTI(it)->time < item->time)
        it = g_list_next(it);
    // all items earlier than the new one -> append
    if (it == NULL)
    {
        track->items = g_list_append(track->items, item);
        return;
    }
    // Here, I don't really care about overlaps - it's more important to preserve
    // all clips as sent by the caller.
    track->items = g_list_insert_before(track->items, it, item);
}

void cbox_track_update_playback(struct cbox_track *track, struct cbox_master *master)
{
    struct cbox_track_playback *pb = cbox_track_playback_new_from_track(track, master);
    struct cbox_track_playback *old_pb = cbox_rt_swap_pointers(master->rt, (void **)&track->pb, pb);
    if (old_pb)
        cbox_track_playback_destroy(old_pb);
}

void cbox_track_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_track *track = CBOX_H2O(objhdr);
    // XXXKF I'm not sure if I want the lifecycle of track playback objects to be managed by the track itself
    if (track->pb)
        cbox_track_playback_destroy(track->pb);
    g_list_free_full(track->items, (GDestroyNotify)cbox_track_item_destroy);
    g_free((gchar *)track->name);
    free(track);
}

gboolean cbox_track_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_track *track = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        GList *it = track->items;
        while(it != NULL)
        {
            struct cbox_track_item *trki = it->data;
            if (!cbox_execute_on(fb, NULL, "/item", "iiio", error, trki->time, trki->offset, trki->length, trki->pattern))
                return FALSE;
            it = g_list_next(it);
        }

        return cbox_execute_on(fb, NULL, "/name", "s", error, track->name) &&
            CBOX_OBJECT_DEFAULT_STATUS(track, fb, error);
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}
