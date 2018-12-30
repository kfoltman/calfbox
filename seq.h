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

#include "dom.h"
#include "midi.h"
#include "mididest.h"

struct cbox_engine;
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
extern void cbox_midi_playback_active_notes_copy(struct cbox_midi_playback_active_notes *dest, const struct cbox_midi_playback_active_notes *src);
extern void cbox_midi_playback_active_notes_clear(struct cbox_midi_playback_active_notes *notes);
extern int cbox_midi_playback_active_notes_release(struct cbox_midi_playback_active_notes *notes, struct cbox_midi_buffer *buf, struct cbox_midi_playback_active_notes *leftover_notes);

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_midi_pattern_playback
{
    struct cbox_midi_event *events;
    uint32_t event_count;
    int ref_count;
    GSequence *note_lookup;
    struct cbox_midi_playback_active_notes note_bitmask;
};

extern struct cbox_midi_pattern_playback *cbox_midi_pattern_playback_new(struct cbox_midi_pattern *pattern);
extern void cbox_midi_pattern_playback_ref(struct cbox_midi_pattern_playback *mppb);
extern void cbox_midi_pattern_playback_unref(struct cbox_midi_pattern_playback *mppb);
extern void cbox_midi_pattern_playback_destroy(struct cbox_midi_pattern_playback *mppb);
extern gboolean cbox_midi_pattern_playback_is_note_active_at(struct cbox_midi_pattern_playback *mppb, uint32_t time_ppqn, uint32_t channel, uint32_t note);

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_midi_clip_playback
{
    struct cbox_midi_pattern_playback *pattern;
    struct cbox_master *master;
    
    uint32_t pos;
    int rel_time_samples;
    // [start, end) of the pattern slice
    uint32_t start_time_samples, end_time_samples;
    uint32_t item_start_ppqn, min_time_ppqn;
    int offset_ppqn;
    struct cbox_midi_playback_active_notes *active_notes;
};

extern void cbox_midi_clip_playback_init(struct cbox_midi_clip_playback *pb, struct cbox_midi_playback_active_notes *active_notes, struct cbox_master *master);
extern void cbox_midi_clip_playback_render(struct cbox_midi_clip_playback *pb, struct cbox_midi_buffer *buf, uint32_t offset, uint32_t nsamples);
extern void cbox_midi_clip_playback_seek_ppqn(struct cbox_midi_clip_playback *pb, uint32_t time_ppqn, uint32_t min_time_ppqn);
extern void cbox_midi_clip_playback_seek_samples(struct cbox_midi_clip_playback *pb, uint32_t time_samples, uint32_t min_time_ppqn);
extern void cbox_midi_clip_playback_set_pattern(struct cbox_midi_clip_playback *pb, struct cbox_midi_pattern_playback *pattern, int start_time_samples, int end_time_samples, int item_start_ppqn, int offset_ppqn);

/////////////////////////////////////////////////////////////////////////////////////////////////////

// all times in this structure are in PPQN
struct cbox_track_playback_item
{
    uint32_t time;
    struct cbox_midi_pattern_playback *pattern;
    uint32_t offset;
    uint32_t length;
    // in future, it should also contain a pre-calculated list of notes to release
};

struct cbox_track_playback
{
    struct cbox_uuid track_uuid; // used as identification only
    struct cbox_track_playback_item *items;
    struct cbox_master *master;
    uint32_t items_count;
    uint32_t pos;
    uint32_t generation; // of the original track
    int ref_count;
    struct cbox_midi_buffer output_buffer;
    struct cbox_midi_clip_playback playback;
    struct cbox_midi_playback_active_notes active_notes;
    struct cbox_midi_merger *external_merger;
    struct cbox_song_playback *spb;
    struct cbox_track_playback *old_state;
    gboolean state_copied;
};

extern struct cbox_track_playback *cbox_track_playback_new_from_track(struct cbox_track *track, struct cbox_master *master, struct cbox_song_playback *spb, struct cbox_track_playback *old_state);
extern void cbox_track_playback_render(struct cbox_track_playback *pb, uint32_t offset, uint32_t nsamples);
extern void cbox_track_playback_seek_ppqn(struct cbox_track_playback *pb, uint32_t time_ppqn, uint32_t min_time_ppqn);
extern void cbox_track_playback_seek_samples(struct cbox_track_playback *pb, uint32_t time_samples);
extern void cbox_track_playback_start_item(struct cbox_track_playback *pb, int time, int is_ppqn, int skip_this_pos);
extern void cbox_track_confirm_stuck_notes(struct cbox_track_playback *pb, struct cbox_midi_playback_active_notes *stuck_notes, uint32_t new_pos);
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

struct cbox_adhoc_pattern
{
    struct cbox_adhoc_pattern *next;

    struct cbox_master *master;
    struct cbox_midi_pattern *pattern;
    struct cbox_midi_pattern_playback *pattern_playback;
    
    struct cbox_midi_playback_active_notes active_notes;
    struct cbox_midi_clip_playback playback;

    struct cbox_midi_buffer output_buffer;
    int id;
    gboolean completed;
};

extern struct cbox_adhoc_pattern *cbox_adhoc_pattern_new(struct cbox_engine *engine, int id, struct cbox_midi_pattern *pattern);
extern void cbox_adhoc_pattern_render(struct cbox_adhoc_pattern *adp, uint32_t offset, uint32_t nsamples);
extern void cbox_adhoc_pattern_destroy(struct cbox_adhoc_pattern *ap);

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_song_playback
{
    struct cbox_master *master;
    struct cbox_song *song; // for identification only
    struct cbox_track_playback **tracks;
    uint32_t track_count;
    struct cbox_tempo_map_item *tempo_map_items;
    int tempo_map_item_count;
    int tempo_map_pos;
    uint32_t song_pos_samples, song_pos_ppqn, min_time_ppqn;
    uint32_t loop_start_ppqn, loop_end_ppqn;
    GHashTable *pattern_map;
    struct cbox_midi_merger track_merger;
    struct cbox_engine *engine;
};

extern struct cbox_song_playback *cbox_song_playback_new(struct cbox_song *song, struct cbox_master *master, struct cbox_engine *engine, struct cbox_song_playback *old_state);
extern void cbox_song_playback_prepare_render(struct cbox_song_playback *spb);
extern void cbox_song_playback_render(struct cbox_song_playback *spb, struct cbox_midi_buffer *output, uint32_t nsamples);
extern int cbox_song_playback_active_notes_release(struct cbox_song_playback *old_spb, struct cbox_song_playback *new_spb, uint32_t new_pos, struct cbox_midi_buffer *buf);
extern void cbox_song_playback_seek_ppqn(struct cbox_song_playback *spb, int time_ppqn, int skip_this_pos);
extern void cbox_song_playback_seek_samples(struct cbox_song_playback *spb, uint32_t time_samples);
extern int cbox_song_playback_tmi_from_ppqn(struct cbox_song_playback *spb, uint32_t time_ppqn);
extern int cbox_song_playback_tmi_from_samples(struct cbox_song_playback *spb, uint32_t time_samples);
struct cbox_midi_pattern_playback *cbox_song_playback_get_pattern(struct cbox_song_playback *spb, struct cbox_midi_pattern *pattern);
extern void cbox_song_playback_apply_old_state(struct cbox_song_playback *spb);
extern void cbox_song_playback_destroy(struct cbox_song_playback *spb);

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_song_time_mapper
{
    struct cbox_time_mapper tmap;
    struct cbox_engine *engine;
};

extern void cbox_song_time_mapper_init(struct cbox_song_time_mapper *tmap, struct cbox_engine *engine);

#endif
