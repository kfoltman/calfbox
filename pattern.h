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

#include "dom.h"
#include "master.h"
#include "midi.h"

CBOX_EXTERN_CLASS(cbox_midi_pattern)

struct cbox_blob;
struct cbox_song;

struct cbox_midi_pattern
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    struct cbox_song *owner;
    gchar *name;
    struct cbox_midi_event *events;
    uint32_t event_count;
    int loop_end;
};

struct cbox_blob_serialized_event
{
    int32_t time;
    unsigned char len, cmd, byte1, byte2;
};

extern struct cbox_midi_pattern *cbox_midi_pattern_new_metronome(struct cbox_song *song, int ts, uint64_t ppqn_factor);
extern struct cbox_midi_pattern *cbox_midi_pattern_load(struct cbox_song *song, const char *name, int is_drum, uint64_t ppqn_factor);
extern struct cbox_midi_pattern *cbox_midi_pattern_load_track(struct cbox_song *song, const char *name, int is_drum, uint64_t ppqn_factor);
extern struct cbox_midi_pattern *cbox_midi_pattern_new_from_blob(struct cbox_song *song, const struct cbox_blob *blob, int length, uint64_t ppqn_factor);

extern struct cbox_blob *cbox_midi_pattern_to_blob(struct cbox_midi_pattern *pat, int *length);

extern gboolean cbox_midi_pattern_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

#endif
