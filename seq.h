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

#ifndef CBOX_SEQ_H
#define CBOX_SEQ_H

struct cbox_track;

struct cbox_track_playback_item
{
    uint32_t time;
    struct cbox_pattern *pattern;
    uint32_t offset;
    uint32_t length;
    // in future, it should also contain a pre-calculated list of notes to release
};

struct cbox_track_playback
{
    struct cbox_track_playback_item **items;
    int items_count;
    int pos;
    struct cbox_midi_playback_active_notes active_notes;
};

extern struct cbox_track_playback *cbox_track_playback_new_from_track(struct cbox_track *track, int time);
extern void cbox_track_playback_destroy(struct cbox_track_playback *pb);

#endif
