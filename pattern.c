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

#include <glib.h>

void cbox_read_pattern(struct cbox_midi_pattern_playback *pb, struct cbox_midi_buffer *buf, int nsamples)
{
    cbox_midi_buffer_clear(buf);
    int loop_end = pb->pattern->loop_end;
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
        int32_t tshift = -pb->time;
        if (src->time >= pb->time + nsamples)
            break;
        if (src->time < pb->time) // convert negative relative time to 0 time
            tshift = -src->time;
        
        cbox_midi_buffer_copy_event(buf, src, tshift);
        pb->pos++;
    }
    pb->time += nsamples;
}

struct cbox_midi_pattern *cbox_midi_pattern_new_metronome(float bpm, int ts, int srate)
{
    struct cbox_midi_pattern *p = malloc(sizeof(struct cbox_midi_pattern));
    int length = (int)(srate * 60 / bpm);
    p->event_count = 2 * ts;
    p->events = malloc(sizeof(struct cbox_midi_event[1]) * p->event_count);
    int channel = cbox_config_get_int("metronome", "channel", 10);
    int accnote = cbox_config_get_note("metronome", "note_accent", 37);
    int note = cbox_config_get_note("metronome", "note", 37);
    
    for (int i = 0; i < ts; i++)
    {
        int e = 2 * i;
        int accent = !i && ts != 1;
        p->events[e].time = length * i;
        p->events[e].size = 3;
        p->events[e].data_inline[0] = 0x90 + channel - 1;
        p->events[e].data_inline[1] = accent ? accnote : note;
        p->events[e].data_inline[2] = accent ? 100 : 127;

        p->events[e + 1].time = length * i + 1;
        p->events[e + 1].size = 3;
        p->events[e + 1].data_inline[0] = 0x80 + channel - 1;
        p->events[e + 1].data_inline[1] = accent ? accnote : note;
        p->events[e + 1].data_inline[2] = 0;
    }
    
    p->loop_end = length * ts;

    return p;
}

void cbox_midi_pattern_destroy(struct cbox_midi_pattern *pattern)
{
    if (pattern->event_count)
        free(pattern->events);
    free(pattern);
}
