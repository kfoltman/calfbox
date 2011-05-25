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

#ifndef CBOX_PATTERN_H
#define CBOX_PATTERN_H

#include "master.h"
#include "midi.h"

struct cbox_midi_pattern
{
    struct cbox_midi_event *events;
    int event_count;
    int loop_end;
};

struct cbox_midi_playback_active_notes
{
    uint16_t channels_active;
    uint32_t notes[16][4]; // 0..127
};

struct cbox_midi_pattern_playback
{
    struct cbox_midi_pattern *pattern;
    struct cbox_master *master;
    int pos;
    int time;
    struct cbox_midi_playback_active_notes *active_notes;
};

extern struct cbox_midi_pattern *cbox_midi_pattern_new_metronome(int ts);

extern struct cbox_midi_pattern *cbox_midi_pattern_load(const char *name, int is_drum);

extern struct cbox_midi_pattern *cbox_midi_pattern_load_track(const char *name, int is_drum);

extern void cbox_read_pattern(struct cbox_midi_pattern_playback *pb, struct cbox_midi_buffer *buf, int nsamples);

extern void cbox_midi_pattern_destroy(struct cbox_midi_pattern *pattern);

extern void cbox_midi_pattern_playback_seek(struct cbox_midi_pattern_playback *pb, int time_ppqn);

extern void cbox_midi_playback_active_notes_init(struct cbox_midi_playback_active_notes *notes);

extern void cbox_midi_playback_active_notes_release(struct cbox_midi_playback_active_notes *notes, struct cbox_midi_buffer *buf);

#endif
