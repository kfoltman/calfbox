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

#ifndef CBOX_IO_H
#define CBOX_IO_H

#include <glib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include "master.h"

struct cbox_io;
struct cbox_io_callbacks;
struct cbox_recording_source;
struct cbox_meter;
struct cbox_midi_buffer;
struct cbox_scene;

struct cbox_open_params
{
};

struct cbox_io_impl
{
    struct cbox_io *pio;

    int (*getsampleratefunc)(struct cbox_io_impl *ioi);
    gboolean (*startfunc)(struct cbox_io_impl *ioi, GError **error);
    gboolean (*stopfunc)(struct cbox_io_impl *ioi, GError **error);
    gboolean (*cyclefunc)(struct cbox_io_impl *ioi, GError **error);
    gboolean (*getstatusfunc)(struct cbox_io_impl *ioi, GError **error);
    void (*pollfunc)(struct cbox_io_impl *ioi);
    int (*getmidifunc)(struct cbox_io_impl *ioi, struct cbox_midi_buffer *destination);
    void (*destroyfunc)(struct cbox_io_impl *ioi);
};

struct cbox_jack_io_impl
{
    struct cbox_io_impl ioi;

    jack_client_t *client;
    jack_port_t **inputs;
    jack_port_t **outputs;
    jack_port_t *midi;
    char *error_str; // set to non-NULL if client has been booted out by JACK

    jack_ringbuffer_t *rb_autoconnect;    
};

struct cbox_io
{
    struct cbox_io_impl *impl;

    int input_count;
    float **input_buffers; // only valid inside jack_rt_process
    int output_count;
    float **output_buffers; // only valid inside jack_rt_process
    int buffer_size;
    
    struct cbox_io_callbacks *cb;
};

struct cbox_io_callbacks
{
    void *user_data;
    
    void (*process)(void *user_data, struct cbox_io *io, uint32_t nframes);
    void (*on_disconnected)(void *user_data);
    void (*on_reconnected)(void *user_data);
};

extern gboolean cbox_io_init_jack(struct cbox_io *io, struct cbox_open_params *const params, GError **error);
extern int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb);
extern int cbox_io_stop(struct cbox_io *io);
extern int cbox_io_get_sample_rate(struct cbox_io *io);
static inline int cbox_io_get_buffer_size(struct cbox_io *io)
{
    return io->buffer_size;
}
extern int cbox_io_get_midi_data(struct cbox_io *io, struct cbox_midi_buffer *destination);
extern gboolean cbox_io_get_disconnect_status(struct cbox_io *io, GError **error);
extern gboolean cbox_io_cycle(struct cbox_io *io, GError **error);
extern void cbox_io_poll_ports(struct cbox_io *io);
extern void cbox_io_close(struct cbox_io *io);

#endif
