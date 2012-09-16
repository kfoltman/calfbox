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
#include "song.h"
#include "track.h"
#include <stdlib.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_song)

/////////////////////////////////////////////////////////////////////////////////////////////////

void cbox_master_track_item_destroy(struct cbox_master_track_item *item)
{
    free(item);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

gboolean cbox_song_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_song *song = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        int nt = 1;
        for(GList *p = song->tracks; p; p = g_list_next(p))
        {
            struct cbox_track *trk = p->data;
            if (!cbox_execute_on(fb, NULL, "/track", "isio", error, nt++, trk->name, g_list_length(trk->items), trk))
                return FALSE;
        }
        int np = 1;
        for(GList *p = song->patterns; p; p = g_list_next(p))
        {
            struct cbox_midi_pattern *pat = p->data;
            if (!cbox_execute_on(fb, NULL, "/pattern", "isio", error, np++, pat->name, pat->loop_end, pat))
                return FALSE;
        }
        return CBOX_OBJECT_DEFAULT_STATUS(song, fb, error);
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}


/////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_song *cbox_song_new(struct cbox_document *document)
{
    struct cbox_song *p = malloc(sizeof(struct cbox_song));
    CBOX_OBJECT_HEADER_INIT(p, cbox_song, document);
    p->master_track_items = NULL;
    p->tracks = NULL;
    p->patterns = NULL;
    p->lyrics_sheet = NULL;
    p->chord_sheet = NULL;
    cbox_command_target_init(&p->cmd_target, cbox_song_process_cmd, p);
    CBOX_OBJECT_REGISTER(p);
    
#if 0
    struct cbox_master_track_item *mti = malloc(sizeof(struct cbox_master_track_item));
    mti->duration_ppqn = PPQN * 2;
    mti->tempo = 120;
    p->master_track_items = g_list_append(p->master_track_items, mti);    
    mti = malloc(sizeof(struct cbox_master_track_item));
    mti->duration_ppqn = PPQN;
    mti->tempo = 60;
    p->master_track_items = g_list_append(p->master_track_items, mti);    
    mti = malloc(sizeof(struct cbox_master_track_item));
    mti->duration_ppqn = PPQN;
    mti->tempo = 120;
    p->master_track_items = g_list_append(p->master_track_items, mti);    
#endif
    
    return p;
}

void cbox_song_add_track(struct cbox_song *song, struct cbox_track *track)
{
    song->tracks = g_list_append(song->tracks, track);
}

void cbox_song_remove_track(struct cbox_song *song, struct cbox_track *track)
{
    song->tracks = g_list_remove(song->tracks, track);
}

void cbox_song_add_pattern(struct cbox_song *song, struct cbox_midi_pattern *pattern)
{
    song->patterns = g_list_append(song->patterns, pattern);
}

void cbox_song_remove_patterns(struct cbox_song *song, struct cbox_midi_pattern *pattern)
{
    song->patterns = g_list_remove(song->patterns, pattern);
}

void cbox_song_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_song *song = (struct cbox_song *)objhdr;
    g_list_free_full(song->master_track_items, (GDestroyNotify)cbox_master_track_item_destroy);
    g_list_free_full(song->tracks, (GDestroyNotify)cbox_object_destroy);
    g_list_free_full(song->patterns, (GDestroyNotify)cbox_object_destroy);
    free(song);
}

