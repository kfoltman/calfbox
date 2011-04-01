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

#include "procmain.h"
#include "midi.h"
#include "module.h"
#include "io.h"
#include <jack/jack.h>
#include <jack/types.h>
#include <jack/midiport.h>

int convert_midi_from_jack(jack_port_t *port, uint32_t nframes, struct cbox_midi_buffer *buf)
{
    void *midi = jack_port_get_buffer(port, nframes);
    uint32_t i;
    uint32_t event_count = jack_midi_get_event_count(midi);
    int ok = 1;

    cbox_midi_buffer_clear(buf);
    
    for (i = 0; i < event_count; i++)
    {
        jack_midi_event_t event;
        
        if (!jack_midi_event_get(&event, midi, i))
        {
            if (!cbox_midi_buffer_write_event(buf, event.time, event.buffer, event.size))
                return -i;
        }
        else
            return -i;
    }
    
    return event_count;
}

void main_process(void *user_data, struct cbox_io *io, uint32_t nframes)
{
    struct cbox_process_struct *ps = user_data;
    struct cbox_module *module = ps->module;
    struct cbox_module *effect = ps->effect;
    if (!module)
        return;
    struct cbox_midi_buffer midi_buf;
    cbox_midi_buffer_init(&midi_buf);
    uint32_t i;
    float *out_l = jack_port_get_buffer(io->output_l, nframes);
    float *out_r = jack_port_get_buffer(io->output_r, nframes);
    int event_count = 0;
    int cur_event = 0;
    uint32_t highwatermark = 0;

    event_count = abs(convert_midi_from_jack(io->midi, nframes, &midi_buf));
    
    for (i = 0; i < nframes; i += 16)
    {
        cbox_sample_t *outputs[2] = {out_l + i, out_r + i};
        
        if (i >= highwatermark)
        {
            while(cur_event < event_count)
            {
                struct cbox_midi_event *event = cbox_midi_buffer_get_event(&midi_buf, cur_event);
                if (event)
                {
                    if (event->time <= i)
                        (*module->process_event)(module->user_data, cbox_midi_event_get_data(event), event->size);
                    else
                    {
                        highwatermark = event->time;
                        break;
                    }
                }
                else
                    break;
                
                cur_event++;
            }
        }
        if (effect)
        {
            cbox_sample_t left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            cbox_sample_t *bufs[2] = {left, right};
            (*module->process_block)(module->user_data, NULL, bufs);
            (*effect->process_block)(effect->user_data, bufs, outputs);
        }
        else
            (*module->process_block)(module->user_data, NULL, outputs);
    }
    while(cur_event < event_count)
    {
        struct cbox_midi_event *event = cbox_midi_buffer_get_event(&midi_buf, cur_event);
        if (event)
        {
            (*module->process_event)(module->user_data, cbox_midi_event_get_data(event), event->size);
        }
        else
            break;
        
        cur_event++;
    }
    
}

