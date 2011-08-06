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

#include "pattern.h"
#include "seq.h"
#include "song.h"
#include "track.h"

static inline void accumulate_event(struct cbox_midi_playback_active_notes *notes, const struct cbox_midi_event *event)
{
    if (event->size != 3)
        return;
    // this ignores poly aftertouch - which, I supposed, is OK for now
    if (event->data_inline[0] < 0x80 || event->data_inline[0] > 0x9F)
        return;
    int ch = event->data_inline[0] & 0x0F;
    if (event->data_inline[0] >= 0x90 && event->data_inline[2] > 0)
    {
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

struct cbox_track_playback *cbox_track_playback_new_from_track(struct cbox_track *track, struct cbox_master *master)
{
    struct cbox_track_playback *pb = malloc(sizeof(struct cbox_track_playback));
    pb->master = master;
    int len = g_list_length(track->items);
    pb->items = malloc(len * sizeof(struct cbox_track_playback_item));
    
    GList *it = track->items;
    struct cbox_track_playback_item *p = pb->items;
    uint32_t safe = 0;
    while(it != NULL)
    {
        struct cbox_track_item *item = it->data;
        // if items overlap, the first one takes precedence
        if (item->time < safe)
        {
            // fully contained in previous item? skip all of it
            // not fully contained - insert the fragment
            if (item->time + item->length >= safe)
            {
                int cut = safe - item->time;
                p->time = safe;
                p->pattern = item->pattern;
                p->offset = item->offset + cut;
                p->length = item->length - cut;
                p++;
            }
        }
        else
        {
            p->time = item->time;
            p->pattern = item->pattern;
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
    cbox_midi_pattern_playback_init(&pb->playback, &pb->active_notes, master);
    cbox_midi_playback_active_notes_init(&pb->active_notes);
    cbox_midi_buffer_init(&pb->output_buffer);
    cbox_track_playback_start_item(pb, 0);
    
    return pb;
}
    
void cbox_track_playback_seek_ppqn(struct cbox_track_playback *pb, int time_ppqn)
{
    pb->pos = 0;
    while(pb->pos < pb->items_count && pb->items[pb->pos].time + pb->items[pb->pos].length < time_ppqn)
        pb->pos++;
    cbox_track_playback_start_item(pb, cbox_master_ppqn_to_samples(pb->master, time_ppqn));
}

void cbox_track_playback_seek_samples(struct cbox_track_playback *pb, int time_samples)
{
    pb->pos = 0;
    while(pb->pos < pb->items_count && cbox_master_ppqn_to_samples(pb->master, pb->items[pb->pos].time + pb->items[pb->pos].length) < time_samples)
        pb->pos++;
    cbox_track_playback_start_item(pb, time_samples);
}

void cbox_track_playback_start_item(struct cbox_track_playback *pb, int time_samples)
{
    if (pb->pos >= pb->items_count)
    {
        return;
    }
    struct cbox_track_playback_item *cur = &pb->items[pb->pos];
    int time_ppqn = cbox_master_samples_to_ppqn(pb->master, time_samples);
    int start_time_ppqn = cur->time, end_time_ppqn = cur->time + cur->length;
    int start_time_samples = cbox_master_ppqn_to_samples(pb->master, start_time_ppqn);
    int end_time_samples = cbox_master_ppqn_to_samples(pb->master, end_time_ppqn);
    cbox_midi_pattern_playback_set_pattern(&pb->playback, cur->pattern, start_time_samples, end_time_samples, cur->offset);
    
    if (time_ppqn < start_time_ppqn)
        cbox_midi_pattern_playback_seek_ppqn(&pb->playback, 0);
    else
        cbox_midi_pattern_playback_seek_ppqn(&pb->playback, time_ppqn - start_time_ppqn);    
}

void cbox_track_playback_render(struct cbox_track_playback *pb, int offset, int nsamples)
{
    int rpos = 0;
    while(rpos < nsamples && pb->pos < pb->items_count)
    {
        int rend = nsamples;
        struct cbox_track_playback_item *cur = &pb->items[pb->pos];
        // a gap before the current item
        if (pb->master->song->song_pos_samples + rpos < pb->playback.start_time_samples)
        {
            int space_samples = pb->playback.start_time_samples - (pb->master->song->song_pos_samples + rpos);
            if (space_samples >= rend - rpos)
                return;
            rpos += space_samples;
            offset += space_samples;
        }
        // check if item finished
        int cur_segment_end_samples = cbox_master_ppqn_to_samples(pb->master, cur->time + cur->length);
        int render_end_samples = pb->master->song->song_pos_samples + rend;
        if (render_end_samples > cur_segment_end_samples)
        {
            rend = cur_segment_end_samples - pb->master->song->song_pos_samples;
            cbox_midi_pattern_playback_render(&pb->playback, &pb->output_buffer, offset, rend - rpos);
            pb->pos++;
            cbox_track_playback_start_item(pb, cur_segment_end_samples);
        }
        else
            cbox_midi_pattern_playback_render(&pb->playback, &pb->output_buffer, offset, rend - rpos);
        offset += rend - rpos;
        rpos = rend;
    }
}

void cbox_track_playback_destroy(struct cbox_track_playback *pb)
{
    free(pb->items);
    free(pb);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void cbox_midi_pattern_playback_init(struct cbox_midi_pattern_playback *pb, struct cbox_midi_playback_active_notes *active_notes, struct cbox_master *master)
{
    pb->pattern = NULL;
    pb->master = master;
    pb->pos = 0;
    pb->rel_time_samples = 0;
    pb->start_time_samples = 0;
    pb->end_time_samples = 0;
    pb->active_notes = active_notes;
    // cbox_midi_playback_active_notes_init(active_notes);
}

void cbox_midi_pattern_playback_set_pattern(struct cbox_midi_pattern_playback *pb, struct cbox_midi_pattern *pattern, int start_time_samples, int end_time_samples, int offset_ppqn)
{
    pb->pattern = pattern;
    pb->pos = 0;
    pb->rel_time_samples = 0;
    pb->start_time_samples = start_time_samples;
    pb->end_time_samples = end_time_samples;
    pb->offset_ppqn = offset_ppqn;
}

void cbox_midi_pattern_playback_render(struct cbox_midi_pattern_playback *pb, struct cbox_midi_buffer *buf, int offset, int nsamples)
{
    uint32_t end_time_samples = pb->end_time_samples;
    uint32_t cur_time_samples = pb->start_time_samples + pb->rel_time_samples;
    
    if (end_time_samples > cur_time_samples + nsamples)
        end_time_samples = cur_time_samples + nsamples;

    while(1)
    {
        if (pb->pos >= pb->pattern->event_count)
            break;
        const struct cbox_midi_event *src = &pb->pattern->events[pb->pos];
        
        int event_time_samples = cbox_master_ppqn_to_samples(pb->master, src->time - pb->offset_ppqn) + pb->start_time_samples;
    
        if (event_time_samples >= end_time_samples)
            break;
        int32_t time = 0;
        if (event_time_samples >= cur_time_samples) // convert negative relative time to 0 time
            time = event_time_samples - cur_time_samples;
        
        cbox_midi_buffer_copy_event(buf, src, offset + time);
        if (pb->active_notes)
            accumulate_event(pb->active_notes, src);
        pb->pos++;
    }
    pb->rel_time_samples += nsamples;
}

void cbox_midi_pattern_playback_seek_ppqn(struct cbox_midi_pattern_playback *pb, int time_ppqn)
{
    int pos = 0;
    int patrel_time_ppqn = time_ppqn + pb->offset_ppqn;
    while (pos < pb->pattern->event_count && patrel_time_ppqn > pb->pattern->events[pos].time)
        pos++;
    pb->rel_time_samples = cbox_master_ppqn_to_samples(pb->master, time_ppqn) - pb->start_time_samples;
    pb->pos = pos;
}

void cbox_midi_pattern_playback_seek_samples(struct cbox_midi_pattern_playback *pb, int time_samples)
{
    int pos = 0;
    while (pos < pb->pattern->event_count && time_samples > cbox_master_ppqn_to_samples(pb->master, pb->pattern->events[pos].time - pb->offset_ppqn))
        pos++;
    pb->rel_time_samples = time_samples;
    pb->pos = pos;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void cbox_midi_playback_active_notes_init(struct cbox_midi_playback_active_notes *notes)
{
    notes->channels_active = 0;
}

int cbox_midi_playback_active_notes_release(struct cbox_midi_playback_active_notes *notes, struct cbox_midi_buffer *buf)
{
    if (!notes->channels_active)
        return 0;
    int note_offs = 0;
    for (int c = 0; c < 16; c++)
    {
        if (!notes->channels_active & (1 << c))
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
                note_offs++;
            }
        }
        // all Note Offs emitted without buffer overflow - channel is no longer active
        notes->channels_active &= ~(1 << c);
    }
    return note_offs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

#define MAX_TRACKS 16

void cbox_song_render(struct cbox_song *song, struct cbox_midi_buffer *output, int nsamples)
{
    struct cbox_midi_buffer *midibufsrcs[MAX_TRACKS];
    
    cbox_midi_buffer_clear(output);
    
    for(GList *p = song->tracks; p; p = g_list_next(p))
    {
        struct cbox_track *trk = p->data;        
        cbox_midi_buffer_clear(&trk->pb->output_buffer);
    }
    
    int end_samples = cbox_master_ppqn_to_samples(song->master, song->loop_end_ppqn);
    
    int rpos = 0;
    while (rpos < nsamples)
    {
        int rend = nsamples;
        int end_pos = song->song_pos_samples + (rend - rpos);
        if (end_pos >= end_samples)
        {
            rend = end_samples - song->song_pos_samples;
            end_pos = end_samples;
        }
        
        if (rend > rpos)
        {
            for(GList *p = song->tracks; p; p = g_list_next(p))
            {
                struct cbox_track *trk = p->data;                
                cbox_track_playback_render(trk->pb, rpos, rend - rpos);
            }
        }
        
        if (end_pos < end_samples)
        {
            song->song_pos_samples += rend - rpos;
            song->song_pos_ppqn = cbox_master_samples_to_ppqn(song->master, song->song_pos_samples);
        }
        else
        {
            if (song->loop_start_ppqn >= song->loop_end_ppqn)
                return;
                
            cbox_song_seek_ppqn(song, song->loop_start_ppqn);
        }
        rpos = rend;
    }
    
    int nt = 0;
    int bpos[MAX_TRACKS];
    for(GList *p = song->tracks; p; nt++, p = g_list_next(p))
    {
        struct cbox_track *trk = p->data;        
        midibufsrcs[nt] = &trk->pb->output_buffer;
        bpos[nt] = 0;
    }
    cbox_midi_buffer_merge(output, midibufsrcs, nt, bpos);
}

int cbox_song_active_notes_release(struct cbox_song *song, struct cbox_midi_buffer *buf)
{
    for(GList *p = song->tracks; p; p = g_list_next(p))
    {
        struct cbox_track *trk = p->data;
        if (!cbox_midi_playback_active_notes_release(&trk->pb->active_notes, buf))
            return 0;
    }
    return 1;
}

void cbox_song_seek_ppqn(struct cbox_song *song, int time_ppqn)
{
    for(GList *p = song->tracks; p; p = g_list_next(p))
    {
        struct cbox_track *trk = p->data;
        cbox_track_playback_seek_ppqn(trk->pb, time_ppqn);
    }
    song->song_pos_samples = cbox_master_ppqn_to_samples(song->master, time_ppqn);
    song->song_pos_ppqn = time_ppqn;
}

void cbox_song_seek_samples(struct cbox_song *song, int time_samples)
{
    for(GList *p = song->tracks; p; p = g_list_next(p))
    {
        struct cbox_track *trk = p->data;
        cbox_track_playback_seek_samples(trk->pb, time_samples);
    }
    song->song_pos_samples = time_samples;
    song->song_pos_ppqn = cbox_master_samples_to_ppqn(song->master, time_samples);
}
