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

#ifndef CBOX_SONG_H
#define CBOX_SONG_H

#include "pattern.h"

struct cbox_track;

struct cbox_master_track_item
{
    uint32_t time;
    double tempo;
    int timesig_nom, timesig_denom;
};

struct cbox_master_track
{
    GList *items;
};

struct cbox_song
{
    struct cbox_master *master;
    GList *master_track_items;
    GList *tracks;
    gchar *lyrics_sheet, *chord_sheet;
    uint32_t loop_start_ppqn, loop_end_ppqn;
};

extern struct cbox_song *cbox_song_new(struct cbox_master *master);
extern void cbox_song_add_track(struct cbox_song *song, struct cbox_track *track);
extern void cbox_song_destroy(struct cbox_song *song);

#endif
