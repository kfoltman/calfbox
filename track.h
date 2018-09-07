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

#ifndef CBOX_TRACK_H
#define CBOX_TRACK_H

#include "dom.h"

CBOX_EXTERN_CLASS(cbox_track_item)
CBOX_EXTERN_CLASS(cbox_track)

struct cbox_midi_pattern;
struct cbox_track;

struct cbox_track_item
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    struct cbox_track *owner;
    uint32_t time;
    struct cbox_midi_pattern *pattern;
    uint32_t offset;
    uint32_t length;
};

struct cbox_track
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    gchar *name;
    gboolean external_output_set;
    struct cbox_uuid external_output;
    GList *items;
    struct cbox_song *owner;
    struct cbox_track_playback *pb;
    uint32_t generation;
};

extern struct cbox_track *cbox_track_new(struct cbox_document *document);
extern struct cbox_track_item *cbox_track_add_item(struct cbox_track *track, uint32_t time, struct cbox_midi_pattern *pattern, uint32_t offset, uint32_t length);
extern void cbox_track_update_playback(struct cbox_track *track, struct cbox_master *master);
extern void cbox_track_set_dirty(struct cbox_track *track);
extern void cbox_track_item_set_dirty(struct cbox_track_item *track_item);

#endif
