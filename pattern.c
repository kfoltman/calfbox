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

void cbox_read_pattern(struct cbox_midi_pattern_playback *pb, struct cbox_midi_buffer *buf, int nsamples)
{
    int32_t rpos = 0;
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

struct cbox_midi_pattern *cbox_pattern_new()
{
    struct cbox_midi_pattern *p = malloc(sizeof(struct cbox_midi_pattern));
    p->event_count = 4;
    p->events = malloc(sizeof(struct cbox_midi_event[1]) * p->event_count);
    p->events[0].time = 0;
    p->events[0].size = 3;
    p->events[0].data_inline[0] = 0x90;
    p->events[0].data_inline[1] = 36;
    p->events[0].data_inline[2] = 127;

    p->events[1].time = 44099;
    p->events[1].size = 3;
    p->events[1].data_inline[0] = 0x90;
    p->events[1].data_inline[1] = 36;
    p->events[1].data_inline[2] = 0;

    p->events[2].time = 44100;
    p->events[2].size = 3;
    p->events[2].data_inline[0] = 0x90;
    p->events[2].data_inline[1] = 38;
    p->events[2].data_inline[2] = 127;
    
    p->events[3].time = 88199;
    p->events[3].size = 3;
    p->events[3].data_inline[0] = 0x90;
    p->events[3].data_inline[1] = 38;
    p->events[3].data_inline[2] = 0;
    
    p->loop_end = 88200;

    return p;
}
