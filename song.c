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

#include "song.h"
#include <stdlib.h>

struct cbox_song *cbox_song_new()
{
    struct cbox_song *p = malloc(sizeof(struct cbox_song *));
    p->master_track_items = NULL;
    p->tracks = NULL;
    p->lyrics_sheet = NULL;
    p->chord_sheet = NULL;
    return p;
}

void cbox_master_track_item_destroy(struct cbox_master_track_item *item)
{
    free(item);
}

void cbox_track_item_destroy(struct cbox_track_item *item)
{
    free(item);
}

struct cbox_track *cbox_track_new()
{
    struct cbox_track *p = malloc(sizeof(struct cbox_track));
    p->output = NULL;
    p->items = NULL;
    cbox_midi_playback_active_notes_init(&p->active_notes);
    return p;
}

void cbox_track_destroy(struct cbox_track *track)
{
    g_list_free_full(track->items, (GDestroyNotify)cbox_track_item_destroy);
    free(track);
}

void cbox_song_destroy(struct cbox_song *song)
{
    g_list_free_full(song->master_track_items, (GDestroyNotify)cbox_master_track_item_destroy);
    g_list_free_full(song->tracks, (GDestroyNotify)cbox_track_destroy);
}

