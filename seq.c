/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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

#include "engine.h"
#include "pattern.h"
#include "rt.h"
#include "seq.h"
#include "song.h"
#include "track.h"
#include <assert.h>

static inline void accumulate_event(struct cbox_midi_playback_active_notes *notes, const struct cbox_midi_event *event)
{
    if (event->size != 3)
        return;
    // this ignores poly aftertouch - which, I supposed, is OK for now
    if (event->data_inline[0] < 0x90 || event->data_inline[0] > 0x9F)
        return;
    if (event->data_inline[2] > 0)
    {
        int ch = event->data_inline[0] & 0x0F;
        int note = event->data_inline[1] & 0x7F;
        if (!(notes->channels_active & (1 << ch)))
        {
            for (int i = 0; i < 4; i++)
                notes->notes[ch][i] = 0;
            notes->channels_active |= 1 << ch;
        }
        notes->notes[ch][note >> 5] |= 1 << (note & 0x1F);
    }
}

// this releases a note on note off (accumulate_event is 'sticky')
static inline void accumulate_event2(struct cbox_midi_playback_active_notes *notes, const struct cbox_midi_event *event)
{
    if (event->size != 3)
        return;
    // this ignores poly aftertouch - which, I supposed, is OK for now
    if (event->data_inline[0] < 0x80 || event->data_inline[0] > 0x9F)
        return;
    int ch = event->data_inline[0] & 0x0F;
    int note = event->data_inline[1] & 0x7F;
    uint32_t mask = 1 << (note & 0x1F);
    if (event->data_inline[0] >= 0x90 && event->data_inline[2] > 0)
    {
        if (!(notes->channels_active & (1 << ch)))
        {
            for (int i = 0; i < 4; i++)
                notes->notes[ch][i] = 0;
            notes->channels_active |= 1 << ch;
        }
        notes->notes[ch][note >> 5] |= mask;
    } else {
        if (notes->notes[ch][note >> 5] & mask) {
            notes->notes[ch][note >> 5] &= ~mask;
            if (!notes->notes[ch][0] && !notes->notes[ch][1] && !notes->notes[ch][2] && !notes->notes[ch][3]) {
                notes->channels_active &= ~(1 << ch);
            }
        }
    }
}

struct cbox_track_playback *cbox_track_playback_new_from_track(struct cbox_track *track, struct cbox_master *master, struct cbox_song_playback *spb, struct cbox_track_playback *old_state)
{
    struct cbox_track_playback *pb = malloc(sizeof(struct cbox_track_playback));
    cbox_uuid_copy(&pb->track_uuid, &CBOX_O2H(track)->instance_uuid);
    pb->old_state = old_state;
    pb->generation = track->generation;
    pb->ref_count = 1;
    pb->master = master;
    int len = g_list_length(track->items);
    pb->items = calloc(len, sizeof(struct cbox_track_playback_item));
    pb->external_merger = NULL;
    pb->spb = spb;
    pb->state_copied = FALSE;
    pb->mute = track->mute;
    
    GList *it = track->items;
    struct cbox_track_playback_item *p = pb->items;
    uint32_t safe = 0;
    while(it != NULL)
    {
        struct cbox_track_item *item = it->data;
        struct cbox_midi_pattern_playback *mppb = cbox_song_playback_get_pattern(spb, item->pattern);

        // if items overlap, the first one takes precedence
        if (item->time < safe)
        {
            // fully contained in previous item? skip all of it
            // not fully contained - insert the fragment
            if (item->time + item->length >= safe)
            {
                int cut = safe - item->time;
                p->time = safe;
                p->pattern = mppb;
                p->offset = item->offset + cut;
                p->length = item->length - cut;
                p++;
            }
        }
        else
        {
            p->time = item->time;
            p->pattern = mppb;
            p->offset = item->offset;
            p->length = item->length;
            safe = item->time + item->length;
            p++;
        }
        
        it = g_list_next(it);
    }
    // in case of full overlap, some items might have been skipped
    pb->items_count = p - pb->items;
    pb->pos = 0;
    cbox_midi_clip_playback_init(&pb->playback, &pb->active_notes, master);
    cbox_midi_playback_active_notes_init(&pb->active_notes);
    cbox_midi_buffer_init(&pb->output_buffer);
    cbox_track_playback_start_item(pb, 0, FALSE, 0);

    if (track->external_output_set)
    {
        struct cbox_midi_merger *merger = cbox_rt_get_midi_output(spb->engine->rt, &track->external_output);
        if (merger)
            cbox_midi_merger_connect(merger, &pb->output_buffer, spb->engine->rt, &pb->external_merger);
    }
    
