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
#include "master.h"
#include "pattern.h"
#include "rt.h"
#include "seq.h"
#include "track.h"
#include <malloc.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_track)
CBOX_CLASS_DEFINITION_ROOT(cbox_track_item)

static gboolean cbox_track_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);
static gboolean cbox_track_item_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

void cbox_track_item_destroyfunc(struct cbox_objhdr *hdr)
{
    struct cbox_track_item *item = CBOX_H2O(hdr);
    item->owner->items = g_list_remove(item->owner->items, item);
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

struct cbox_track_item *cbox_track_add_item(struct cbox_track *track, uint32_t time, struct cbox_midi_pattern *pattern, uint32_t offset, uint32_t length)
{
    struct cbox_track_item *item = malloc(sizeof(struct cbox_track_item));
    CBOX_OBJECT_HEADER_INIT(item, cbox_track_item, CBOX_GET_DOCUMENT(track));
    item->owner = track;
    item->time = time;
    item->pattern = pattern;
    item->offset = offset;
    item->length = length;
    cbox_command_target_init(&item->cmd_target, cbox_track_item_process_cmd, item);
    
    GList *it = track->items;
    while(it != NULL && CBTI(it)->time < item->time)
        it = g_list_next(it);
    // all items earlier than the new one -> append
    if (it == NULL)
    {
        track->items = g_list_append(track->items, item);
        CBOX_OBJECT_REGISTER(item);
        return item;
    }
    // Here, I don't really care about overlaps - it's more important to preserve
    // all clips as sent by the caller.
    track->items = g_list_insert_before(track->items, it, item);
    CBOX_OBJECT_REGISTER(item);
    return item;
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
    // The items will unlink themselves from the list in destructor
    while(track->items)
        cbox_object_destroy(track->items->data);
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
            if (!cbox_execute_on(fb, NULL, "/clip", "iiioo", error, trki->time, trki->offset, trki->length, trki->pattern, trki))
                return FALSE;
            it = g_list_next(it);
        }

        return cbox_execute_on(fb, NULL, "/name", "s", error, track->name) &&
            CBOX_OBJECT_DEFAULT_STATUS(track, fb, error);
    }
    else if (!strcmp(cmd->command, "/add_clip") && !strcmp(cmd->arg_types, "iiis"))
    {
        int pos = CBOX_ARG_I(cmd, 0);
        int offset = CBOX_ARG_I(cmd, 1);
        int length = CBOX_ARG_I(cmd, 2);
        if (pos < 0)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid pattern position %d (cannot be negative)", pos);
            return FALSE;
        }
        if (offset < 0)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid pattern offset %d (cannot be negative)", offset);
            return FALSE;
        }
        if (length <= 0)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid pattern length %d (must be positive)", length);
            return FALSE;
        }
        struct cbox_objhdr *pattern = CBOX_ARG_O(cmd, 3, track, cbox_midi_pattern, error);
        if (!pattern)
            return FALSE;
        struct cbox_midi_pattern *mp = CBOX_H2O(pattern);
        struct cbox_track_item *trki = cbox_track_add_item(track, pos, mp, offset, length);
        if (fb)
            return cbox_execute_on(fb, NULL, "/uuid", "o", error, trki);
        return TRUE;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

gboolean cbox_track_item_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_track_item *trki = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        return cbox_execute_on(fb, NULL, "/pos", "i", error, trki->time) &&
            cbox_execute_on(fb, NULL, "/offset", "i", error, trki->offset) &&
            cbox_execute_on(fb, NULL, "/length", "i", error, trki->length) &&
            cbox_execute_on(fb, NULL, "/pattern", "o", error, trki->pattern) &&
            CBOX_OBJECT_DEFAULT_STATUS(trki, fb, error);
    }
    if (!strcmp(cmd->command, "/delete") && !strcmp(cmd->arg_types, ""))
    {
        cbox_object_destroy(CBOX_O2H(trki));
        return TRUE;
    }
    return cbox_object_default_process_cmd(ct, fb, cmd, error);
}
