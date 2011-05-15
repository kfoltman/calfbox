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

#include "config-api.h"
#include "pattern.h"
#include "pattern-maker.h"

#include <glib.h>

int ppqn_to_samples(struct cbox_master *master, int time)
{
    return (int)(master->srate * 60.0 * time / (master->tempo * PPQN));
}

void cbox_read_pattern(struct cbox_midi_pattern_playback *pb, struct cbox_midi_buffer *buf, int nsamples)
{
    cbox_midi_buffer_clear(buf);
    int loop_end = ppqn_to_samples(pb->master, pb->pattern->loop_end);
    while(1)
    {
        if (pb->pos >= pb->pattern->event_count)
        {
            if (loop_end == -1)
                break;
            if (loop_end >= pb->time + nsamples)
                break;
            pb->pos = 0;
            pb->time -= loop_end; // may be negative, but that's OK
        }
        const struct cbox_midi_event *src = &pb->pattern->events[pb->pos];
        int srctime = ppqn_to_samples(pb->master, src->time);
        if (srctime >= pb->time + nsamples)
            break;
        int32_t time = 0;
        if (srctime >= pb->time) // convert negative relative time to 0 time
            time = srctime - pb->time;
        
        cbox_midi_buffer_copy_event(buf, src, time);
        pb->pos++;
    }
    pb->time += nsamples;
}

struct cbox_midi_pattern *cbox_midi_pattern_new_metronome(int ts, int srate)
{
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new();
    
    int length = PPQN;
    int channel = cbox_config_get_int("metronome", "channel", 10);
    int accnote = cbox_config_get_note("metronome", "note_accent", 37);
    int note = cbox_config_get_note("metronome", "note", 37);
    
    for (int i = 0; i < ts; i++)
    {
        int e = 2 * i;
        int accent = !i && ts != 1;
        cbox_midi_pattern_maker_add(m, length * i, 0x90 + channel - 1, accent ? accnote : note, accent ? 127 : 100);
        cbox_midi_pattern_maker_add(m, length * i + 1, 0x80 + channel - 1, accent ? accnote : note, 0);
    }
    
    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m);
    
    p->loop_end = length * ts;
    
    cbox_midi_pattern_maker_destroy(m);

    return p;
}

void cbox_midi_pattern_destroy(struct cbox_midi_pattern *pattern)
{
    if (pattern->event_count)
        free(pattern->events);
    free(pattern);
}
