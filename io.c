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

#include "config.h"
#include "config-api.h"
#include "io.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>

static const char *io_section = "io";

int cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params)
{
    jack_status_t status = 0;
    io->client = jack_client_open("cbox", 0, &status);
    io->cb = NULL;
    
    if (io->client == NULL)
        return 0;

    io->output_l = jack_port_register(io->client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    io->output_r = jack_port_register(io->client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    io->midi = jack_port_register(io->client, "midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    
    if (!io->output_l || !io->output_r || !io->midi)
        return 0;
    
    return 1;
};

static int process_cb(jack_nframes_t frames, void *arg)
{
    struct cbox_io *io = arg;
    struct cbox_io_callbacks *cb = io->cb;
    
    cb->process(cb->user_data, io, frames);
    return 0;
}

static void autoconnect(jack_client_t *client, const char *port, const char *config_var, int is_cbox_input, int is_midi)
{
    char *name, *orig_name, *dpos;
    const char *use_name;
    
    orig_name = cbox_config_get_string(io_section, config_var);
    if (orig_name)
    {
        name = orig_name;
        do {
            dpos = strchr(name, ';');
            if (dpos)
                *dpos = '\0';
            
            use_name = name;
            if (use_name[0] == '#')
            {
                char *endptr = NULL;
                long port = strtol(use_name + 1, &endptr, 10);
                if (endptr == use_name + strlen(use_name))
                {
                    const char **names = jack_get_ports(client, ".*", is_midi ? JACK_DEFAULT_MIDI_TYPE : JACK_DEFAULT_AUDIO_TYPE, is_cbox_input ? JackPortIsOutput : JackPortIsInput);
                    int i;
                    for (i = 0; i < port && names[i]; i++)
                        ;
                    
                    if (names[i])
                        use_name = names[i];
                }
            }
            if (is_cbox_input)
                jack_connect(client, use_name, port);
            else
                jack_connect(client, port, use_name);    
            g_message("Connect: %s %s %s", port, is_cbox_input ? "<-" : "->", use_name);
            if (dpos)
                name = dpos + 1;
        } while(dpos);
        g_free(orig_name);
    }
}

int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb)
{
    io->cb = cb;
    jack_set_process_callback(io->client, process_cb, io);
    jack_activate(io->client);

    if (cbox_config_has_section(io_section))
    {
        autoconnect(io->client, "cbox:out_l", "out_left", 0, 0);
        autoconnect(io->client, "cbox:out_r", "out_right", 0, 0);
        autoconnect(io->client, "cbox:midi", "midi", 1, 1);
    }
    
    return 1;
}

int cbox_io_stop(struct cbox_io *io)
{
    jack_deactivate(io->client);
    return 1;
}

void cbox_io_close(struct cbox_io *io)
{
    if (io->client)
    {
        if (io->output_l)
            jack_port_unregister(io->client, io->output_l);
        if (io->output_r)
            jack_port_unregister(io->client, io->output_r);
        if (io->midi)
            jack_port_unregister(io->client, io->midi);
        
        jack_client_close(io->client);
    }
}

