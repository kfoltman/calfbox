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

#if USE_JACK

#include "app.h"
#include "config-api.h"
#include "errors.h"
#include "hwcfg.h"
#include "io.h"
#include "meter.h"
#include "midi.h"
#include "mididest.h"
#include "recsrc.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <jack/ringbuffer.h>
#include <jack/types.h>
#include <jack/midiport.h>
#include <jack/transport.h>
#include <jack/uuid.h>
#include <jack/metadata.h>

struct cbox_jack_io_impl
{
    struct cbox_io_impl ioi;

    jack_client_t *client;
    jack_port_t **inputs;
    jack_port_t **outputs;
    jack_port_t *midi;
    char *error_str; // set to non-NULL if client has been booted out by JACK
    char *client_name;
    gboolean enable_common_midi_input;
    jack_transport_state_t last_transport_state;
    gboolean debug_transport;
    gboolean external_tempo;

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

struct cbox_jack_audio_output
{
    struct cbox_audio_output hdr;
    gchar *autoconnect_spec;
    jack_port_t *port;
    struct cbox_jack_io_impl *jii;
};

static struct cbox_midi_input *cbox_jackio_create_midi_in(struct cbox_io_impl *impl, const char *name, GError **error);
static struct cbox_midi_output *cbox_jackio_create_midi_out(struct cbox_io_impl *impl, const char *name, GError **error);
static struct cbox_audio_output *cbox_jackio_create_audio_out(struct cbox_io_impl *impl, const char *name, GError **error);
static void cbox_jackio_destroy_midi_in(struct cbox_io_impl *ioi, struct cbox_midi_input *midiin);
static void cbox_jackio_destroy_midi_out(struct cbox_io_impl *ioi, struct cbox_midi_output *midiout);
static void cbox_jackio_destroy_audio_out(struct cbox_io_impl *ioi, struct cbox_audio_output *audioout);
static void cbox_jack_midi_output_set_autoconnect(struct cbox_jack_midi_output *jmo, const gchar *autoconnect_spec);
static void cbox_jack_audio_output_set_autoconnect(struct cbox_jack_audio_output *jao, const gchar *autoconnect_spec);

static const char *transport_state_names[] = {"Stopped", "Rolling", "Looping?", "Starting", "Unknown/invalid#4", "Unknown/invalid#5", "Unknown/invalid#6" };

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
    assert(!jmo->hdr.merger.inputs);
    free(jmo);
}

void cbox_jack_audio_output_destroy(struct cbox_jack_audio_output *jao)
{
    if (jao->port)
    {
        jack_port_unregister(jao->jii->client, jao->port);
        jao->port = NULL;
    }
    g_free(jao->hdr.name);
    g_free(jao->autoconnect_spec);
    free(jao);
}

///////////////////////////////////////////////////////////////////////////////

