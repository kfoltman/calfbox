/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2012 Krzysztof Foltman

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
#include "errors.h"
#include "hwcfg.h"
#include "io.h"
#include "meter.h"
#include "midi.h"
#include "recsrc.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <jack/ringbuffer.h>
#include <jack/types.h>
#include <jack/midiport.h>

static const char *io_section = "io";

static int process_cb(jack_nframes_t nframes, void *arg)
{
    struct cbox_jack_io_impl *jii = arg;
    struct cbox_io *io = jii->ioi.pio;
    struct cbox_io_callbacks *cb = io->cb;
    
    io->buffer_size = nframes;
    for (int i = 0; i < io->input_count; i++)
        io->input_buffers[i] = jack_port_get_buffer(jii->inputs[i], nframes);
    for (int i = 0; i < io->output_count; i++)
    {
        io->output_buffers[i] = jack_port_get_buffer(jii->outputs[i], nframes);
        for (int j = 0; j < nframes; j ++)
            io->output_buffers[i][j] = 0.f;
    }
    cb->process(cb->user_data, io, nframes);
    for (int i = 0; i < io->input_count; i++)
        io->input_buffers[i] = NULL;
    for (int i = 0; i < io->output_count; i++)
        io->output_buffers[i] = NULL;
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
    struct cbox_jack_io_impl *jii = arg;
    if (registered)
    {
        jack_port_t *portobj = jack_port_by_id(jii->client, port);
        
        jack_ringbuffer_write(jii->rb_autoconnect, (uint8_t *)&portobj, sizeof(portobj));
    }
}

static void port_autoconnect(struct cbox_jack_io_impl *jii, jack_port_t *portobj)
{
    struct cbox_io *io = jii->ioi.pio;

    for (int i = 0; i < io->output_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("cbox:out_%d", 1 + i);
        gchar *config_key = g_strdup_printf("out_%d", 1 + i);
        autoconnect(jii->client, cbox_port, config_key, 0, 0, portobj);
        g_free(cbox_port);
        g_free(config_key);
    }
    for (int i = 0; i < io->input_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("cbox:in_%d", 1 + i);
        gchar *config_key = g_strdup_printf("in_%d", 1 + i);
        autoconnect(jii->client, cbox_port, config_key, 1, 0, portobj);
        g_free(cbox_port);
        g_free(config_key);
    }
    autoconnect(jii->client, "cbox:midi", "midi", 1, 1, portobj);        
}

int cbox_jackio_get_sample_rate(struct cbox_io_impl *impl)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    
    return jack_get_sample_rate(jii->client);
}

gboolean cbox_jackio_get_status(struct cbox_io_impl *impl, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    if (!jii->error_str)
        return TRUE;
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "%s", jii->error_str);    
    return FALSE;
}

static void client_shutdown_cb(jack_status_t code, const char *reason, void *arg)
{
    struct cbox_jack_io_impl *jii = arg;
    struct cbox_io *io = jii->ioi.pio;
    jii->error_str = g_strdup(reason);
    if (io->cb && io->cb->on_disconnected)
        (io->cb->on_disconnected)(io->cb->user_data);
}

gboolean cbox_jackio_start(struct cbox_io_impl *impl, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    
    jack_set_process_callback(jii->client, process_cb, jii);
    jack_set_port_registration_callback(jii->client, port_connect_cb, jii);
    jack_on_info_shutdown(jii->client, client_shutdown_cb, jii);
    jack_activate(jii->client);

    if (cbox_config_has_section(io_section))
        port_autoconnect(jii, NULL);
    
    return TRUE;
}

gboolean cbox_jackio_stop(struct cbox_io_impl *impl, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;

    if (jii->error_str)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "%s", jii->error_str);    
        return FALSE;
    }
    jack_deactivate(jii->client);
    return TRUE;
}

void cbox_jackio_poll_ports(struct cbox_io_impl *impl)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;

    while (jack_ringbuffer_read_space(jii->rb_autoconnect) >= sizeof(jack_port_t *))
    {
        jack_port_t *portobj;
        jack_ringbuffer_read(jii->rb_autoconnect, (uint8_t *)&portobj, sizeof(portobj));
        port_autoconnect(jii, portobj);
    }
}