    return pb;
}
    
void cbox_track_confirm_stuck_notes(struct cbox_track_playback *pb, struct cbox_midi_playback_active_notes *stuck_notes, uint32_t new_pos_ppqn)
{
    // Check if no notes are stuck
    if (!stuck_notes->channels_active)
        return;
    uint32_t pos = 0;
    while(pos < pb->items_count && pb->items[pos].time + pb->items[pos].length < new_pos_ppqn)
        pos++;
    if (pos >= pb->items_count) // past the end of the track - all notes are stuck
        return;
    const struct cbox_track_playback_item *tpi = &pb->items[pos];
    uint32_t rel_time_ppqn = new_pos_ppqn - tpi->time;
    if (rel_time_ppqn < tpi->length)
    {
        // inside the clip
        rel_time_ppqn += tpi->offset;
        
        for (unsigned c = 0; c < 16; c++)
        {
            if (!(stuck_notes->channels_active & (1 << c)))
                continue;
            
            gboolean any_left = FALSE;
            for (unsigned g = 0; g < 4; g++)
            {
                uint32_t group = stuck_notes->notes[c][g];
                if (!group)
                    continue;
                for (unsigned i = 0; i < 32; i++)
                {
                    if (!(group & (1 << i)))
                        continue;
                    uint8_t n = i + g * 32;
                    if (cbox_midi_pattern_playback_is_note_active_at(tpi->pattern, rel_time_ppqn, c, n))
                    {
                        // That note is not stuck
                        group &= ~(1 << i);
                    } else {
                        // It is stuck, so keep the channel as containing stuck notes
                        any_left = TRUE;
                    }
                }
                stuck_notes->notes[c][g] = group;
            }
            if (!any_left) {
                stuck_notes->channels_active &= ~(1 << c);
            }
        }
        return;
    }
}

void cbox_track_playback_seek_ppqn(struct cbox_track_playback *pb, uint32_t time_ppqn, uint32_t min_time_ppqn)
{
    pb->pos = 0;
    while(pb->pos < pb->items_count && pb->items[pb->pos].time + pb->items[pb->pos].length < time_ppqn)
        pb->pos++;
    cbox_track_playback_start_item(pb, time_ppqn, TRUE, min_time_ppqn);
}

void cbox_track_playback_seek_samples(struct cbox_track_playback *pb, uint32_t time_samples)
{
    pb->pos = 0;
    while(pb->pos < pb->items_count && cbox_master_ppqn_to_samples(pb->master, pb->items[pb->pos].time + pb->items[pb->pos].length) < time_samples)
        pb->pos++;
    if (pb->pos < pb->items_count)
    {
        int min_time_ppqn = cbox_master_samples_to_ppqn(pb->master, time_samples);
        cbox_track_playback_start_item(pb, time_samples, FALSE, min_time_ppqn);
    }
}

void cbox_track_playback_start_item(struct cbox_track_playback *pb, int time, int is_ppqn, int min_time_ppqn)
{
    if (pb->pos >= pb->items_count)
    {
        return;
    }
    struct cbox_track_playback_item *cur = &pb->items[pb->pos];
    int time_samples, time_ppqn;
    
    if (is_ppqn)
    {
        time_ppqn = time;
        time_samples = cbox_master_ppqn_to_samples(pb->master, time_ppqn);
    }
    else
    {
        time_samples = time;
        time_ppqn = cbox_master_samples_to_ppqn(pb->master, time_samples);
    }
    int start_time_ppqn = cur->time, end_time_ppqn = cur->time + cur->length;
    int start_time_samples = cbox_master_ppqn_to_samples(pb->master, start_time_ppqn);
    int end_time_samples = cbox_master_ppqn_to_samples(pb->master, end_time_ppqn);
    cbox_midi_clip_playback_set_pattern(&pb->playback, cur->pattern, start_time_samples, end_time_samples, cur->time, cur->offset);

    if (is_ppqn)
    {
        if (time_ppqn < start_time_ppqn)
            cbox_midi_clip_playback_seek_ppqn(&pb->playback, 0, min_time_ppqn);
        else
            cbox_midi_clip_playback_seek_ppqn(&pb->playback, time_ppqn - start_time_ppqn, min_time_ppqn);
    }
    else
    {
        if (time_ppqn < start_time_ppqn)
            cbox_midi_clip_playback_seek_samples(&pb->playback, 0, min_time_ppqn);
        else
            cbox_midi_clip_playback_seek_samples(&pb->playback, time_samples - start_time_samples, min_time_ppqn);
    }
}

