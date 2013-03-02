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

#include "dom.h"
#include "pattern.h"

CBOX_EXTERN_CLASS(cbox_song)

struct cbox_track;

struct cbox_master_track_item
{
    uint32_t duration_ppqn;
    // May be zero (= no change)
    double tempo;
    // Either both are zero (= no change) or both are non-zero
    int timesig_nom, timesig_denom;
};

struct cbox_master_track
{
    GList *items;
};

struct cbox_song
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    GList *master_track_items;
    GList *tracks;
    GList *patterns;
    gchar *lyrics_sheet, *chord_sheet;
    uint32_t loop_start_ppqn, loop_end_ppqn;
};

extern struct cbox_song *cbox_song_new(struct cbox_document *document);
extern void cbox_song_add_track(struct cbox_song *song, struct cbox_track *track);
extern void cbox_song_remove_track(struct cbox_song *song, struct cbox_track *track);
extern void cbox_song_clear(struct cbox_song *song);
extern void cbox_song_use_looped_pattern(struct cbox_song *song, struct cbox_midi_pattern *pattern);
extern void cbox_song_set_mti(struct cbox_song *song, uint32_t pos, double tempo, int timesig_nom, int timesig_denom);
extern void cbox_song_destroy(struct cbox_song *song);

#endif
