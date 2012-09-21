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
#include "errors.h"
#include "song.h"
#include "track.h"
#include <assert.h>
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
        
        for(GList *p = song->tracks; p; p = g_list_next(p))
        {
            struct cbox_track *trk = p->data;
            if (!cbox_execute_on(fb, NULL, "/track", "sio", error, trk->name, g_list_length(trk->items), trk))
                return FALSE;
        }
        for(GList *p = song->patterns; p; p = g_list_next(p))
        {
            struct cbox_midi_pattern *pat = p->data;
            if (!cbox_execute_on(fb, NULL, "/pattern", "sio", error, pat->name, pat->loop_end, pat))
                return FALSE;
        }
        return cbox_execute_on(fb, NULL, "/loop_start", "i", error, (int)song->loop_start_ppqn) &&
            cbox_execute_on(fb, NULL, "/loop_end", "i", error, (int)song->loop_end_ppqn) &&
            CBOX_OBJECT_DEFAULT_STATUS(song, fb, error);
    }
    else
    if (!strcmp(cmd->command, "/set_loop") && !strcmp(cmd->arg_types, "ii"))
    {
        song->loop_start_ppqn = CBOX_ARG_I(cmd, 0);
        song->loop_end_ppqn = CBOX_ARG_I(cmd, 1);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/clear") && !strcmp(cmd->arg_types, ""))
    {
        cbox_song_clear(song);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/add_track") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct cbox_track *track = cbox_track_new(CBOX_GET_DOCUMENT(song));
        cbox_song_add_track(song, track);
        if (!cbox_execute_on(fb, NULL, "/uuid", "o", error, track))
        {
            CBOX_DELETE(track);
            return FALSE;
        }
        
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/load_pattern") && !strcmp(cmd->arg_types, "si"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct cbox_midi_pattern *pattern = cbox_midi_pattern_load(song, CBOX_ARG_S(cmd, 0), CBOX_ARG_I(cmd, 1));
        if (!cbox_execute_on(fb, NULL, "/uuid", "o", error, pattern))
        {
            CBOX_DELETE(pattern);
            return FALSE;
        }
        
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/load_track") && !strcmp(cmd->arg_types, "si"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct cbox_midi_pattern *pattern = cbox_midi_pattern_load_track(song, CBOX_ARG_S(cmd, 0), CBOX_ARG_I(cmd, 1));
        if (!cbox_execute_on(fb, NULL, "/uuid", "o", error, pattern))
        {
            CBOX_DELETE(pattern);
            return FALSE;
        }
        
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/load_metronome") && !strcmp(cmd->arg_types, "i"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct cbox_midi_pattern *pattern = cbox_midi_pattern_new_metronome(song, CBOX_ARG_I(cmd, 0));
        if (!cbox_execute_on(fb, NULL, "/uuid", "o", error, pattern))
        {
            CBOX_DELETE(pattern);
            return FALSE;
        }
        
        return TRUE;
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
    track->owner = song;
    song->tracks = g_list_append(song->tracks, track);
}

void cbox_song_remove_track(struct cbox_song *song, struct cbox_track *track)
{
    assert(track->owner == song);
    song->tracks = g_list_remove(song->tracks, track);
    track->owner = NULL;
}

void cbox_song_add_pattern(struct cbox_song *song, struct cbox_midi_pattern *pattern)
{
    pattern->owner = song;
    song->patterns = g_list_append(song->patterns, pattern);
}

void cbox_song_remove_pattern(struct cbox_song *song, struct cbox_midi_pattern *pattern)
{
    assert(pattern->owner == song);
    pattern->owner = NULL;
    song->patterns = g_list_remove(song->patterns, pattern);
}

void cbox_song_clear(struct cbox_song *song)
{
    while(song->tracks)
        cbox_object_destroy(song->tracks->data);
    while(song->patterns)
        cbox_object_destroy(song->patterns->data);
}

void cbox_song_use_looped_pattern(struct cbox_song *song, struct cbox_midi_pattern *pattern)
{
    assert(pattern->owner == song);
    song->patterns = g_list_remove(song->patterns, pattern);
    pattern->owner = NULL;
    
    cbox_song_clear(song);
    struct cbox_track *trk = cbox_track_new(CBOX_GET_DOCUMENT(song));
    cbox_song_add_track(song, trk);
    cbox_song_add_pattern(song, pattern);
    song->loop_start_ppqn = 0;
    song->loop_end_ppqn = pattern->loop_end;
    cbox_track_add_item(trk, 0, pattern, 0, pattern->loop_end);
    cbox_rt_update_song_playback(app.rt);
}

void cbox_song_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_song *song = CBOX_H2O(objhdr);
    g_list_free_full(song->master_track_items, (GDestroyNotify)cbox_master_track_item_destroy);
    g_list_free_full(song->tracks, (GDestroyNotify)cbox_object_destroy);
    g_list_free_full(song->patterns, (GDestroyNotify)cbox_object_destroy);
    free(song);
}

