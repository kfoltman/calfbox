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

    jack_ringbuffer_t *rb_autoconnect;    
};

///////////////////////////////////////////////////////////////////////////////

struct cbox_jack_midi_input
{
    struct cbox_midi_input hdr;
    gchar *autoconnect_spec;
    jack_port_t *port;
    struct cbox_jack_io_impl *jii;
};

struct cbox_jack_midi_output
{
    struct cbox_midi_output hdr;
    gchar *autoconnect_spec;
    jack_port_t *port;
    struct cbox_jack_io_impl *jii;
};

static struct cbox_midi_input *cbox_jackio_create_midi_in(struct cbox_io_impl *impl, const char *name, GError **error);
static struct cbox_midi_output *cbox_jackio_create_midi_out(struct cbox_io_impl *impl, const char *name, GError **error);
static void cbox_jackio_destroy_midi_in(struct cbox_io_impl *ioi, struct cbox_midi_input *midiin);
static void cbox_jackio_destroy_midi_out(struct cbox_io_impl *ioi, struct cbox_midi_output *midiout);
static void cbox_jack_midi_output_set_autoconnect(struct cbox_jack_midi_output *jmo, const gchar *autoconnect_spec);

void cbox_jack_midi_input_destroy(struct cbox_jack_midi_input *jmi)
{
    if (jmi->port)
    {
        jack_port_unregister(jmi->jii->client, jmi->port);
        jmi->port = NULL;
    }
    g_free(jmi->hdr.name);
    g_free(jmi->autoconnect_spec);
    free(jmi);
}

void cbox_jack_midi_output_destroy(struct cbox_jack_midi_output *jmo)
{
    if (jmo->port)
    {
        jack_port_unregister(jmo->jii->client, jmo->port);
        jmo->port = NULL;
    }
    g_free(jmo->hdr.name);
    g_free(jmo->autoconnect_spec);
    free(jmo);
}

///////////////////////////////////////////////////////////////////////////////

static int copy_midi_data_to_buffer(jack_port_t *port, int buffer_size, struct cbox_midi_buffer *destination)
{
    void *midi = jack_port_get_buffer(port, buffer_size);
    uint32_t event_count = jack_midi_get_event_count(midi);

    cbox_midi_buffer_clear(destination);
    for (uint32_t i = 0; i < event_count; i++)
    {
        jack_midi_event_t event;
        
        if (!jack_midi_event_get(&event, midi, i))
        {
            if (!cbox_midi_buffer_write_event(destination, event.time, event.buffer, event.size))
                return -i;
        }
        else
            return -i;
    }
    
    return event_count;
}

///////////////////////////////////////////////////////////////////////////////