void cbox_track_playback_render(struct cbox_track_playback *pb, uint32_t offset, uint32_t nsamples)
{
    struct cbox_song_playback *spb = pb->master->spb;
    if (pb->mute) {
        cbox_midi_playback_active_notes_release(&pb->active_notes, &pb->output_buffer, NULL);
    }
    uint32_t rpos = 0;
    while(rpos < nsamples && pb->pos < pb->items_count)
    {
        uint32_t rend = nsamples;
        struct cbox_track_playback_item *cur = &pb->items[pb->pos];
        // a gap before the current item
        if (spb->song_pos_samples + rpos < pb->playback.start_time_samples)
        {
            uint32_t space_samples = pb->playback.start_time_samples - (spb->song_pos_samples + rpos);
            if (space_samples >= rend - rpos)
                return;
            rpos += space_samples;
            offset += space_samples;
        }
        // check if item finished
        int cur_segment_end_samples = cbox_master_ppqn_to_samples(pb->master, cur->time + cur->length);
        int render_end_samples = spb->song_pos_samples + rend;
        if (render_end_samples > cur_segment_end_samples)
        {
            rend = cur_segment_end_samples - spb->song_pos_samples;
            cbox_midi_clip_playback_render(&pb->playback, &pb->output_buffer, offset, rend - rpos, pb->mute);
            pb->pos++;
            cbox_track_playback_start_item(pb, cur_segment_end_samples, FALSE, FALSE);
        }
        else
            cbox_midi_clip_playback_render(&pb->playback, &pb->output_buffer, offset, rend - rpos, pb->mute);
        offset += rend - rpos;
        rpos = rend;
    }
}

void cbox_track_playback_ref(struct cbox_track_playback *pb)
{
    ++pb->ref_count;
}

