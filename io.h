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

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include "master.h"

struct cbox_document;
struct cbox_io_callbacks;
struct cbox_recording_source;
struct cbox_meter;
struct cbox_midi_buffer;

struct cbox_open_params
{
};

struct cbox_io
{
    jack_client_t *client;
    jack_port_t **inputs;
    int input_count;
    float **input_buffers; // only valid inside jack_rt_process
    jack_port_t **outputs;
    int output_count;
    float **output_buffers; // only valid inside jack_rt_process
    jack_port_t *midi;
    int buffer_size;
    struct cbox_recording_source *rec_mono_inputs, *rec_mono_outputs;
    struct cbox_recording_source *rec_stereo_inputs, *rec_stereo_outputs;
    
    jack_ringbuffer_t *rb_autoconnect;
    
    
    
    struct cbox_io_callbacks *cb;
};

struct cbox_io_callbacks
{
    void *user_data;
    
    void (*process)(void *user_data, struct cbox_io *io, uint32_t nframes);
};

extern int cbox_io_init(struct cbox_io *io, struct cbox_document *doc, struct cbox_open_params *const params);
extern int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb);
extern int cbox_io_stop(struct cbox_io *io);
extern int cbox_io_get_sample_rate(struct cbox_io *io);
static inline int cbox_io_get_buffer_size(struct cbox_io *io)
{
    return io->buffer_size;
}
extern int cbox_io_get_midi_data(struct cbox_io *io, struct cbox_midi_buffer *destination);
extern void cbox_io_poll_ports(struct cbox_io *io);
extern void cbox_io_close(struct cbox_io *io);

#endif
