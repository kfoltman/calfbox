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
#include "mididest.h"
#include "recsrc.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <jack/ringbuffer.h>
#include <jack/types.h>
#include <jack/midiport.h>

struct cbox_jack_io_impl
{
    struct cbox_io_impl ioi;

    jack_client_t *client;
    jack_port_t **inputs;
    jack_port_t **outputs;
    jack_port_t *midi;
    char *error_str; // set to non-NULL if client has been booted out by JACK
    char *client_name;
    GSList *extra_midi_ports;

    jack_ringbuffer_t *rb_autoconnect;    
};

///////////////////////////////////////////////////////////////////////////////

struct cbox_jack_midi_output
{
    gchar *name;
    jack_port_t *port;
    struct cbox_midi_buffer buffer;
    struct cbox_midi_merger merger;
};

static struct cbox_midi_merger *cbox_jackio_get_midi_out(struct cbox_io_impl *impl, const char *name)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    for (GSList *p = jii->extra_midi_ports; p; p = g_slist_next(p))
    {
        struct cbox_jack_midi_output *midiout = p->data;
        if (!strcmp(midiout->name, name))
            return &midiout->merger;
    }
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////

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
    for (GSList *p = jii->extra_midi_ports; p; p = g_slist_next(p))
    {
        struct cbox_jack_midi_output *midiout = p->data;

        void *pbuf = jack_port_get_buffer(midiout->port, nframes);
        jack_midi_clear_buffer(pbuf);

        cbox_midi_merger_render(&midiout->merger);
        if (midiout->buffer.count)
        {
            uint8_t tmp_data[4];
            for (int i = 0; i < midiout->buffer.count; i++)
            {
                struct cbox_midi_event *event = cbox_midi_buffer_get_event(&midiout->buffer, i);
                uint8_t *pdata = cbox_midi_event_get_data(event);
                if ((pdata[0] & 0xF0) == 0x90 && !pdata[2] && event->size == 3)
                {
                    tmp_data[0] = pdata[0] & ~0x10;
                    tmp_data[1] = pdata[1];
                    tmp_data[2] = pdata[2];
                    pdata = tmp_data;
                }
                if (jack_midi_event_write(pbuf, event->time, pdata, event->size))
                {
                    g_warning("MIDI buffer overflow on JACK output port '%s'", midiout->name);
                    break;
                }
            }
        }
    }
    return 0;
}

static void autoconnect_port(jack_client_t *client, const char *port, const char *use_name, int is_cbox_input, const jack_port_t *only_connect_port, struct cbox_command_target *fb)
{
    int res;
    if (only_connect_port)
    {
        jack_port_t *right;
        right = jack_port_by_name(client, use_name);
        if (only_connect_port != right)
            return;
    }
    
    const char *pfrom = is_cbox_input ? use_name : port;
    const char *pto = !is_cbox_input ? use_name : port;
    
    res = jack_connect(client, pfrom, pto);
    gboolean suppressed = FALSE;
    if (fb)
    {
        if (!res)
            suppressed = cbox_execute_on(fb, NULL, "/io/jack/connected", "ss", NULL, pfrom, pto);
        else
            suppressed = cbox_execute_on(fb, NULL, "/io/jack/connect_failed", "sss", NULL, pfrom, pto, (res == EEXIST ? "already connected" : "failed"));
    }
    if (!suppressed)
        g_message("Connect: %s %s %s (%s)", port, is_cbox_input ? "<-" : "->", use_name, res == 0 ? "success" : (res == EEXIST ? "already connected" : "failed"));
}

static void autoconnect(jack_client_t *client, const char *port, const char *config_var, int is_cbox_input, int is_midi, const jack_port_t *only_connect_port, struct cbox_command_target *fb)
{
    char *name, *orig_name, *dpos;
    const char *use_name;
    
    orig_name = cbox_config_get_string(cbox_io_section, config_var);
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
                        autoconnect_port(client, port, names[i], is_cbox_input, only_connect_port, fb);
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
                            autoconnect_port(client, port, names[i], is_cbox_input, only_connect_port, fb);
                    }
                    else
                        autoconnect_port(client, port, names[0], is_cbox_input, only_connect_port, fb);
                }
                else
                    g_message("Connect: unmatched port regexp %s", use_name);
                jack_free(names);
            }
            else
                autoconnect_port(client, port, use_name, is_cbox_input, only_connect_port, fb);

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
        
        jack_ringbuffer_write(jii->rb_autoconnect, (char *)&portobj, sizeof(portobj));
    }
}