void cbox_track_playback_destroy(struct cbox_track_playback *pb)
{
    if (pb->external_merger)
        cbox_midi_merger_disconnect(pb->external_merger, &pb->output_buffer, pb->spb->engine->rt);

    for (uint32_t i = 0; i < pb->items_count; ++i)
        cbox_midi_pattern_playback_unref(pb->items[i].pattern);
    free(pb->items);
    free(pb);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

static gint note_compare_fn(const void *p1, const void *p2, void *user_data)
{
    const struct cbox_midi_event *e1 = p1, *e2 = p2;
    int cn1 = ((e1->data_inline[0] & 0x0F) << 8) | e1->data_inline[1];
    int cn2 = ((e2->data_inline[0] & 0x0F) << 8) | e2->data_inline[1];
    if (cn1 < cn2)
        return -1;
    if (cn2 < cn1)
        return +1;
    if (e1->time < e2->time)
        return -1;
    if (e1->time > e2->time)
        return +1;
    if (p1 < p2)
        return -1;
    if (p1 > p2)
        return +1;
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_midi_pattern_playback *cbox_midi_pattern_playback_new(struct cbox_midi_pattern *pattern)
{
    struct cbox_midi_pattern_playback *mppb = calloc(1, sizeof(struct cbox_midi_pattern_playback));
    mppb->events = malloc(sizeof(struct cbox_midi_event) * pattern->event_count);
    memcpy(mppb->events, pattern->events, sizeof(struct cbox_midi_event) * pattern->event_count);
    mppb->event_count = pattern->event_count;
    mppb->ref_count = 1;
    cbox_midi_playback_active_notes_init(&mppb->note_bitmask);
    mppb->note_lookup = g_sequence_new(NULL);
    for (uint32_t i = 0; i < mppb->event_count; ++i) {
        struct cbox_midi_event *event = &mppb->events[i];
        if (event->size == 3 && (event->data_inline[0] & 0xE0) == 0x80) {
            g_sequence_insert_sorted(mppb->note_lookup, event, note_compare_fn, NULL);
            if (event->data_inline[0] >= 0x90)
                accumulate_event(&mppb->note_bitmask, event);
        }
    }

    return mppb;
}

void cbox_midi_pattern_playback_unref(struct cbox_midi_pattern_playback *mppb)
{
    if (!(--mppb->ref_count))
        cbox_midi_pattern_playback_destroy(mppb);
}

void cbox_midi_pattern_playback_ref(struct cbox_midi_pattern_playback *mppb)
{
    ++mppb->ref_count;
}

void cbox_midi_pattern_playback_destroy(struct cbox_midi_pattern_playback *mppb)
{
    g_sequence_free(mppb->note_lookup);
    free(mppb->events);
    free(mppb);
}

gboolean cbox_midi_pattern_playback_is_note_active_at(struct cbox_midi_pattern_playback *mppb, uint32_t time_ppqn, uint32_t channel, uint32_t note)
{
    struct cbox_midi_event event;
    event.time = time_ppqn;
    event.size = 3;
    event.data_inline[0] = 0x90 | channel;
    event.data_inline[1] = note;
    event.data_inline[2] = 127;
    // printf("checking stuck note ch %d note %d at %d\n", channel, note, time_ppqn);
    GSequenceIter *i = g_sequence_search(mppb->note_lookup, &event, note_compare_fn, NULL);
    if (g_sequence_iter_is_begin(i)) // before first note
    {
        // printf("before first note\n");
        return FALSE;
    }
    i = g_sequence_iter_prev(i);
    // A preceding note with the same channel and note number
    struct cbox_midi_event *pevent = g_sequence_get(i);
    // If it's an event for a different note, channel or not a note on event, then the note hasn't been active at the time
    // XXXKF what about notes that start before clip offset?
    if (pevent->size != 3 || pevent->data_inline[0] != event.data_inline[0] || pevent->data_inline[1] != event.data_inline[1] || !pevent->data_inline[2]) {
        // printf("pevent wrong %d %d %d %d\n", pevent->time, pevent->data_inline[0], pevent->data_inline[1], pevent->data_inline[2]);
        return FALSE;
    }
    
    // printf("confirmed note ch %d note %d\n", channel, note);
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void cbox_midi_clip_playback_init(struct cbox_midi_clip_playback *pb, struct cbox_midi_playback_active_notes *active_notes, struct cbox_master *master)
{
    pb->pattern = NULL;
    pb->master = master;
    pb->pos = 0;
    pb->rel_time_samples = 0;
    pb->start_time_samples = 0;
    pb->end_time_samples = 0;
    pb->active_notes = active_notes;
    pb->min_time_ppqn = 0;
    // cbox_midi_playback_active_notes_init(active_notes);
}

void cbox_midi_clip_playback_set_pattern(struct cbox_midi_clip_playback *pb, struct cbox_midi_pattern_playback *pattern, int start_time_samples, int end_time_samples, int item_start_ppqn, int offset_ppqn)
{
    pb->pattern = pattern;
    pb->pos = 0;
    pb->rel_time_samples = 0;
    pb->start_time_samples = start_time_samples;
    pb->end_time_samples = end_time_samples;
    pb->item_start_ppqn = item_start_ppqn;
    pb->offset_ppqn = offset_ppqn;
    pb->min_time_ppqn = offset_ppqn;
}

void cbox_midi_clip_playback_render(struct cbox_midi_clip_playback *pb, struct cbox_midi_buffer *buf, uint32_t offset, uint32_t nsamples, gboolean mute)
{
    uint32_t end_time_samples = pb->end_time_samples;
    uint32_t cur_time_samples = pb->start_time_samples + pb->rel_time_samples;
    
    if (end_time_samples > cur_time_samples + nsamples)
        end_time_samples = cur_time_samples + nsamples;

    while(pb->pos < pb->pattern->event_count)
    {
        const struct cbox_midi_event *src = &pb->pattern->events[pb->pos];
        
        if (src->time - pb->offset_ppqn + pb->item_start_ppqn >= pb->min_time_ppqn)
        {
            uint32_t event_time_samples = cbox_master_ppqn_to_samples(pb->master, src->time - pb->offset_ppqn + pb->item_start_ppqn);
        
            if (event_time_samples >= end_time_samples)
                break;
            int32_t time = 0;
            if (event_time_samples >= cur_time_samples) // convert negative relative time to 0 time
                time = event_time_samples - cur_time_samples;
            
            if (!mute) {
                cbox_midi_buffer_copy_event(buf, src, offset + time);
                if (pb->active_notes)
                    accumulate_event2(pb->active_notes, src);
            }
        }
        pb->pos++;
    }
    pb->rel_time_samples += nsamples;
}

void cbox_midi_clip_playback_seek_ppqn(struct cbox_midi_clip_playback *pb, uint32_t time_ppqn, uint32_t min_time_ppqn)
{
    uint32_t patrel_time_ppqn = time_ppqn + pb->offset_ppqn;
    uint32_t L = 0, U = pb->pattern->event_count;

    if (patrel_time_ppqn > 0) {
        while (U > L + 2) {
            uint32_t M = (L >> 1) + (U >> 1) + (L & U & 1);
            uint32_t time = pb->pattern->events[M].time;
            if (time < patrel_time_ppqn)
                L = M + 1;
            else if (time >= patrel_time_ppqn)
                U = M + 1; // this might still be the event we're looking for
        }
    }

    uint32_t pos = L;
    while (pos < U && pb->pattern->events[pos].time < patrel_time_ppqn)
        pos++;
    pb->rel_time_samples = cbox_master_ppqn_to_samples(pb->master, pb->item_start_ppqn + time_ppqn) - pb->start_time_samples;
    pb->min_time_ppqn = min_time_ppqn;
    pb->pos = pos;
}

void cbox_midi_clip_playback_seek_samples(struct cbox_midi_clip_playback *pb, uint32_t time_samples, uint32_t min_time_ppqn)
{
    uint32_t pos = 0;
    while (pos < pb->pattern->event_count && time_samples > cbox_master_ppqn_to_samples(pb->master, pb->item_start_ppqn + pb->pattern->events[pos].time - pb->offset_ppqn))
        pos++;
    pb->rel_time_samples = time_samples;
    pb->min_time_ppqn = min_time_ppqn;
    pb->pos = pos;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void cbox_midi_playback_active_notes_init(struct cbox_midi_playback_active_notes *notes)
{
    notes->channels_active = 0;
}

void cbox_midi_playback_active_notes_copy(struct cbox_midi_playback_active_notes *dest, const struct cbox_midi_playback_active_notes *src)
{
    dest->channels_active = src->channels_active;
    memcpy(dest->notes, src->notes, sizeof(dest->notes));
}

int cbox_midi_playback_active_notes_release(struct cbox_midi_playback_active_notes *notes, struct cbox_midi_buffer *buf, struct cbox_midi_playback_active_notes *leftover_notes)
{
    if (!notes->channels_active)
        return 0;
    int note_offs = 0;
    for (int c = 0; c < 16; c++)
    {
        if (!(notes->channels_active & (1 << c)))
            continue;
        
        for (int g = 0; g < 4; g++)
        {
            uint32_t group = notes->notes[c][g];
            if (!group)
                continue;
            for (int i = 0; i < 32; i++)
            {
                int n = i + g * 32;
                if (!(group & (1 << i)))
                    continue;
                if (!cbox_midi_buffer_can_store_msg(buf, 3))
                    return -1;
                cbox_midi_buffer_write_inline(buf, cbox_midi_buffer_get_last_event_time(buf), 0x80 + c, n, 0);
                group &= ~(1 << i);
                notes->notes[c][g] = group;
                if (leftover_notes)
                    leftover_notes->notes[c][g] &= ~(1 << i);
                note_offs++;
            }
        }
        // all Note Offs emitted without buffer overflow - channel is no longer active
        notes->channels_active &= ~(1 << c);
    }
    return note_offs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

struct cbox_song_playback *cbox_song_playback_new(struct cbox_song *song, struct cbox_master *master, struct cbox_engine *engine, struct cbox_song_playback *old_state)
{
    struct cbox_song_playback *spb = calloc(1, sizeof(struct cbox_song_playback));
    if (old_state && old_state->song != song)
        old_state = NULL;
    spb->song = song;
    spb->engine = engine;
    spb->pattern_map = g_hash_table_new(NULL, NULL);
    spb->master = master;
    spb->track_count = g_list_length(song->tracks);
    spb->tracks = malloc(spb->track_count * sizeof(struct cbox_track_playback *));
    spb->song_pos_samples = 0;
    spb->song_pos_ppqn = 0;
    spb->min_time_ppqn = 0;
    spb->loop_start_ppqn = song->loop_start_ppqn;
    spb->loop_end_ppqn = song->loop_end_ppqn;
    cbox_midi_merger_init(&spb->track_merger, NULL);
    int pos = 0;
    for (GList *p = song->tracks; p != NULL; p = g_list_next(p))
    {
        struct cbox_track *trk = p->data;
        struct cbox_track_playback *old_trk = NULL;
        if (old_state && old_state->track_count)
        {
            for (uint32_t i = 0; i < old_state->track_count; i++)
            {
                if (cbox_uuid_equal(&old_state->tracks[i]->track_uuid, &CBOX_O2H(trk)->instance_uuid))
                {
                    old_trk = old_state->tracks[i];
                    break;
                }
            }
        }
        if (old_trk && trk->generation == old_trk->generation) {
            old_trk->state_copied = TRUE;
            cbox_track_playback_ref(old_trk);
            spb->tracks[pos++] = old_trk;
        }
        else {
            if (old_trk)
                old_trk->state_copied = FALSE;
            spb->tracks[pos++] = cbox_track_playback_new_from_track(trk, spb->master, spb, old_trk);
        }
        if (!trk->external_output_set)
            cbox_midi_merger_connect(&spb->track_merger, &spb->tracks[pos - 1]->output_buffer, NULL, NULL);
    }
    
    spb->tempo_map_item_count = g_list_length(song->master_track_items);
    spb->tempo_map_items = malloc(spb->tempo_map_item_count * sizeof(struct cbox_tempo_map_item));
    pos = 0;
    int pos_ppqn = 0;
    int pos_samples = 0;
    double tempo = master->tempo;
    int timesig_num = master->timesig_num;
    int timesig_denom = master->timesig_denom;
    struct cbox_bbt cur_bbt = {0, 0, 0, 0};
    for (GList *p = song->master_track_items; p != NULL; p = g_list_next(p))
    {
        struct cbox_master_track_item *mti = p->data;
        if (mti->tempo == 0 && mti->timesig_num == 0 && 
            mti->timesig_denom == 0 && p == song->master_track_items) {
            spb->tempo_map_item_count--;
            continue;
        }
        if (mti->tempo > 0)
            tempo = mti->tempo;
        if (mti->timesig_num > 0)
            timesig_num = mti->timesig_num;
        if (mti->timesig_denom > 0)
            timesig_denom = mti->timesig_denom;
        struct cbox_tempo_map_item *tmi = &spb->tempo_map_items[pos];
        tmi->time_ppqn = pos_ppqn;
        tmi->time_samples = pos_samples;
        tmi->tempo = tempo;
        tmi->timesig_num = timesig_num;
        tmi->timesig_denom = timesig_denom;
        memcpy(&tmi->bbt, &cur_bbt, sizeof(cur_bbt));

        cbox_bbt_add(&cur_bbt, mti->duration_ppqn, master->ppqn_factor, timesig_num, timesig_denom);
        pos_ppqn += mti->duration_ppqn;
        pos_samples += master->srate * 60.0 * mti->duration_ppqn / (tempo * master->ppqn_factor);
        pos++;
    }
    return spb;
}

void cbox_song_playback_apply_old_state(struct cbox_song_playback *spb)
{
    for (uint32_t i = 0; i < spb->track_count; i++)
    {
        struct cbox_track_playback *tpb = spb->tracks[i];
        tpb->spb = spb;
        if (tpb->old_state)
        {
            cbox_midi_playback_active_notes_copy(&tpb->active_notes, &tpb->old_state->active_notes);
            tpb->old_state->state_copied = TRUE;
            tpb->old_state = NULL;
        }
    }
}


static void cbox_song_playback_set_tempo(struct cbox_song_playback *spb, double tempo)
{
    int ppos = spb->song_pos_ppqn;
    int pos1 = cbox_master_ppqn_to_samples(spb->master, ppos);
    int pos2 = cbox_master_ppqn_to_samples(spb->master, ppos + 1);
    double relpos = 0.0;
    if (pos1 != pos2)
        relpos = (spb->song_pos_samples - pos1) * 1.0 / (pos2 - pos1);
    spb->master->tempo = tempo;

    // This seek loses the fractional value of the PPQN song position.
    // This needs to be compensated for by shifting the playback
    // position by the fractional part.
    cbox_song_playback_seek_ppqn(spb, ppos, spb->min_time_ppqn);
    if (relpos > 0)
    {
        pos2 = cbox_master_ppqn_to_samples(spb->master, ppos + 1);
        cbox_song_playback_seek_samples(spb, spb->song_pos_samples + (pos2 - spb->song_pos_samples) * relpos + 0.5);
    }
}

int cbox_song_playback_get_next_tempo_change(struct cbox_song_playback *spb)
{
    double new_tempo = 0;
    // Skip items at or already past the playback pointer
    while (spb->tempo_map_pos + 1 < spb->tempo_map_item_count && 
        spb->song_pos_samples >= spb->tempo_map_items[spb->tempo_map_pos + 1].time_samples)
    {
        new_tempo = spb->tempo_map_items[spb->tempo_map_pos + 1].tempo;
        spb->tempo_map_pos++;
    }
    if (new_tempo != 0.0 && new_tempo != spb->master->tempo) {
        cbox_song_playback_set_tempo(spb, new_tempo);
    }
        
    // No more items?
    if (spb->tempo_map_pos + 1 >= spb->tempo_map_item_count)
        return -1;
    
    return spb->tempo_map_items[spb->tempo_map_pos + 1].time_samples;
}

void cbox_song_playback_prepare_render(struct cbox_song_playback *spb)
{
    for(uint32_t i = 0; i < spb->track_count; i++)
    {
        cbox_midi_buffer_clear(&spb->tracks[i]->output_buffer);
    }
}

void cbox_song_playback_render(struct cbox_song_playback *spb, struct cbox_midi_buffer *output, uint32_t nsamples)
{
    cbox_midi_buffer_clear(output);
    
    if (spb->master->new_tempo != 0)
    {
        if (spb->master->new_tempo != spb->master->tempo)
            cbox_song_playback_set_tempo(spb, spb->master->new_tempo);
        spb->master->new_tempo = 0;
    }
    if (spb->master->state == CMTS_STOPPING)
    {
        if (cbox_song_playback_active_notes_release(spb, NULL, 0, output) > 0)
            spb->master->state = CMTS_STOP;
    }
    else
    if (spb->master->state == CMTS_ROLLING)
    {                
        uint32_t end_samples = cbox_master_ppqn_to_samples(spb->master, spb->loop_end_ppqn);
        
        uint32_t rpos = 0;
        while (rpos < nsamples)
        {
            uint32_t rend = nsamples;
            
            // 1. Shorten the period so that it doesn't go past a tempo change
            int tmpos = cbox_song_playback_get_next_tempo_change(spb);
            if (tmpos != -1)
            {
                // Number of samples until the next tempo change
                uint32_t stntc = tmpos - spb->song_pos_samples;
                if (rend - rpos > stntc)
                    rend = rpos + stntc;
            }
            
            // 2. Shorten the period so that it doesn't go past the song length
            uint32_t end_pos = spb->song_pos_samples + (rend - rpos);
            if (end_pos >= end_samples)
            {
                rend = end_samples - spb->song_pos_samples;
                end_pos = end_samples;
            }
            
            if (rend > rpos)
            {
                for (uint32_t i = 0; i < spb->track_count; i++)
                    cbox_track_playback_render(spb->tracks[i], rpos, rend - rpos);
            }
            
            if (end_pos < end_samples)
            {
                spb->song_pos_samples += rend - rpos;
                // XXXKF optimize
                spb->min_time_ppqn = cbox_master_samples_to_ppqn(spb->master, spb->song_pos_samples - 1) + 1;
                spb->song_pos_ppqn = cbox_master_samples_to_ppqn(spb->master, spb->song_pos_samples);
            }
            else
            {
                if (spb->loop_start_ppqn >= spb->loop_end_ppqn)
                {
                    spb->song_pos_samples = end_samples;
                    spb->song_pos_ppqn = spb->loop_end_ppqn;
                    spb->master->state = CMTS_STOPPING;
                    break;
                }

                cbox_song_playback_seek_ppqn(spb, spb->loop_start_ppqn, spb->loop_start_ppqn);
            }
            rpos = rend;
        }
        cbox_midi_merger_render_to(&spb->track_merger, output);
    }
}

int cbox_song_playback_active_notes_release(struct cbox_song_playback *spb, struct cbox_song_playback *new_spb, uint32_t new_pos, struct cbox_midi_buffer *buf)
{
    // Release notes from deleted tracks
    for(uint32_t i = 0; i < spb->track_count; i++)
    {
        struct cbox_track_playback *trk = spb->tracks[i];
        if (new_spb && trk->state_copied)
            continue;
        struct cbox_midi_buffer *output = trk->external_merger ? &trk->output_buffer : buf;
        if (cbox_midi_playback_active_notes_release(&trk->active_notes, output, NULL) < 0)
            return 0;
    }
    // Release notes from removed/modified clips
    if (new_spb) {
        for(uint32_t i = 0; i < new_spb->track_count; i++)
        {
            struct cbox_track_playback *new_trk = new_spb->tracks[i];
            if (!new_trk->active_notes.channels_active)
                continue;
            // struct cbox_track_playback *old_trk = new_trk->old_state;
            // if (!old_trk)
            //     continue;
            struct cbox_midi_buffer *output = new_trk->external_merger ? &new_trk->output_buffer : buf;
            struct cbox_midi_playback_active_notes stuck_notes;
            cbox_midi_playback_active_notes_copy(&stuck_notes, &new_trk->active_notes);
            cbox_track_confirm_stuck_notes(new_trk, &stuck_notes, new_pos);
            if (cbox_midi_playback_active_notes_release(&stuck_notes, output, &new_trk->active_notes) < 0)
                return 0;
        }
    }
    return 1;
}

void cbox_song_playback_seek_ppqn(struct cbox_song_playback *spb, int time_ppqn, int min_time_ppqn)
{
    for(uint32_t i = 0; i < spb->track_count; i++)
    {
        struct cbox_track_playback *trk = spb->tracks[i];
        cbox_track_playback_seek_ppqn(trk, time_ppqn, min_time_ppqn);
    }
    spb->song_pos_samples = cbox_master_ppqn_to_samples(spb->master, time_ppqn);
    spb->song_pos_ppqn = time_ppqn;
    spb->min_time_ppqn = min_time_ppqn;
    spb->tempo_map_pos = cbox_song_playback_tmi_from_ppqn(spb, time_ppqn);
}

void cbox_song_playback_seek_samples(struct cbox_song_playback *spb, uint32_t time_samples)
{
    for(uint32_t i = 0; i < spb->track_count; i++)
    {
        struct cbox_track_playback *trk = spb->tracks[i];
        cbox_track_playback_seek_samples(trk, time_samples);
    }
    spb->song_pos_samples = time_samples;
    spb->song_pos_ppqn = cbox_master_samples_to_ppqn(spb->master, time_samples);
    spb->min_time_ppqn = spb->song_pos_ppqn;
    spb->tempo_map_pos = cbox_song_playback_tmi_from_samples(spb, time_samples);
}

int cbox_song_playback_tmi_from_ppqn(struct cbox_song_playback *spb, uint32_t time_ppqn)
{
    if (!spb->tempo_map_item_count)
        return -1;
    assert(spb->tempo_map_items[0].time_samples == 0);
    assert(spb->tempo_map_items[0].time_ppqn == 0);
    // XXXKF should use binary search here really
    for (int i = 1; i < spb->tempo_map_item_count; i++)
    {
        if (time_ppqn < spb->tempo_map_items[i].time_ppqn)
            return i - 1;
    }
    return spb->tempo_map_item_count - 1;
}

int cbox_song_playback_tmi_from_samples(struct cbox_song_playback *spb, uint32_t time_samples)
{
    if (!spb->tempo_map_item_count)
        return -1;
    assert(spb->tempo_map_items[0].time_samples == 0);
    assert(spb->tempo_map_items[0].time_ppqn == 0);
    // XXXKF should use binary search here really
    for (int i = 1; i < spb->tempo_map_item_count; i++)
    {
        if (time_samples < spb->tempo_map_items[i].time_samples)
            return i - 1;
    }
    return spb->tempo_map_item_count - 1;
}

struct cbox_midi_pattern_playback *cbox_song_playback_get_pattern(struct cbox_song_playback *spb, struct cbox_midi_pattern *pattern)
{
    struct cbox_midi_pattern_playback *mppb = g_hash_table_lookup(spb->pattern_map, pattern);
    if (mppb) {
        cbox_midi_pattern_playback_ref(mppb);
        return mppb;
    }
    
    mppb = cbox_midi_pattern_playback_new(pattern);
    g_hash_table_insert(spb->pattern_map, pattern, mppb);

    return mppb;
}

void cbox_song_playback_destroy(struct cbox_song_playback *spb)
{
    cbox_midi_merger_close(&spb->track_merger, spb->engine->rt);
    for (uint32_t i = 0; i < spb->track_count; i++)
    {
        if (!(--spb->tracks[i]->ref_count))
            cbox_track_playback_destroy(spb->tracks[i]);
    }
    free(spb->tempo_map_items);
    free(spb->tracks);
    g_hash_table_destroy(spb->pattern_map);
    free(spb);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t cbox_song_time_mapper_map_time(struct cbox_time_mapper *tmap, uint32_t free_running_counter)
{
    struct cbox_song_time_mapper *stmap = (struct cbox_song_time_mapper *)tmap;
    struct cbox_engine *engine = stmap->engine;

    if (!engine->spb || engine->master->state != CMTS_ROLLING)
        return free_running_counter & 0x7FFFFFFF;

    int32_t rel_samples = free_running_counter - engine->rt->io->free_running_frame_counter;
    if (rel_samples < 0 || rel_samples >= 1048576)
        return (uint32_t)-1;
    uint32_t abs_samples = engine->spb->song_pos_samples + rel_samples;
    uint32_t loop_end_samples = cbox_master_ppqn_to_samples(engine->master, engine->spb->loop_end_ppqn);
    if (abs_samples >= loop_end_samples) {
        // Correct for looping
        uint32_t loop_start_samples = cbox_master_ppqn_to_samples(engine->master, engine->spb->loop_start_ppqn);
        if (loop_start_samples < loop_end_samples) {
            uint32_t loop_length = loop_end_samples - loop_start_samples;
            abs_samples = loop_start_samples + (abs_samples - loop_start_samples) % loop_length;
        }
    }
    uint32_t abs_ppqn = cbox_master_samples_to_ppqn(engine->master, abs_samples);
    return abs_ppqn | 0x80000000;
}

void cbox_song_time_mapper_init(struct cbox_song_time_mapper *tmap, struct cbox_engine *engine)
{
    tmap->tmap.map_time = cbox_song_time_mapper_map_time;
    tmap->engine = engine;
}
