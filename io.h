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
#include "dom.h"
#include "master.h"
#include "mididest.h"

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
    gboolean (*startfunc)(struct cbox_io_impl *ioi, struct cbox_command_target *fb, GError **error);
    gboolean (*stopfunc)(struct cbox_io_impl *ioi, GError **error);
    gboolean (*cyclefunc)(struct cbox_io_impl *ioi, struct cbox_command_target *fb, GError **error);
    gboolean (*getstatusfunc)(struct cbox_io_impl *ioi, GError **error);
    void (*pollfunc)(struct cbox_io_impl *ioi, struct cbox_command_target *fb);
    int (*getmidifunc)(struct cbox_io_impl *ioi, struct cbox_midi_buffer *destination);
    struct cbox_midi_output *(*createmidioutfunc)(struct cbox_io_impl *ioi, const char *name, GError **error);
    void (*destroymidioutfunc)(struct cbox_io_impl *ioi, struct cbox_midi_output *midiout);
    void (*destroyfunc)(struct cbox_io_impl *ioi);
};

struct cbox_io
{
    struct cbox_io_impl *impl;
    struct cbox_command_target cmd_target;

    int input_count;
    float **input_buffers; // only valid inside jack_rt_process
    int output_count;
    float **output_buffers; // only valid inside jack_rt_process
    int buffer_size;
    
    struct cbox_io_callbacks *cb;
    GSList *midi_outputs;
};

struct cbox_io_callbacks
{
    void *user_data;
    
    void (*process)(void *user_data, struct cbox_io *io, uint32_t nframes);
    void (*on_disconnected)(void *user_data);
    void (*on_reconnected)(void *user_data);
    void (*on_midi_outputs_changed)(void *user_data);
};

struct cbox_midi_output
{
    gchar *name;
    struct cbox_uuid uuid;
    struct cbox_midi_buffer buffer;
    struct cbox_midi_merger merger;
    // This is set if the output is in process of being removed and should not
    // be used for output.
    gboolean removing;
};

extern gboolean cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params, struct cbox_command_target *fb, GError **error);
extern gboolean cbox_io_init_jack(struct cbox_io *io, struct cbox_open_params *const params, struct cbox_command_target *fb, GError **error);
extern gboolean cbox_io_init_usb(struct cbox_io *io, struct cbox_open_params *const params, struct cbox_command_target *fb, GError **error);

extern int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb, struct cbox_command_target *fb);
extern int cbox_io_stop(struct cbox_io *io);
extern int cbox_io_get_sample_rate(struct cbox_io *io);
static inline int cbox_io_get_buffer_size(struct cbox_io *io)
{
    return io->buffer_size;
}
extern int cbox_io_get_midi_data(struct cbox_io *io, struct cbox_midi_buffer *destination);
extern gboolean cbox_io_get_disconnect_status(struct cbox_io *io, GError **error);
extern gboolean cbox_io_cycle(struct cbox_io *io, struct cbox_command_target *fb, GError **error);
extern void cbox_io_poll_ports(struct cbox_io *io, struct cbox_command_target *fb);
extern struct cbox_midi_output *cbox_io_get_midi_output(struct cbox_io *io, const char *name, const struct cbox_uuid *uuid);
extern struct cbox_midi_output *cbox_io_create_midi_output(struct cbox_io *io, const char *name, GError **error);
extern void cbox_io_destroy_midi_output(struct cbox_io *io, struct cbox_midi_output *midiout);
extern void cbox_io_destroy_all_midi_outputs(struct cbox_io *io);
extern gboolean cbox_io_process_cmd(struct cbox_io *io, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error, gboolean *cmd_handled);
extern void cbox_io_close(struct cbox_io *io);

extern const char *cbox_io_section;

#endif