static int copy_midi_data_to_buffer(jack_port_t *port, int buffer_size, struct cbox_midi_buffer *destination)
{
    void *midi = jack_port_get_buffer(port, buffer_size);
    if (!midi)
        return 0;
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
    for (uint32_t i = 0; i < io->io_env.input_count; i++)
        io->input_buffers[i] = jack_port_get_buffer(jii->inputs[i], nframes);
    for (uint32_t i = 0; i < io->io_env.output_count; i++)
    {
        io->output_buffers[i] = jack_port_get_buffer(jii->outputs[i], nframes);
        if (!io->output_buffers[i])
            continue;
        for (uint32_t j = 0; j < nframes; j ++)
            io->output_buffers[i][j] = 0.f;
    }
    if (cb->on_transport_sync || (jii->external_tempo && cb->on_tempo_sync)) {
        jack_position_t pos;
        memset(&pos, 0, sizeof(pos));
        jack_transport_state_t state = jack_transport_query(jii->client, &pos);
        if (jii->external_tempo && cb->on_tempo_sync && (pos.valid & JackPositionBBT) && pos.beats_per_minute > 0) {
            cb->on_tempo_sync(cb->user_data, pos.beats_per_minute);
        }
        if (cb->on_transport_sync)
        {
            if (state != jii->last_transport_state)
            {
                jack_position_t pos;
                jack_transport_query(jii->client, &pos);
                if (jii->debug_transport)
                    g_message("JACK transport: incoming state change, state = %s, last state = %s, pos = %d\n", transport_state_names[state], transport_state_names[(int)jii->last_transport_state], (int)pos.frame);
                if (state == JackTransportStopped)
                {
                    if (cb->on_transport_sync(cb->user_data, ts_stopping, pos.frame))
                        jii->last_transport_state = state;
                }
                else
                if (state == JackTransportRolling && jii->last_transport_state == JackTransportStarting)
                {
                    if (cb->on_transport_sync(cb->user_data, ts_rolling, pos.frame))
                        jii->last_transport_state = state;
                }
                else
                    jii->last_transport_state = state;
            }
        }
    }
    for (GSList *p = io->midi_inputs; p; p = p->next)
    {
        struct cbox_jack_midi_input *input = p->data;
        if (input->hdr.output_set || input->hdr.enable_appsink)
        {
            copy_midi_data_to_buffer(input->port, io->io_env.buffer_size, &input->hdr.buffer);
            if (input->hdr.enable_appsink)
                cbox_midi_appsink_supply(&input->hdr.appsink, &input->hdr.buffer, io->free_running_frame_counter);
        }
        else
            cbox_midi_buffer_clear(&input->hdr.buffer);
    }
    cb->process(cb->user_data, io, nframes);
    for (uint32_t i = 0; i < io->io_env.input_count; i++)
        io->input_buffers[i] = NULL;
    for (uint32_t i = 0; i < io->io_env.output_count; i++)
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
            for (uint32_t i = 0; i < midiout->hdr.buffer.count; i++)
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
    io->free_running_frame_counter += nframes;
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
                // Client killed by JACK
                if (!names) {
                    g_free(copy_spec);
                    return;
                }
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
            // Client killed by JACK
            if (!names) {
                g_free(copy_spec);
                return;
            }

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

    for (uint32_t i = 0; i < io->io_env.output_count; i++)
    {
        gchar *cbox_port = g_strdup_printf("%s:out_%d", jii->client_name, 1 + i);
        gchar *config_key = g_strdup_printf("out_%d", 1 + i);
        autoconnect_by_var(jii->client, cbox_port, config_key, 0, 0, portobj, fb);
        g_free(cbox_port);
        g_free(config_key);
    }
    for (uint32_t i = 0; i < io->io_env.input_count; i++)
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
    for (GSList *p = io->audio_outputs; p; p = g_slist_next(p))
    {
        struct cbox_jack_audio_output *audioout = p->data;
        if (audioout->autoconnect_spec)
        {
            gchar *cbox_port = g_strdup_printf("%s:%s", jii->client_name, audioout->hdr.name);
            autoconnect_by_spec(jii->client, cbox_port, audioout->autoconnect_spec, 0, 1, portobj, fb);
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

static void timebase_cb(jack_transport_state_t state, jack_nframes_t nframes,
                       jack_position_t *pos, int new_pos, void *arg)
{
    struct cbox_jack_io_impl *jii = arg;
    struct cbox_io *io = jii->ioi.pio;

    if (io->cb->get_transport_data) {
        struct cbox_transport_position tpos;

        io->cb->get_transport_data(io->cb->user_data, new_pos, pos->frame, &tpos);
        pos->valid = JackPositionBBT;
        pos->bar = tpos.bar;
        pos->beat = tpos.beat;
        pos->tick = tpos.tick;
        pos->bar_start_tick = tpos.bar_start_tick;
        pos->ticks_per_beat = tpos.ticks_per_beat;
        pos->beats_per_minute = tpos.tempo;
        pos->beats_per_bar = tpos.timesig_num;
        pos->beat_type = tpos.timesig_denom;
    }
}

static int sync_cb(jack_transport_state_t state, jack_position_t *pos, void *arg)
{
    struct cbox_jack_io_impl *jii = arg;
    struct cbox_io *io = jii->ioi.pio;
    gboolean result = TRUE;
    int last_state = jii->last_transport_state;
    switch(state)
    {
        case JackTransportStopped:
            result = io->cb->on_transport_sync(io->cb->user_data, ts_stopped, pos->frame);
            break;
        case JackTransportStarting:
            jii->last_transport_state = JackTransportStarting;
            result = io->cb->on_transport_sync(io->cb->user_data, ts_starting, pos->frame);
            break;
        case JackTransportRolling:
            result = io->cb->on_transport_sync(io->cb->user_data, ts_rolling, pos->frame);
            break;
        default:
            // assume the client is ready
            result = TRUE;
    }
    if (jii->debug_transport)
        g_message("JACK transport: incoming sync callback, state = %s, last state = %s, pos = %d, result = %d\n", transport_state_names[state], transport_state_names[last_state], (int)pos->frame, result);
    return result;
}

gboolean cbox_jackio_start(struct cbox_io_impl *impl, struct cbox_command_target *fb, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    struct cbox_io *io = jii->ioi.pio;

    if (io->cb->on_transport_sync)
        jack_set_sync_callback(jii->client, sync_cb, jii);
    if (io->cb->get_transport_data)
        jack_set_timebase_callback(jii->client, 1, timebase_cb, jii);
    jack_set_process_callback(jii->client, process_cb, jii);
    jack_set_port_registration_callback(jii->client, port_connect_cb, jii);
    jack_on_info_shutdown(jii->client, client_shutdown_cb, jii);

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
    int result = jack_deactivate(jii->client);
    if (result)
        g_warning("jack_deactivate has failed, result = %d", result);
    jack_release_timebase(jii->client);
    jack_set_process_callback(jii->client, NULL, 0);
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
    if (!jii->enable_common_midi_input)
    {
        cbox_midi_buffer_clear(destination);
        return 1;
    }

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
            for (uint32_t i = 0; i < io->io_env.input_count; i++)
                jack_port_unregister(jii->client, jii->inputs[i]);
            free(jii->inputs);
            for (uint32_t i = 0; i < io->io_env.output_count; i++)
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

struct cbox_audio_output *cbox_jackio_create_audio_out(struct cbox_io_impl *impl, const char *name, GError **error)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    jack_port_t *port = jack_port_register(jii->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!port)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create output audio port '%s'", name);
        return FALSE;
    }
    struct cbox_jack_audio_output *output = calloc(1, sizeof(struct cbox_jack_audio_output));
    output->hdr.name = g_strdup(name);
    output->hdr.removing = FALSE;
    output->port = port;
    output->jii = jii;
    cbox_uuid_generate(&output->hdr.uuid);

    return (struct cbox_audio_output *)output;
}

void cbox_jack_port_set_autoconnect(gchar **spec_ptr, const gchar *autoconnect_spec, struct cbox_jack_io_impl *jii, const gchar *port_name, gboolean is_cbox_input, gboolean is_midi)
{
    if (*spec_ptr)
        g_free(*spec_ptr);
    *spec_ptr = autoconnect_spec && *autoconnect_spec ? g_strdup(autoconnect_spec) : NULL;
    if (*spec_ptr)
    {
        gchar *cbox_port = g_strdup_printf("%s:%s", jii->client_name, port_name);
        autoconnect_by_spec(jii->client, cbox_port, *spec_ptr, is_cbox_input, is_midi, NULL, NULL);
        g_free(cbox_port);
    }
}

void cbox_jack_midi_input_set_autoconnect(struct cbox_jack_midi_input *jmi, const gchar *autoconnect_spec)
{
    cbox_jack_port_set_autoconnect(&jmi->autoconnect_spec, autoconnect_spec, jmi->jii, jmi->hdr.name, TRUE, TRUE);
}

void cbox_jack_midi_output_set_autoconnect(struct cbox_jack_midi_output *jmo, const gchar *autoconnect_spec)
{
    cbox_jack_port_set_autoconnect(&jmo->autoconnect_spec, autoconnect_spec, jmo->jii, jmo->hdr.name, FALSE, TRUE);
}

void cbox_jack_audio_output_set_autoconnect(struct cbox_jack_audio_output *jao, const gchar *autoconnect_spec)
{
    cbox_jack_port_set_autoconnect(&jao->autoconnect_spec, autoconnect_spec, jao->jii, jao->hdr.name, FALSE, FALSE);
}

void cbox_jackio_destroy_midi_in(struct cbox_io_impl *ioi, struct cbox_midi_input *midiin)
{
    cbox_jack_midi_input_destroy((struct cbox_jack_midi_input *)midiin);
}

void cbox_jackio_destroy_midi_out(struct cbox_io_impl *ioi, struct cbox_midi_output *midiout)
{
    cbox_jack_midi_output_destroy((struct cbox_jack_midi_output *)midiout);
}

void cbox_jackio_destroy_audio_out(struct cbox_io_impl *ioi, struct cbox_audio_output *audioout)
{
    cbox_jack_audio_output_destroy((struct cbox_jack_audio_output *)audioout);
}

#if JACK_HAS_RENAME
#define jack_port_rename_fn jack_port_rename
#else
#define jack_port_rename_fn(client, handle, name) jack_port_set_name(handle, name)
#endif


static gboolean cbox_jack_io_get_jack_uuid_from_name(struct cbox_jack_io_impl *jii, const char *name, jack_uuid_t *uuid, GError **error)
{
    jack_port_t *port = NULL;
    port = jack_port_by_name(jii->client, name);
    if (!port)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", name);
        return FALSE;
    }
    assert(uuid);
    jack_uuid_t temp_uuid = jack_port_uuid(port);
    if (!temp_uuid)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "JACK uuid for port '%s' not found", name);
        return FALSE;
    }
    *uuid = temp_uuid;
    return TRUE;
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
            cbox_execute_on(fb, NULL, "/external_tempo", "i", error, jii->external_tempo) &&
            cbox_io_process_cmd(io, fb, cmd, error, &handled);
    }
    else if (!strcmp(cmd->command, "/jack_transport_position") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        jack_position_t pos;
        memset(&pos, 0, sizeof(pos));
        jack_transport_state_t state = jack_transport_query(jii->client, &pos);
        if (!(cbox_execute_on(fb, NULL, "/state", "i", error, (int)state) &&
              cbox_execute_on(fb, NULL, "/unique_lo", "i", error, (int)pos.unique_1) &&
              cbox_execute_on(fb, NULL, "/unique_hi", "i", error, (int)(pos.unique_1 >> 32)) &&
              cbox_execute_on(fb, NULL, "/usecs_lo", "i", error, (int)pos.usecs) &&
              cbox_execute_on(fb, NULL, "/usecs_hi", "i", error, (int)(pos.usecs >> 32)) &&
              cbox_execute_on(fb, NULL, "/frame_rate", "i", error, (int)(pos.frame_rate)) &&
              cbox_execute_on(fb, NULL, "/frame", "i", error, (int)(pos.frame))))
            return FALSE;
        if ((pos.valid & JackPositionBBT) && !(
            cbox_execute_on(fb, NULL, "/bar", "i", error, (int)pos.bar) &&
            cbox_execute_on(fb, NULL, "/beat", "i", error, (int)pos.beat) &&
            cbox_execute_on(fb, NULL, "/tick", "i", error, (int)pos.tick) &&
            cbox_execute_on(fb, NULL, "/bar_start_tick", "f", error, (int)pos.bar_start_tick) &&
            cbox_execute_on(fb, NULL, "/beats_per_bar", "f", error, (double)pos.beats_per_bar) &&
            cbox_execute_on(fb, NULL, "/beat_type", "f", error, (double)pos.beat_type) &&
            cbox_execute_on(fb, NULL, "/ticks_per_beat", "f", error, (double)pos.ticks_per_beat) &&
            cbox_execute_on(fb, NULL, "/beats_per_minute", "f", error, (double)pos.beats_per_minute)))
            return FALSE;
        if ((pos.valid & JackBBTFrameOffset) && !(
            cbox_execute_on(fb, NULL, "/bbt_frame_offset", "i", error, (int)pos.bbt_offset)))
            return FALSE;
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/jack_transport_locate") && !strcmp(cmd->arg_types, "i"))
    {
        jack_transport_locate(jii->client, (uint32_t)CBOX_ARG_I(cmd, 0));
        return TRUE;
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
        if (0 != jack_port_rename_fn(jii->client, port, new_name))
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set port name to '%s'", new_name);
            return FALSE;
        }
        g_free(*pname);
        *pname = g_strdup(new_name);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/rename_audio_port") && !strcmp(cmd->arg_types, "ss"))
    {
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        const char *new_name = CBOX_ARG_S(cmd, 1);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_audio_output *audioout = cbox_io_get_audio_output(io, NULL, &uuid);
        if (!audioout)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        if (0 != jack_port_rename_fn(jii->client, ((struct cbox_jack_audio_output *)audioout)->port, new_name))
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set port name to '%s'", new_name);
            return FALSE;
        }
        g_free(audioout->name);
        audioout->name = g_strdup(new_name);
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
        struct cbox_audio_output *audioout = cbox_io_get_audio_output(io, NULL, &uuid);
        if (audioout)
        {
            cbox_jack_audio_output_set_autoconnect((struct cbox_jack_audio_output *)audioout, spec);
            return TRUE;
        }
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
        return FALSE;
    }
    else if (!strcmp(cmd->command, "/disconnect_audio_output") && !strcmp(cmd->arg_types, "s"))
    {
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_audio_output *audioout = cbox_io_get_audio_output(io, NULL, &uuid);
        if (!audioout)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        jack_port_disconnect(jii->client, ((struct cbox_jack_audio_output *)audioout)->port);
        return TRUE;
    }
    else if (!strncmp(cmd->command, "/disconnect_midi_", 17) && !strcmp(cmd->arg_types, "s"))
    {
        bool is_both = !strcmp(cmd->command + 17, "port");
        bool is_in = is_both || !strcmp(cmd->command + 17, "input");
        bool is_out = is_both || !strcmp(cmd->command + 17, "output");
        if (is_in || is_out) {
            const char *uuidstr = CBOX_ARG_S(cmd, 0);
            struct cbox_uuid uuid;
            if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
                return FALSE;
            struct cbox_midi_input *midiin = is_in ? cbox_io_get_midi_input(io, NULL, &uuid) : NULL;
            struct cbox_midi_output *midiout = is_out ? cbox_io_get_midi_output(io, NULL, &uuid) : NULL;
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
    }

    if (!strcmp(cmd->command, "/port_connect") && !strcmp(cmd->arg_types, "ss"))
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
    else if (!strcmp(cmd->command, "/get_connected_ports") && !strcmp(cmd->arg_types, "s"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        const char *name = CBOX_ARG_S(cmd, 0);
        jack_port_t *port = NULL;
        if (!strchr(name, ':')) {
            // try UUID
            struct cbox_uuid uuid;
            if (!cbox_uuid_fromstring(&uuid, name, error))
                return FALSE;
            struct cbox_midi_output *midiout = cbox_io_get_midi_output(io, NULL, &uuid);
            if (midiout)
                port = ((struct cbox_jack_midi_output *)midiout)->port;
            else {
                struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
                if (midiin)
                    port = ((struct cbox_jack_midi_input *)midiin)->port;
            }
        }
        else
            port = jack_port_by_name(jii->client, name);
        if (!port)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", name);
            return FALSE;
        }
        const char** ports = jack_port_get_all_connections(jii->client, port);
        for (int i = 0; ports && ports[i]; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/port", "s", error, ports[i]))
                return FALSE;
        }
        jack_free(ports);
        return TRUE;
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
        jack_free(ports);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/external_tempo") && !strcmp(cmd->arg_types, "i"))
    {
        jii->external_tempo = CBOX_ARG_I(cmd, 0);
        return TRUE;
    }

    //Metadata

    else if (!strcmp(cmd->command, "/set_property") && !strcmp(cmd->arg_types, "ssss"))
    //parameters: "client:port", key, value, type according to jack_property_t (empty or NULL for string)
    {
        const char *name = CBOX_ARG_S(cmd, 0);
        const char *key = CBOX_ARG_S(cmd, 1);
        const char *value = CBOX_ARG_S(cmd, 2);
        const char *type = CBOX_ARG_S(cmd, 3);

        jack_uuid_t subject;
        if (!cbox_jack_io_get_jack_uuid_from_name(jii, name, &subject, error)) //error message set inside
            return FALSE;

        if (jack_set_property(jii->client, subject, key, value, type)) // 0 on success
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Set property key:'%s' value: '%s' to port '%s' was not successful", key, value, name);
            return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/get_property") && !strcmp(cmd->arg_types, "ss"))
    //parameters: "client:port", key
    //returns python key, value and type as strings
    {
        const char *name = CBOX_ARG_S(cmd, 0);
        const char *key = CBOX_ARG_S(cmd, 1);

        jack_uuid_t subject;
        if (!cbox_jack_io_get_jack_uuid_from_name(jii, name, &subject, error)) //error message set inside
            return FALSE;

        char* value = NULL;
        char* type = NULL;

        if (jack_get_property(subject, key, &value, &type)) // 0 on success, -1 if the subject has no key property.
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' does not have key '%s'", name, key);
            return FALSE;
        }

        char* returntype; //We need to call jack_free on type in any case so it can't be our own data.
        if (type == NULL)
             returntype = "";
        else
            returntype = type;

        if (!cbox_execute_on(fb, NULL, "/value", "ss", error, value, returntype)) //send return values to Python.
           return FALSE;

        jack_free(value);
        jack_free(type);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/get_properties") && !strcmp(cmd->arg_types, "s"))
    //parameters: "client:port"
    {
        const char *name = CBOX_ARG_S(cmd, 0);

        jack_uuid_t subject;
        if (!cbox_jack_io_get_jack_uuid_from_name(jii, name, &subject, error))  //error message set inside
            return FALSE;

        jack_description_t desc;
        if (!jack_get_properties(subject, &desc)) // 0 on success, -1 if no subject with any properties exists.
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' with uuid '%lli' does not have any properties", name, (long long)subject);
            return FALSE;
        }

        const char *returntype;
        for (uint32_t i = 0; i<desc.property_cnt ; i++)
        {
            if (desc.properties[i].type == NULL)
                 returntype = "";
            else
                returntype = desc.properties[i].type;
            if (!cbox_execute_on(fb, NULL, "/properties", "sss", error, desc.properties[i].key, desc.properties[i].data, returntype))
                return FALSE;
        }
        jack_free_description(&desc, 0); //if non-zero desc will also be passed to free()
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/get_all_properties") && !strcmp(cmd->arg_types, ""))
    {
        jack_description_t *descs;
        int counter;
        counter = jack_get_all_properties(&descs);
        const char *returntype;
        for (int j = 0; j < counter; j++)
        {
            jack_description_t *one_desc = &descs[j];
            for (uint32_t i = 0; i < one_desc->property_cnt ; i++)
            {
                if (one_desc->properties[i].type == NULL)
                    returntype = "";
                else
                    returntype = one_desc->properties[i].type;

                /*
                index = jack_uuid_to_index(one_desc->subject)
                portid = jack_port_by_id(jii->client, index);
                portname = jack_port_name(port);
                */
                if (!cbox_execute_on(fb, NULL, "/all_properties", "ssss",
                        error,
                        jack_port_name(jack_port_by_id(jii->client, jack_uuid_to_index(one_desc->subject))),
                        one_desc->properties[i].key,
                        one_desc->properties[i].data,
                        returntype))
                    return FALSE;
            }
            jack_free_description(one_desc, 0); //if non-zero one_desc will also be passed to free()
        }
        jack_free(descs);
        return TRUE;
    }


    else if (!strcmp(cmd->command, "/remove_property") && !strcmp(cmd->arg_types, "ss"))
    {
        const char *name = CBOX_ARG_S(cmd, 0);
        const char *key = CBOX_ARG_S(cmd, 1);

        jack_uuid_t subject;
        if (!cbox_jack_io_get_jack_uuid_from_name(jii, name, &subject, error)) //error message set inside
            return FALSE;

        if (jack_remove_property(jii->client, subject, key)) // 0 on success, -1 otherwise
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Could not remove port '%s' key '%s'", name, key);
            return FALSE;
        }
        return TRUE;
    }

    else if (!strcmp(cmd->command, "/remove_properties") && !strcmp(cmd->arg_types, "s"))
    {
        const char *name = CBOX_ARG_S(cmd, 0);

        jack_uuid_t subject;
        if (!cbox_jack_io_get_jack_uuid_from_name(jii, name, &subject, error)) //error message set inside
            return FALSE;

        if (jack_remove_properties(jii->client, subject) == -1) // number of removed properties returned, -1 on error
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Could not remove properties of port '%s'", name);
            return FALSE;
        }
        return TRUE;
    }


    else if (!strcmp(cmd->command, "/remove_all_properties") && !strcmp(cmd->arg_types, ""))
    {
        if (jack_remove_all_properties(jii->client)) // 0 on success, -1 otherwise
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Remove all JACK properties was not successful");
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

static void cbox_jackio_control_transport(struct cbox_io_impl *impl, gboolean roll, uint32_t pos)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;

    if (jii->debug_transport)
        g_message("JACK transport: control(op=%s, pos=%d)\n", roll ? "roll" : "stop", (int)pos);

    jack_transport_state_t state = jack_transport_query(jii->client, NULL);
    if (roll && pos != (uint32_t)-1)
        jack_transport_locate(jii->client, pos);
    if (roll && state == JackTransportStopped)
        jack_transport_start(jii->client);
    if (!roll && state != JackTransportStopped)
        jack_transport_stop(jii->client);
    if (!roll && pos != (uint32_t)-1)
        jack_transport_locate(jii->client, pos);
}

static gboolean cbox_jackio_get_sync_completed(struct cbox_io_impl *impl)
{
    struct cbox_jack_io_impl *jii = (struct cbox_jack_io_impl *)impl;
    return jack_transport_query(jii->client, NULL) != JackTransportStarting;
}

///////////////////////////////////////////////////////////////////////////////

static void cbox_jackio_update_midi_in_routing(struct cbox_io_impl *impl)
{
    // XXXKF slow and wasteful, but that's okay for now
    for (GSList *p = impl->pio->midi_inputs; p; p = g_slist_next(p))
    {
        struct cbox_midi_input *midiin = p->data;
        for (GSList *q = impl->pio->midi_outputs; q; q = g_slist_next(q))
        {
            struct cbox_midi_output *midiout = q->data;

            bool add = midiin->output_set && !midiout->removing && cbox_uuid_equal(&midiout->uuid, &midiin->output);
            if (add)
                cbox_midi_merger_connect(&midiout->merger, &midiin->buffer, app.rt, NULL);
            else
                cbox_midi_merger_disconnect(&midiout->merger, &midiin->buffer, app.rt);
        }
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
    jii->enable_common_midi_input = cbox_config_get_int("io", "enable_common_midi_input", 1);
    jii->debug_transport = cbox_config_get_int("debug", "jack_transport", 0);
    jii->last_transport_state = JackTransportStopped;
    jii->external_tempo = FALSE;

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
    jii->ioi.updatemidiinroutingfunc = cbox_jackio_update_midi_in_routing;
    jii->ioi.createaudiooutfunc = cbox_jackio_create_audio_out;
    jii->ioi.destroyaudiooutfunc = cbox_jackio_destroy_audio_out;
    jii->ioi.controltransportfunc = cbox_jackio_control_transport;
    jii->ioi.getsynccompletedfunc = cbox_jackio_get_sync_completed;
    jii->ioi.destroyfunc = cbox_jackio_destroy;

    jii->client_name = g_strdup(jack_get_client_name(client));
    jii->client = client;
    jii->rb_autoconnect = jack_ringbuffer_create(sizeof(jack_port_t *) * 128);
    jii->error_str = NULL;
    io->io_env.srate = jack_get_sample_rate(client);

    jii->inputs = malloc(sizeof(jack_port_t *) * io->io_env.input_count);
    jii->outputs = malloc(sizeof(jack_port_t *) * io->io_env.output_count);
    for (uint32_t i = 0; i < io->io_env.input_count; i++)
        jii->inputs[i] = NULL;
    for (uint32_t i = 0; i < io->io_env.output_count; i++)
        jii->outputs[i] = NULL;
    for (uint32_t i = 0; i < io->io_env.input_count; i++)
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
    for (uint32_t i = 0; i < io->io_env.output_count; i++)
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
    if (jii->enable_common_midi_input)
    {
        jii->midi = jack_port_register(jii->client, "midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        if (!jii->midi)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create MIDI port");
            return FALSE;
        }
    }
    else
        jii->midi = NULL;

    if (fb)
        cbox_execute_on(fb, NULL, "/io/jack_client_name", "s", NULL, jii->client_name);

    cbox_io_poll_ports(io, fb);

    return TRUE;

cleanup:
    if (jii->inputs)
    {
        for (uint32_t i = 0; i < io->io_env.input_count; i++)
            free(jii->inputs[i]);
        free(jii->inputs);
    }
    if (jii->outputs)
    {
        for (uint32_t i = 0; i < io->io_env.output_count; i++)
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

#endif
