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

#include <stdint.h>

#include "midi.h"

struct cbox_midi_pattern;
struct cbox_track;
struct cbox_song;

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_midi_playback_active_notes
{
    uint16_t channels_active;
    uint32_t notes[16][4]; // 0..127
};

extern void cbox_midi_playback_active_notes_init(struct cbox_midi_playback_active_notes *notes);
extern int cbox_midi_playback_active_notes_release(struct cbox_midi_playback_active_notes *notes, struct cbox_midi_buffer *buf);

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_midi_pattern_playback
{
    struct cbox_midi_pattern *pattern;
    struct cbox_master *master;
    int pos;
    int rel_time_samples;
    // [start, end) of the pattern slice
    int start_time_samples, end_time_samples;
    int item_start_ppqn, min_time_ppqn;
    int offset_ppqn;
    struct cbox_midi_playback_active_notes *active_notes;
};

extern void cbox_midi_pattern_playback_init(struct cbox_midi_pattern_playback *pb, struct cbox_midi_playback_active_notes *active_notes, struct cbox_master *master);
extern void cbox_midi_pattern_playback_render(struct cbox_midi_pattern_playback *pb, struct cbox_midi_buffer *buf, int offset, int nsamples);
extern void cbox_midi_pattern_playback_seek_ppqn(struct cbox_midi_pattern_playback *pb, int time_ppqn, int skip_this_pos);
extern void cbox_midi_pattern_playback_seek_samples(struct cbox_midi_pattern_playback *pb, int time_samples);
extern void cbox_midi_pattern_playback_set_pattern(struct cbox_midi_pattern_playback *pb, struct cbox_midi_pattern *pattern, int start_time_samples, int end_time_samples, int item_start_ppqn, int offset_ppqn);

/////////////////////////////////////////////////////////////////////////////////////////////////////

// all times in this structure are in PPQN
struct cbox_track_playback_item
{
    uint32_t time;
    struct cbox_midi_pattern *pattern;
    uint32_t offset;
    uint32_t length;
    // in future, it should also contain a pre-calculated list of notes to release
};

struct cbox_track_playback
{
    struct cbox_track_playback_item *items;
    struct cbox_master *master;
    int items_count;
    int pos;
    struct cbox_midi_buffer output_buffer;
    struct cbox_midi_pattern_playback playback;
    struct cbox_midi_playback_active_notes active_notes;
};

extern struct cbox_track_playback *cbox_track_playback_new_from_track(struct cbox_track *track, struct cbox_master *master);
extern void cbox_track_playback_render(struct cbox_track_playback *pb, int offset, int nsamples);
extern void cbox_track_playback_seek_ppqn(struct cbox_track_playback *pb, int time_ppqn, int min_time_ppqn);
extern void cbox_track_playback_seek_samples(struct cbox_track_playback *pb, int time_samples);
extern void cbox_track_playback_start_item(struct cbox_track_playback *pb, int time, int is_ppqn, int skip_this_pos);
extern void cbox_track_playback_destroy(struct cbox_track_playback *pb);

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_tempo_map_item
{
    uint32_t time_ppqn;
    uint32_t time_samples;
    double tempo;
    int timesig_nom, timesig_denom;
    // should also have a bar/beat position to make things easier
};

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_song_playback
{
    struct cbox_master *master;
    struct cbox_track_playback **tracks;
    int track_count;
    struct cbox_tempo_map_item *tempo_map_items;
    int tempo_map_item_count;
    int tempo_map_pos;
    uint32_t song_pos_samples, song_pos_ppqn, min_time_ppqn;
    uint32_t loop_start_ppqn, loop_end_ppqn;
};

extern struct cbox_song_playback *cbox_song_playback_new(struct cbox_song *song);
extern void cbox_song_playback_render(struct cbox_song_playback *spb, struct cbox_midi_buffer *output, int nsamples);
extern int cbox_song_playback_active_notes_release(struct cbox_song_playback *spb, struct cbox_midi_buffer *buf);
extern void cbox_song_playback_seek_ppqn(struct cbox_song_playback *spb, int time_ppqn, int skip_this_pos);
extern void cbox_song_playback_seek_samples(struct cbox_song_playback *spb, int time_samples);
extern int cbox_song_playback_tmi_from_ppqn(struct cbox_song_playback *spb, int time_ppqn);
extern int cbox_song_playback_tmi_from_samples(struct cbox_song_playback *spb, int time_samples);
extern void cbox_song_playback_destroy(struct cbox_song_playback *spb);

#endif
