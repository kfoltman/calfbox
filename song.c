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
        uint32_t pos = 0;
        for(GList *p = song->master_track_items; p; p = g_list_next(p))
        {
            struct cbox_master_track_item *mti = p->data;
            if (!cbox_execute_on(fb, NULL, "/mti", "ifii", error, pos, mti->tempo, mti->timesig_nom, mti->timesig_denom))
                return FALSE;
            pos += mti->duration_ppqn;
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
    if (!strcmp(cmd->command, "/set_mti") && !strcmp(cmd->arg_types, "ifii"))
    {
        cbox_song_set_mti(song, CBOX_ARG_I(cmd, 0), CBOX_ARG_F(cmd, 1), CBOX_ARG_I(cmd, 2), CBOX_ARG_I(cmd, 3));
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
    if (!strcmp(cmd->command, "/load_blob") && !strcmp(cmd->arg_types, "bi"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct cbox_midi_pattern *pattern = cbox_midi_pattern_new_from_blob(song, CBOX_ARG_B(cmd, 0), CBOX_ARG_I(cmd, 1));
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
    
    return p;
}

void cbox_song_set_mti(struct cbox_song *song, uint32_t pos, double tempo, int timesig_nom, int timesig_denom)
{
    uint32_t tstart = 0, tend = 0;
    GList *prev = NULL;
    // A full no-op
    if (tempo < 0 && timesig_nom < 0)
        return;
    gboolean is_noop = tempo == 0 && timesig_nom == 0;
    
    struct cbox_master_track_item *mti = NULL;
    for(GList *p = song->master_track_items; p; p = g_list_next(p))
    {
        mti = p->data;
        tend = tstart + mti->duration_ppqn;
        // printf("range %d-%d %f %d\n", tstart, tend, mti->tempo, mti->timesig_nom);
        if (pos == tstart)
        {
            double new_tempo = tempo >= 0 ? tempo : mti->tempo;
            int new_timesig_nom = timesig_nom >= 0 ? timesig_nom : mti->timesig_nom;
            // Is this operation going to become a no-op after the change?
            gboolean is_noop_here = new_tempo <= 0 && new_timesig_nom <= 0;
            // If the new item is a no-op and not the first item, delete it
            // and extend the previous item by deleted item's duration
            if (is_noop_here && prev)
            {
                uint32_t deleted_duration = mti->duration_ppqn;
                song->master_track_items = g_list_remove(song->master_track_items, mti);
                mti = prev->data;
                mti->duration_ppqn += deleted_duration;
                return;
            }
            goto set_values;
        }
        if (pos >= tstart && pos < tend)
        {
            if (is_noop || (tempo <= 0 && timesig_nom <= 0))
                return;
            // Split old item's duration
            mti->duration_ppqn = pos - tstart;
            mti = calloc(1, sizeof(struct cbox_master_track_item));
            mti->duration_ppqn = tend - pos;
            p = g_list_next(p);
            song->master_track_items = g_list_insert_before(song->master_track_items, p, mti);
            goto set_values;
        }
        prev = p;
        tstart = tend;
    }
    // The new item is a no-op and it's not deleting any of the current MTIs.
    // Ignore it then.
    if (is_noop)
        return;
    // The add position is past the end of the current MTIs.
    if (pos > tend)
    {
        // Either extend the previous item, if there's any
        if (prev)
        {
            mti = prev->data;
            mti->duration_ppqn += pos - tend;
        }
        else
        {
            // ... or add a dummy 'pad' item
            mti = calloc(1, sizeof(struct cbox_master_track_item));
            mti->duration_ppqn = pos;
            assert(!song->master_track_items);
            song->master_track_items = g_list_append(song->master_track_items, mti);
            prev = song->master_track_items;
        }
    }
    // Add the new item at the end
    mti = calloc(1, sizeof(struct cbox_master_track_item));
    song->master_track_items = g_list_append(song->master_track_items, mti);
set_values:
    // No effect if -1
    if (tempo >= 0)
        mti->tempo = tempo;
    if ((timesig_nom > 0 && timesig_denom > 0) ||
        (timesig_nom == 0 && timesig_denom == 0))
    {
        mti->timesig_nom = timesig_nom;
        mti->timesig_denom = timesig_denom;
    }
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
    while(song->master_track_items)
    {
        struct cbox_master_track_item *mti = song->master_track_items->data;
        song->master_track_items = g_list_remove(song->master_track_items, mti);
        cbox_master_track_item_destroy(mti);
    }
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
    cbox_song_clear(song);
    free(song);
}

