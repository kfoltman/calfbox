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

#include "io.h"
#include "layer.h"
#include "midi.h"
#include "module.h"
#include "procmain.h"
#include "scene.h"
#include <jack/jack.h>
#include <jack/types.h>
#include <jack/midiport.h>

int convert_midi_from_jack(jack_port_t *port, uint32_t nframes, struct cbox_scene *scene)
{
    void *midi = jack_port_get_buffer(port, nframes);
    uint32_t i;
    int l;
    uint32_t event_count = jack_midi_get_event_count(midi);
    int ok = 1;

    for (i = 0; i < scene->instrument_count; i++)
    {
        cbox_midi_buffer_clear(&scene->instruments[i]->midi_input);
    }
    for (i = 0; i < event_count; i++)
    {
        jack_midi_event_t event;
        
        if (!jack_midi_event_get(&event, midi, i))
        {
            // XXXKF ignore sysex for now
            if (event.size >= 4)
                continue;
            
            for (l = 0; l < scene->layer_count; l++)
            {
                struct cbox_layer *lp = scene->layers[l];
                uint8_t data[4];
                memcpy(data, event.buffer, event.size);
                if (data[0] < 0xF0) // per-channel messages
                {
                    int cmd = data[0] >> 4;
                    // filter on MIDI channel
                    if (lp->in_channel >= 0 && lp->in_channel != (data[0] & 0x0F))
                        continue;
                    // force output channel
                    if (lp->out_channel >= 0)
                        data[0] = (data[0] & 0xF0) + (lp->out_channel & 0x0F);
                    if (cmd >= 8 && cmd <= 10)
                    {
                        // note filter
                        if (data[1] < lp->low_note || data[1] > lp->high_note)
                            continue;
                        // transpose
                        if (lp->transpose)
                        {
                            int note = data[1] + lp->transpose;
                            if (note < 0 || note > 127)
                                continue;
                            data[1] = (uint8_t)note;
                        }
                        // fixed note
                        if (lp->fixed_note != -1)
                        {
                            data[1] = (uint8_t)lp->fixed_note;
                        }
                    }
                }
                if (!cbox_midi_buffer_write_event(&lp->output->midi_input, event.time, data, event.size))
                    return -i;
            }
        }
        else
            return -i;
    }
    
    return event_count;
}

void main_process(void *user_data, struct cbox_io *io, uint32_t nframes)
{
    struct cbox_process_struct *ps = user_data;
    struct cbox_scene *scene = ps->scene;
    struct cbox_module *effect = ps->effect;
    if (!scene)
        return;
    uint32_t i, j, n;
    float *out_l = jack_port_get_buffer(io->output_l, nframes);
    float *out_r = jack_port_get_buffer(io->output_r, nframes);

    convert_midi_from_jack(io->midi, nframes, scene);
    
    for (i = 0; i < nframes; i ++)
    {
        out_l[i] = out_r[i] = 0.f;
    }
    
    for (n = 0; n < scene->instrument_count; n++)
    {
        struct cbox_module *module = scene->instruments[n];
        int event_count = module->midi_input.count;
        int cur_event = 0;
        uint32_t highwatermark = 0;
        
        for (i = 0; i < nframes; i += 16)
        {
            cbox_sample_t left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            cbox_sample_t *outputs[2] = {left, right};
            
            if (i >= highwatermark)
            {
                while(cur_event < event_count)
                {
                    struct cbox_midi_event *event = cbox_midi_buffer_get_event(&module->midi_input, cur_event);
                    if (event)
                    {
                        if (event->time <= i)
                            (*module->process_event)(module, cbox_midi_event_get_data(event), event->size);
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
            (*module->process_block)(module, NULL, outputs);
            for (j = 0; j < CBOX_BLOCK_SIZE; j++)
            {
                out_l[i + j] += left[j];
                out_r[i + j] += right[j];
            }
        }
        while(cur_event < event_count)
        {
            struct cbox_midi_event *event = cbox_midi_buffer_get_event(&module->midi_input, cur_event);
            if (event)
            {
                (*module->process_event)(module, cbox_midi_event_get_data(event), event->size);
            }
            else
                break;
            
            cur_event++;
        }
    }
    if (effect)
    {
        for (i = 0; i < nframes; i += 16)
        {
            cbox_sample_t left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            cbox_sample_t *in_bufs[2] = {out_l + i, out_r + i};
            cbox_sample_t *out_bufs[2] = {left, right};
            (*effect->process_block)(effect, in_bufs, out_bufs);
            for (j = 0; j < CBOX_BLOCK_SIZE; j++)
            {
                out_l[i + j] = left[j];
                out_r[i + j] = right[j];
            }
        }
    }    
}

