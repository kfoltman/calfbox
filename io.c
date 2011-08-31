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

#include "config.h"
#include "config-api.h"
#include "hwcfg.h"
#include "io.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <jack/ringbuffer.h>
#include <jack/types.h>

static const char *io_section = "io";

int cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params)
{
    jack_status_t status = 0;
    io->client = jack_client_open("cbox", JackNoStartServer, &status);
    if (io->client == NULL)
    {
        if (!cbox_hwcfg_setup_jack())
            return 0;
        
        status = 0;
        io->client = jack_client_open("cbox", 0, &status);
    }
    // XXXKF would use a callback instead
    io->buffer_size = jack_get_buffer_size(io->client);
    io->cb = NULL;
    
    if (io->client == NULL)
        return 0;

    io->input_count = cbox_config_get_int("io", "inputs", 0);
    io->inputs = malloc(sizeof(jack_port_t *) * io->input_count);
    io->input_buffers = malloc(sizeof(float *) * io->input_count);
    for (int i = 0; i < io->input_count; i++)
    {
        gchar *name = g_strdup_printf("in_%d", 1 + i);
        io->inputs[i] = jack_port_register(io->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (!io->inputs[i])
        {
            // XXXKF switch to glib style error reporting, though this condition will probably rarely happen in real life
            g_error("Cannot create input port %d", i);
            return 0;
        }
        g_free(name);
    }
    io->output_count = cbox_config_get_int("io", "outputs", 2);
    io->outputs = malloc(sizeof(jack_port_t *) * io->output_count);
    io->output_buffers = malloc(sizeof(float *) * io->output_count);
    for (int i = 0; i < io->output_count; i++)
    {
        gchar *name = g_strdup_printf("out_%d", 1 + i);
        io->outputs[i] = jack_port_register(io->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!io->outputs[i])
        {
            // XXXKF switch to glib style error reporting, though this condition will probably rarely happen in real life
            g_error("Cannot create output port %d", i);
            return 0;
        }
        g_free(name);
    }
    io->midi = jack_port_register(io->client, "midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    
    if (!io->midi)
        return 0;
    
    return 1;
};

static int process_cb(jack_nframes_t frames, void *arg)
{
    struct cbox_io *io = arg;
    struct cbox_io_callbacks *cb = io->cb;
    
    for (int i = 0; i < io->input_count; i++)
        io->input_buffers[i] = NULL;
    io->buffer_size = frames;
    cb->process(cb->user_data, io, frames);
    return 0;
}

static void autoconnect_port(jack_client_t *client, const char *port, const char *use_name, int is_cbox_input, const jack_port_t *only_connect_port)
{
    int res;
    if (only_connect_port)
    {
        jack_port_t *right;
        right = jack_port_by_name(client, use_name);
        if (only_connect_port != right)
            return;
    }
    
    if (is_cbox_input)
        res = jack_connect(client, use_name, port);
    else
        res = jack_connect(client, port, use_name);    
    g_message("Connect: %s %s %s (%s)", port, is_cbox_input ? "<-" : "->", use_name, res == 0 ? "success" : (res == EEXIST ? "already connected" : "failed"));
}

static void autoconnect(jack_client_t *client, const char *port, const char *config_var, int is_cbox_input, int is_midi, const jack_port_t *only_connect_port)
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
                long portidx = strtol(use_name + 1, &endptr, 10) - 1;
                if (endptr == use_name + strlen(use_name))
                {
                    const char **names = jack_get_ports(client, ".*", is_midi ? JACK_DEFAULT_MIDI_TYPE : JACK_DEFAULT_AUDIO_TYPE, is_cbox_input ? JackPortIsOutput : JackPortIsInput);
                    int i;
                    for (i = 0; i < portidx && names[i]; i++)
                        ;
                    
                    if (names[i])
                        autoconnect_port(client, port, names[i], is_cbox_input, only_connect_port);
                    else
                        g_message("Connect: unmatched port index %d", (int)portidx);
                    
                    jack_free(names);
                }
            }
            else if (use_name[0] == '~' || use_name[0] == '*')
            {
                const char **names = jack_get_ports(client, use_name + 1, is_midi ? JACK_DEFAULT_MIDI_TYPE : JACK_DEFAULT_AUDIO_TYPE, is_cbox_input ? JackPortIsOutput : JackPortIsInput);
                
                if (names && names[0])
                {
                    if (use_name[0] == '*')
                    {
                        int i;
                        for (i = 0; names[i]; i++)
                            autoconnect_port(client, port, names[i], is_cbox_input, only_connect_port);
                    }
                    else
                        autoconnect_port(client, port, names[0], is_cbox_input, only_connect_port);
                }
                else
                    g_message("Connect: unmatched port regexp %s", use_name);
                jack_free(names);
            }
            else
                autoconnect_port(client, port, use_name, is_cbox_input, only_connect_port);

            if (dpos)
                name = dpos + 1;
        } while(dpos);
    }
}

static void port_connect_cb(jack_port_id_t port, int registered, void *arg)
{
    struct cbox_io *io = arg;
    if (registered)
    {
        jack_port_t *portobj = jack_port_by_id(io->client, port);
        
        jack_ringbuffer_write(io->rb_autoconnect, (uint8_t *)&portobj, sizeof(portobj));
    }
}

int cbox_io_get_sample_rate(struct cbox_io *io)
{
    return jack_get_sample_rate(io->client);
}

static void do_autoconnect(struct cbox_io *io, jack_port_t *portobj)
{
    for (int i = 0; i < io->output_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("cbox:out_%d", 1 + i);
        gchar *config_key = g_strdup_printf("out_%d", 1 + i);
        autoconnect(io->client, cbox_port, config_key, 0, 0, portobj);
        g_free(cbox_port);
        g_free(config_key);
    }
    for (int i = 0; i < io->input_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("cbox:in_%d", 1 + i);
        gchar *config_key = g_strdup_printf("in_%d", 1 + i);
        autoconnect(io->client, cbox_port, config_key, 1, 0, portobj);
        g_free(cbox_port);
        g_free(config_key);
    }
    autoconnect(io->client, "cbox:midi", "midi", 1, 1, portobj);        
}

void cbox_io_poll_ports(struct cbox_io *io)
{
    if (jack_ringbuffer_read_space(io->rb_autoconnect) >= sizeof(jack_port_t *))
    {
        jack_port_t *portobj;
        jack_ringbuffer_read(io->rb_autoconnect, (uint8_t *)&portobj, sizeof(portobj));
        do_autoconnect(io, portobj);
        
    }
}

int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb)
{
    io->cb = cb;
    io->rb_autoconnect = jack_ringbuffer_create(sizeof(jack_port_t *) * 128);
    jack_set_process_callback(io->client, process_cb, io);
    jack_set_port_registration_callback(io->client, port_connect_cb, io);
    jack_activate(io->client);

    if (cbox_config_has_section(io_section))
        do_autoconnect(io, NULL);
    
    return 1;
}

int cbox_io_stop(struct cbox_io *io)
{
    jack_ringbuffer_free(io->rb_autoconnect);
    jack_deactivate(io->client);
    return 1;
}

void cbox_io_close(struct cbox_io *io)
{
    if (io->client)
    {
        for (int i = 0; i < io->output_count; i++)
            jack_port_unregister(io->client, io->outputs[i]);
        if (io->midi)
            jack_port_unregister(io->client, io->midi);
        
        jack_client_close(io->client);
    }
}

