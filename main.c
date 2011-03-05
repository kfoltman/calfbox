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
#include "midi.h"
#include "module.h"

#include <glib.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <jack/midiport.h>

struct process_struct
{
    struct cbox_module *module;
};

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

void dummy_process(void *user_data, struct cbox_io *io, uint32_t nframes)
{
    struct process_struct *ps = user_data;
    struct cbox_module *module = ps->module;
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

static const char *short_options = "i:c:h";

static struct option long_options[] = {
    {"help", 0, 0, 'h'},
    {"instrument", 0, 0, 'i'},
    {"config", 0, 0, 'c'},
    {0,0,0,0},
};

void print_help(char *progname)
{
    printf("Usage: %s [--help] [--instrument <name>] [--config <name>]\n", progname);
    exit(0);
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
    const char *config_name = NULL;
    const char *instrument_name = "default";
    char *instr_section;

    while(1)
    {
        int option_index;
        int c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;
        switch (c)
        {
            case 'c':
                config_name = optarg;
                break;
            case 'i':
                instrument_name = optarg;
                break;
            case 'h':
            case '?':
                print_help(argv[0]);
                return 0;
        }
    }

    cbox_config_init(config_name);

    instr_section = g_strdup_printf("instrument:%s", instrument_name);
    module = cbox_config_get_string_with_default(instr_section, "engine", "tonewheel_organ");
    
    for (mptr = cbox_module_list; *mptr; mptr++)
    {
        if (!strcmp((*mptr)->name, module))
        {
            cbox_module_manifest_dump(*mptr);
            process.module = (*(*mptr)->create)((*mptr)->user_data, instr_section);
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
    g_free(instr_section);
    
    return 0;
}
