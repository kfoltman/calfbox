/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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
#include "io.h"
#include "module.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <jack/midiport.h>

struct process_struct
{
    struct cbox_module *module;
};

void dummy_process(void *user_data, struct cbox_io *io, uint32_t nframes)
{
    struct process_struct *ps = user_data;
    struct cbox_module *module = ps->module;
    if (!module)
        return;
    uint32_t i;
    float *out_l = jack_port_get_buffer(io->output_l, nframes);
    float *out_r = jack_port_get_buffer(io->output_r, nframes);
    void *midi = jack_port_get_buffer(io->midi, nframes);
    int event_count = jack_midi_get_event_count(midi);
    int cur_event = 0;
    uint32_t highwatermark = 0;
    
    for (i = 0; i < nframes; i += 16)
    {
        cbox_sample_t *outputs[2] = {out_l + i, out_r + i};
        
        if (i >= highwatermark)
        {
            while(cur_event < event_count)
            {
                jack_midi_event_t event;
                if (!jack_midi_event_get(&event, midi, cur_event))
                {
                    if (event.time <= i)
                        (*module->process_event)(module->user_data, event.buffer, event.size);
                    else
                    {
                        highwatermark = event.time;
                        break;
                    }
                }
                else
                    break;
                
                cur_event++;
            }
        }
        (*module->process_block)(module->user_data, NULL, outputs);
    }
    while(cur_event < event_count)
    {
        jack_midi_event_t event;
        if (!jack_midi_event_get(&event, midi, cur_event))
        {
            (*module->process_event)(module->user_data, event.buffer, event.size);
        }
        else
            break;
        
        cur_event++;
    }
}

int main(int argc, char *argv[])
{
    struct cbox_io io;
    struct cbox_open_params params;
    char buf[3];
    struct process_struct process = { NULL };
    struct cbox_io_callbacks cbs = { &process, dummy_process};
    const char *module = NULL;
    struct cbox_module_manifest **mptr;

    cbox_config_init();

    module = cbox_config_get_string_with_default("instrument:default", "engine", "tonewheel_organ");
    
    for (mptr = cbox_module_list; *mptr; mptr++)
    {
        if (!strcmp((*mptr)->name, module))
        {
            process.module = (*(*mptr)->create)(&tonewheel_organ_module, "instrument:default");
            break;
        }
    }
    if (!process.module)
    {
        fprintf(stderr, "Cannot find module %s\n", module);
        return 1;
    }

    if (!cbox_io_init(&io, &params))
    {
        fprintf(stderr, "Cannot initialise sound I/O\n");
        return 1;
    }
    cbox_io_start(&io, &cbs);
    fgets(buf, 2, stdin);
    cbox_io_stop(&io);
    cbox_io_close(&io);
    cbox_config_close();
    return 0;
}