int cbox_jackio_get_midi_data(struct cbox_io_impl *impl, struct cbox_midi_buffer *destination)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;

    jack_port_t *port = jii->midi;
    void *midi = jack_port_get_buffer(port, jii->ioi.pio->buffer_size);
    uint32_t event_count = jack_midi_get_event_count(midi);

    cbox_midi_buffer_clear(destination);
    for (uint32_t i = 0; i < event_count; i++)
    {
        jack_midi_event_t event;
        
        if (!jack_midi_event_get(&event, midi, i))
        {
            // XXXKF ignore sysex for now
            if (event.size >= 4)
                continue;
            
            uint8_t data[4];
            memcpy(data, event.buffer, event.size);
            if (!cbox_midi_buffer_write_event(destination, event.time, data, event.size))
                return -i;
        }
        else
            return -i;
    }
    
    return event_count;
}

void cbox_jackio_destroy(struct cbox_io_impl *impl)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    struct cbox_io *io = impl->pio;
    if (jii->client)
    {
        if (jii->error_str)
        {
            g_free(jii->error_str);
            jii->error_str = NULL;
        }
        else
        {
            for (int i = 0; i < io->output_count; i++)
                jack_port_unregister(jii->client, jii->outputs[i]);
            if (jii->midi)
                jack_port_unregister(jii->client, jii->midi);
        }
        
        jack_ringbuffer_free(jii->rb_autoconnect);
        jack_client_close(jii->client);
    }
}

///////////////////////////////////////////////////////////////////////////////

int cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params)
{
    jack_client_t *client = NULL;
    jack_status_t status = 0;
    client = jack_client_open("cbox", JackNoStartServer, &status);
    if (client == NULL)
    {
        if (!cbox_hwcfg_setup_jack())
            return 0;
        
        status = 0;
        client = jack_client_open("cbox", 0, &status);
    }
    if (client == NULL)
        return 0;
    
    // XXXKF would use a callback instead
    io->buffer_size = jack_get_buffer_size(client);
    io->cb = NULL;
    io->input_count = cbox_config_get_int("io", "inputs", 0);
    io->input_buffers = malloc(sizeof(float *) * io->input_count);
    io->output_count = cbox_config_get_int("io", "outputs", 2);
    io->output_buffers = malloc(sizeof(float *) * io->output_count);

    struct cbox_jack_io_impl *jii = malloc(sizeof(struct cbox_jack_io_impl));
    io->impl = &jii->ioi;

    jii->ioi.pio = io;
    jii->ioi.getsampleratefunc = cbox_jackio_get_sample_rate;
    jii->ioi.startfunc = cbox_jackio_start;
    jii->ioi.stopfunc = cbox_jackio_stop;
    jii->ioi.getstatusfunc = cbox_jackio_get_status;
    jii->ioi.pollfunc = cbox_jackio_poll_ports;
    jii->ioi.getmidifunc = cbox_jackio_get_midi_data;
    jii->ioi.destroyfunc = cbox_jackio_destroy;
    
    jii->client = client;
    jii->rb_autoconnect = jack_ringbuffer_create(sizeof(jack_port_t *) * 128);
    jii->error_str = NULL;
    
    jii->inputs = malloc(sizeof(jack_port_t *) * io->input_count);
    for (int i = 0; i < io->input_count; i++)
    {
        gchar *name = g_strdup_printf("in_%d", 1 + i);
        jii->inputs[i] = jack_port_register(jii->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (!jii->inputs[i])
        {
            // XXXKF switch to glib style error reporting, though this condition will probably rarely happen in real life
            g_error("Cannot create input port %d", i);
            return 0;
        }
        g_free(name);
    }
    jii->outputs = malloc(sizeof(jack_port_t *) * io->output_count);
    for (int i = 0; i < io->output_count; i++)
    {
        gchar *name = g_strdup_printf("out_%d", 1 + i);
        jii->outputs[i] = jack_port_register(jii->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!jii->outputs[i])
        {
            // XXXKF switch to glib style error reporting, though this condition will probably rarely happen in real life
            g_error("Cannot create output port %d", i);
            return 0;
        }
        g_free(name);
    }
    jii->midi = jack_port_register(jii->client, "midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    
    if (!jii->midi)
        return 0;

    cbox_io_poll_ports(io);

    return 1;
};