static void port_autoconnect(struct cbox_jack_io_impl *jii, jack_port_t *portobj, struct cbox_command_target *fb)
{
    struct cbox_io *io = jii->ioi.pio;

    for (int i = 0; i < io->output_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("%s:out_%d", jii->client_name, 1 + i);
        gchar *config_key = g_strdup_printf("out_%d", 1 + i);
        autoconnect(jii->client, cbox_port, config_key, 0, 0, portobj, fb);
        g_free(cbox_port);
        g_free(config_key);
    }
    for (int i = 0; i < io->input_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("%s:in_%d", jii->client_name, 1 + i);
        gchar *config_key = g_strdup_printf("in_%d", 1 + i);
        autoconnect(jii->client, cbox_port, config_key, 1, 0, portobj, fb);
        g_free(cbox_port);
        g_free(config_key);
    }
    gchar *cbox_port = g_strdup_printf("%s:midi", jii->client_name);
    autoconnect(jii->client, cbox_port, "midi", 1, 1, portobj, fb);
    g_free(cbox_port);
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

gboolean cbox_jackio_start(struct cbox_io_impl *impl, struct cbox_command_target *fb, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    
    jack_set_process_callback(jii->client, process_cb, jii);
    jack_set_port_registration_callback(jii->client, port_connect_cb, jii);
    jack_on_info_shutdown(jii->client, client_shutdown_cb, jii);
    jack_activate(jii->client);

    if (cbox_config_has_section(cbox_io_section))
        port_autoconnect(jii, NULL, fb);
    
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

void cbox_jackio_poll_ports(struct cbox_io_impl *impl, struct cbox_command_target *fb)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;

    while (jack_ringbuffer_read_space(jii->rb_autoconnect) >= sizeof(jack_port_t *))
    {
        jack_port_t *portobj;
        jack_ringbuffer_read(jii->rb_autoconnect, (char *)&portobj, sizeof(portobj));
        port_autoconnect(jii, portobj, fb);
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
        if (jii->client_name)
        {
            free(jii->client_name);
            jii->client_name = NULL;
        }
        while(jii->extra_midi_ports)
        {
            struct cbox_jack_midi_output *jmo = jii->extra_midi_ports->data;
            jack_port_unregister(jii->client, jmo->port);
            free(jmo);
            jii->extra_midi_ports = g_slist_remove(jii->extra_midi_ports, jii->extra_midi_ports->data);
        }
        
        jack_ringbuffer_free(jii->rb_autoconnect);
        jack_client_close(jii->client);
    }
    free(jii);
}

gboolean cbox_jackio_cycle(struct cbox_io_impl *impl, struct cbox_command_target *fb, GError **error)
{
    struct cbox_io *io = impl->pio;
    struct cbox_io_callbacks *cb = io->cb;
    cbox_io_close(io);
    
    // XXXKF use params structure some day
    if (!cbox_io_init_jack(io, NULL, fb, error))
        return FALSE;
    
    cbox_io_start(io, cb, fb);
    if (cb->on_reconnected)
        (cb->on_reconnected)(cb->user_data);
    return TRUE;
}



///////////////////////////////////////////////////////////////////////////////

static gboolean cbox_jack_io_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/client_name", "s", error, jii->client_name);
    }
    else if (!strcmp(cmd->command, "/create_midi_output") && !strcmp(cmd->arg_types, "s"))
    {
        const char *name = CBOX_ARG_S(cmd, 0);
        jack_port_t *port = jack_port_register(jii->client, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
        if (!port)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create output MIDI port '%s'", name);
            return FALSE;
        }
        struct cbox_jack_midi_output *output = calloc(1, sizeof(struct cbox_jack_midi_output));
        output->name = g_strdup(name);
        output->port = port;
        cbox_midi_buffer_init(&output->buffer);
        cbox_midi_merger_init(&output->merger, &output->buffer);
        jii->extra_midi_ports = g_slist_append(jii->extra_midi_ports, output);
        return TRUE;
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

///////////////////////////////////////////////////////////////////////////////

gboolean cbox_io_init_jack(struct cbox_io *io, struct cbox_open_params *const params, struct cbox_command_target *fb, GError **error)
{
    const char *client_name = cbox_config_get_string_with_default("io", "client_name", "cbox");
    
    jack_client_t *client = NULL;
    jack_status_t status = 0;
    client = jack_client_open(client_name, JackNoStartServer, &status);
    if (client == NULL)
    {
        if (!cbox_hwcfg_setup_jack())
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set up JACK server configuration based on current hardware");
            return FALSE;
        }
        
        status = 0;
        client = jack_client_open(client_name, 0, &status);
    }
    if (client == NULL)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create JACK instance");
        return FALSE;
    }
    
    // XXXKF would use a callback instead
    io->buffer_size = jack_get_buffer_size(client);
    io->cb = NULL;
    io->input_count = cbox_config_get_int("io", "inputs", 0);
    io->input_buffers = malloc(sizeof(float *) * io->input_count);
    io->output_count = cbox_config_get_int("io", "outputs", 2);
    io->output_buffers = malloc(sizeof(float *) * io->output_count);

    struct cbox_jack_io_impl *jii = malloc(sizeof(struct cbox_jack_io_impl));
    io->impl = &jii->ioi;

    cbox_command_target_init(&io->cmd_target, cbox_jack_io_process_cmd, jii);
    jii->ioi.pio = io;
    jii->ioi.getsampleratefunc = cbox_jackio_get_sample_rate;
    jii->ioi.startfunc = cbox_jackio_start;
    jii->ioi.stopfunc = cbox_jackio_stop;
    jii->ioi.getstatusfunc = cbox_jackio_get_status;
    jii->ioi.pollfunc = cbox_jackio_poll_ports;
    jii->ioi.cyclefunc = cbox_jackio_cycle;
    jii->ioi.getmidifunc = cbox_jackio_get_midi_data;
    jii->ioi.getmidioutfunc = cbox_jackio_get_midi_out;
    jii->ioi.destroyfunc = cbox_jackio_destroy;
    
    jii->client_name = g_strdup(jack_get_client_name(client));
    jii->client = client;
    jii->rb_autoconnect = jack_ringbuffer_create(sizeof(jack_port_t *) * 128);
    jii->error_str = NULL;
    jii->extra_midi_ports = NULL;
    
    jii->inputs = malloc(sizeof(jack_port_t *) * io->input_count);
    jii->outputs = malloc(sizeof(jack_port_t *) * io->output_count);
    for (int i = 0; i < io->input_count; i++)
        jii->inputs[i] = NULL;
    for (int i = 0; i < io->output_count; i++)
        jii->outputs[i] = NULL;
    for (int i = 0; i < io->input_count; i++)
    {
        gchar *name = g_strdup_printf("in_%d", 1 + i);
        jii->inputs[i] = jack_port_register(jii->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (!jii->inputs[i])
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create input port %d (%s)", i, name);
            g_free(name);
            goto cleanup;
        }
        g_free(name);
    }
    for (int i = 0; i < io->output_count; i++)
    {
        gchar *name = g_strdup_printf("out_%d", 1 + i);
        jii->outputs[i] = jack_port_register(jii->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!jii->outputs[i])
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create output port %d (%s)", i, name);
            g_free(name);
            goto cleanup;
        }
        g_free(name);
    }
    jii->midi = jack_port_register(jii->client, "midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    
    if (!jii->midi)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create MIDI port");
        return FALSE;
    }
    if (fb)
        cbox_execute_on(fb, NULL, "/io/jack_client_name", "s", NULL, jii->client_name);
    
    cbox_io_poll_ports(io, fb);

    return TRUE;

cleanup:
    if (jii->inputs)
    {
        for (int i = 0; i < io->input_count; i++)
            free(jii->inputs[i]);
        free(jii->inputs);
    }
    if (jii->outputs)
    {
        for (int i = 0; i < io->output_count; i++)
            free(jii->outputs[i]);
        free(jii->outputs);
    }
    while(jii->extra_midi_ports)
    {
        free(jii->extra_midi_ports->data);
        jii->extra_midi_ports = g_slist_remove(jii->extra_midi_ports, jii->extra_midi_ports->data);
    }
    if (jii->client_name)
        free(jii->client_name);
    jack_client_close(jii->client);
    free(jii);
    io->impl = NULL;
    return FALSE;
};