static int process_cb(jack_nframes_t nframes, void *arg)
{
    struct cbox_jack_io_impl *jii = arg;
    struct cbox_io *io = jii->ioi.pio;
    struct cbox_io_callbacks *cb = io->cb;
    
    io->io_env.buffer_size = nframes;
    for (int i = 0; i < io->io_env.input_count; i++)
        io->input_buffers[i] = jack_port_get_buffer(jii->inputs[i], nframes);
    for (int i = 0; i < io->io_env.output_count; i++)
    {
        io->output_buffers[i] = jack_port_get_buffer(jii->outputs[i], nframes);
        for (int j = 0; j < nframes; j ++)
            io->output_buffers[i][j] = 0.f;
    }
    for (GSList *p = io->midi_inputs; p; p = p->next)
    {
        struct cbox_jack_midi_input *input = p->data;
        if (input->hdr.output_set || input->hdr.enable_appsink)
        {
            copy_midi_data_to_buffer(input->port, io->io_env.buffer_size, &input->hdr.buffer);
            if (input->hdr.enable_appsink)
                cbox_midi_appsink_supply(&input->hdr.appsink, &input->hdr.buffer);
        }
        else
            cbox_midi_buffer_clear(&input->hdr.buffer);
    }
    cb->process(cb->user_data, io, nframes);
    for (int i = 0; i < io->io_env.input_count; i++)
        io->input_buffers[i] = NULL;
    for (int i = 0; i < io->io_env.output_count; i++)
        io->output_buffers[i] = NULL;
    for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
    {
        struct cbox_jack_midi_output *midiout = p->data;

        void *pbuf = jack_port_get_buffer(midiout->port, nframes);
        jack_midi_clear_buffer(pbuf);

        cbox_midi_merger_render(&midiout->hdr.merger);
        if (midiout->hdr.buffer.count)
        {
            uint8_t tmp_data[4];
            for (int i = 0; i < midiout->hdr.buffer.count; i++)
            {
                const struct cbox_midi_event *event = cbox_midi_buffer_get_event(&midiout->hdr.buffer, i);
                const uint8_t *pdata = cbox_midi_event_get_data(event);
                if ((pdata[0] & 0xF0) == 0x90 && !pdata[2] && event->size == 3)
                {
                    tmp_data[0] = pdata[0] & ~0x10;
                    tmp_data[1] = pdata[1];
                    tmp_data[2] = pdata[2];
                    pdata = tmp_data;
                }
                if (jack_midi_event_write(pbuf, event->time, pdata, event->size))
                {
                    g_warning("MIDI buffer overflow on JACK output port '%s'", midiout->hdr.name);
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
    if (res == EEXIST)
        res = 0;
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

static void autoconnect_by_spec(jack_client_t *client, const char *port, const char *orig_spec, int is_cbox_input, int is_midi, const jack_port_t *only_connect_port, struct cbox_command_target *fb)
{
    char *name, *copy_spec, *dpos;
    const char *use_name;
    
    copy_spec = g_strdup(orig_spec);
    name = copy_spec;
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
    g_free(copy_spec);
}

static void autoconnect_by_var(jack_client_t *client, const char *port, const char *config_var, int is_cbox_input, int is_midi, const jack_port_t *only_connect_port, struct cbox_command_target *fb)
{
    const char *orig_spec = cbox_config_get_string(cbox_io_section, config_var);
    if (orig_spec)
        autoconnect_by_spec(client, port, orig_spec, is_cbox_input, is_midi, only_connect_port, fb);
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

    for (int i = 0; i < io->io_env.output_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("%s:out_%d", jii->client_name, 1 + i);
        gchar *config_key = g_strdup_printf("out_%d", 1 + i);
        autoconnect_by_var(jii->client, cbox_port, config_key, 0, 0, portobj, fb);
        g_free(cbox_port);
        g_free(config_key);
    }
    for (int i = 0; i < io->io_env.input_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("%s:in_%d", jii->client_name, 1 + i);
        gchar *config_key = g_strdup_printf("in_%d", 1 + i);
        autoconnect_by_var(jii->client, cbox_port, config_key, 1, 0, portobj, fb);
        g_free(cbox_port);
        g_free(config_key);
    }
    for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
    {
        struct cbox_jack_midi_output *midiout = p->data;
        if (midiout->autoconnect_spec)
        {
            gchar *cbox_port = g_strdup_printf("%s:%s", jii->client_name, midiout->hdr.name);
            autoconnect_by_spec(jii->client, cbox_port, midiout->autoconnect_spec, 0, 1, portobj, fb);
            g_free(cbox_port);
        }
    }
    for (GSList *p = io->midi_inputs; p; p = g_slist_next(p))
    {
        struct cbox_jack_midi_input *midiin = p->data;
        if (midiin->autoconnect_spec)
        {
            gchar *cbox_port = g_strdup_printf("%s:%s", jii->client_name, midiin->hdr.name);
            autoconnect_by_spec(jii->client, cbox_port, midiin->autoconnect_spec, 1, 1, portobj, fb);
            g_free(cbox_port);
        }
    }
    gchar *cbox_port = g_strdup_printf("%s:midi", jii->client_name);
    autoconnect_by_var(jii->client, cbox_port, "midi", 1, 1, portobj, fb);
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

    struct cbox_io *io = jii->ioi.pio;
    if (io->cb->on_started)
        io->cb->on_started(io->cb->user_data);

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

    return copy_midi_data_to_buffer(jii->midi, jii->ioi.pio->io_env.buffer_size, destination);
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
            for (int i = 0; i < io->io_env.input_count; i++)
                jack_port_unregister(jii->client, jii->inputs[i]);
            free(jii->inputs);
            for (int i = 0; i < io->io_env.output_count; i++)
                jack_port_unregister(jii->client, jii->outputs[i]);
            free(jii->outputs);
            if (jii->midi)
                jack_port_unregister(jii->client, jii->midi);
        }
        if (jii->client_name)
        {
            free(jii->client_name);
            jii->client_name = NULL;
        }
        cbox_io_destroy_all_midi_ports(io);
        
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

struct cbox_midi_input *cbox_jackio_create_midi_in(struct cbox_io_impl *impl, const char *name, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    jack_port_t *port = jack_port_register(jii->client, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    if (!port)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create input MIDI port '%s'", name);
        return FALSE;
    }
    struct cbox_jack_midi_input *input = calloc(1, sizeof(struct cbox_jack_midi_input));
    input->hdr.name = g_strdup(name);
    input->hdr.removing = FALSE;
    input->port = port;
    input->jii = jii;
    cbox_uuid_generate(&input->hdr.uuid);
    cbox_midi_buffer_init(&input->hdr.buffer);

    return (struct cbox_midi_input *)input;
}

struct cbox_midi_output *cbox_jackio_create_midi_out(struct cbox_io_impl *impl, const char *name, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    jack_port_t *port = jack_port_register(jii->client, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
    if (!port)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create output MIDI port '%s'", name);
        return FALSE;
    }
    struct cbox_jack_midi_output *output = calloc(1, sizeof(struct cbox_jack_midi_output));
    output->hdr.name = g_strdup(name);
    output->hdr.removing = FALSE;
    output->port = port;
    output->jii = jii;
    cbox_uuid_generate(&output->hdr.uuid);
    cbox_midi_buffer_init(&output->hdr.buffer);
    cbox_midi_merger_init(&output->hdr.merger, &output->hdr.buffer);

    return (struct cbox_midi_output *)output;
}

void cbox_jack_midi_input_set_autoconnect(struct cbox_jack_midi_input *jmi, const gchar *autoconnect_spec)
{
    if (jmi->autoconnect_spec)
        g_free(jmi->autoconnect_spec);
    jmi->autoconnect_spec = autoconnect_spec && *autoconnect_spec ? g_strdup(autoconnect_spec) : NULL;
    if (jmi->autoconnect_spec)
    {
        gchar *cbox_port = g_strdup_printf("%s:%s", jmi->jii->client_name, jmi->hdr.name);
        autoconnect_by_spec(jmi->jii->client, cbox_port, jmi->autoconnect_spec, 1, 1, NULL, NULL);
        g_free(cbox_port);
    }
}

void cbox_jack_midi_output_set_autoconnect(struct cbox_jack_midi_output *jmo, const gchar *autoconnect_spec)
{
    if (jmo->autoconnect_spec)
        g_free(jmo->autoconnect_spec);
    jmo->autoconnect_spec = autoconnect_spec && *autoconnect_spec ? g_strdup(autoconnect_spec) : NULL;
    if (jmo->autoconnect_spec)
    {
        gchar *cbox_port = g_strdup_printf("%s:%s", jmo->jii->client_name, jmo->hdr.name);
        autoconnect_by_spec(jmo->jii->client, cbox_port, jmo->autoconnect_spec, 0, 1, NULL, NULL);
        g_free(cbox_port);
    }
}

void cbox_jackio_destroy_midi_in(struct cbox_io_impl *ioi, struct cbox_midi_input *midiin)
{
    cbox_jack_midi_input_destroy((struct cbox_jack_midi_input *)midiin);
}

void cbox_jackio_destroy_midi_out(struct cbox_io_impl *ioi, struct cbox_midi_output *midiout)
{
    cbox_jack_midi_output_destroy((struct cbox_jack_midi_output *)midiout);
}

static gboolean cbox_jack_io_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)ct->user_data;
    struct cbox_io *io = jii->ioi.pio;
    gboolean handled = FALSE;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/client_type", "s", error, "JACK") &&
            cbox_execute_on(fb, NULL, "/client_name", "s", error, jii->client_name) &&
            cbox_io_process_cmd(io, fb, cmd, error, &handled);
    }
    else if (!strcmp(cmd->command, "/rename_midi_port") && !strcmp(cmd->arg_types, "ss"))
    {
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        const char *new_name = CBOX_ARG_S(cmd, 1);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
        struct cbox_midi_output *midiout = cbox_io_get_midi_output(io, NULL, &uuid);
        if (!midiout && !midiin)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        jack_port_t *port = midiout ? ((struct cbox_jack_midi_output *)midiout)->port 
            : ((struct cbox_jack_midi_input *)midiin)->port;
        char **pname = midiout ? &midiout->name : &midiin->name;
        if (0 != jack_port_set_name(port, new_name))
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set port name to '%s'", new_name);
            return FALSE;
        }
        g_free(*pname);
        *pname = g_strdup(new_name);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/autoconnect") && !strcmp(cmd->arg_types, "ss"))
    {
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        const char *spec = CBOX_ARG_S(cmd, 1);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_output *midiout = cbox_io_get_midi_output(io, NULL, &uuid);
        if (midiout)
        {
            cbox_jack_midi_output_set_autoconnect((struct cbox_jack_midi_output *)midiout, spec);
            return TRUE;
        }
        struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
        if (midiin)
        {
            cbox_jack_midi_input_set_autoconnect((struct cbox_jack_midi_input *)midiin, spec);
            return TRUE;
        }
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
        return FALSE;
    }
    else if (!strcmp(cmd->command, "/disconnect_midi_port") && !strcmp(cmd->arg_types, "s"))
    {
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
        struct cbox_midi_output *midiout = cbox_io_get_midi_output(io, NULL, &uuid);
        if (!midiout && !midiin)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        jack_port_t *port = midiout ? ((struct cbox_jack_midi_output *)midiout)->port 
            : ((struct cbox_jack_midi_input *)midiin)->port; 
        jack_port_disconnect(jii->client, port);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/port_connect") && !strcmp(cmd->arg_types, "ss"))
    {
        const char *port_from = CBOX_ARG_S(cmd, 0);
        const char *port_to = CBOX_ARG_S(cmd, 1);
        int res = jack_connect(jii->client, port_from, port_to);
        if (res == EEXIST)
            res = 0;
        if (res)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot connect port '%s' to '%s'", port_from, port_to);
        return res == 0;
    }
    else if (!strcmp(cmd->command, "/port_disconnect") && !strcmp(cmd->arg_types, "ss"))
    {
        const char *port_from = CBOX_ARG_S(cmd, 0);
        const char *port_to = CBOX_ARG_S(cmd, 1);
        int res = jack_disconnect(jii->client, port_from, port_to);
        if (res)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot disconnect port '%s' from '%s'", port_from, port_to);
        return res == 0;
    }
    else if (!strcmp(cmd->command, "/get_ports") && !strcmp(cmd->arg_types, "ssi"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        const char *mask = CBOX_ARG_S(cmd, 0);
        const char *type = CBOX_ARG_S(cmd, 1);
        uint32_t flags = CBOX_ARG_I(cmd, 2);
        const char** ports = jack_get_ports(jii->client, mask, type, flags);
        for (int i = 0; ports && ports[i]; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/port", "s", error, ports[i]))
                return FALSE;
            
        }
        return TRUE;
    }
    else
    {
        gboolean result = cbox_io_process_cmd(io, fb, cmd, error, &handled);
        if (!handled)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return result;
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
    io->io_env.buffer_size = jack_get_buffer_size(client);
    io->cb = NULL;
    io->io_env.input_count = cbox_config_get_int("io", "inputs", 0);
    io->input_buffers = malloc(sizeof(float *) * io->io_env.input_count);
    io->io_env.output_count = cbox_config_get_int("io", "outputs", 2);
    io->output_buffers = malloc(sizeof(float *) * io->io_env.output_count);

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
    jii->ioi.createmidiinfunc = cbox_jackio_create_midi_in;
    jii->ioi.destroymidiinfunc = cbox_jackio_destroy_midi_in;
    jii->ioi.createmidioutfunc = cbox_jackio_create_midi_out;
    jii->ioi.destroymidioutfunc = cbox_jackio_destroy_midi_out;
    jii->ioi.updatemidiinroutingfunc = NULL;
    jii->ioi.destroyfunc = cbox_jackio_destroy;
    
    jii->client_name = g_strdup(jack_get_client_name(client));
    jii->client = client;
    jii->rb_autoconnect = jack_ringbuffer_create(sizeof(jack_port_t *) * 128);
    jii->error_str = NULL;
    io->io_env.srate = jack_get_sample_rate(client);
    
    jii->inputs = malloc(sizeof(jack_port_t *) * io->io_env.input_count);
    jii->outputs = malloc(sizeof(jack_port_t *) * io->io_env.output_count);
    for (int i = 0; i < io->io_env.input_count; i++)
        jii->inputs[i] = NULL;
    for (int i = 0; i < io->io_env.output_count; i++)
        jii->outputs[i] = NULL;
    for (int i = 0; i < io->io_env.input_count; i++)
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
    for (int i = 0; i < io->io_env.output_count; i++)
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
        for (int i = 0; i < io->io_env.input_count; i++)
            free(jii->inputs[i]);
        free(jii->inputs);
    }
    if (jii->outputs)
    {
        for (int i = 0; i < io->io_env.output_count; i++)
            free(jii->outputs[i]);
        free(jii->outputs);
    }
    cbox_io_destroy_all_midi_ports(io);
    if (jii->client_name)
        free(jii->client_name);
    jack_client_close(jii->client);
    free(jii);
    io->impl = NULL;
    return FALSE;
};

