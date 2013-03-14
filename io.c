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

const char *cbox_io_section = "io";

gboolean cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params, struct cbox_command_target *fb, GError **error)
{
    if (cbox_config_get_int(cbox_io_section, "use_usb", 0))
        return cbox_io_init_usb(io, params, fb, error);
    return cbox_io_init_jack(io, params, fb, error);
}

int cbox_io_get_sample_rate(struct cbox_io *io)
{
    return io->impl->getsampleratefunc(io->impl);
}

void cbox_io_poll_ports(struct cbox_io *io, struct cbox_command_target *fb)
{
    io->impl->pollfunc(io->impl, fb);
}

int cbox_io_get_midi_data(struct cbox_io *io, struct cbox_midi_buffer *destination)
{
    return io->impl->getmidifunc(io->impl, destination);
}

int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb, struct cbox_command_target *fb)
{
    io->cb = cb;
    return io->impl->startfunc(io->impl, fb, NULL);
}

gboolean cbox_io_get_disconnect_status(struct cbox_io *io, GError **error)
{
    return io->impl->getstatusfunc(io->impl, error);
}

gboolean cbox_io_cycle(struct cbox_io *io, struct cbox_command_target *fb, GError **error)
{
    return io->impl->cyclefunc(io->impl, fb, error);
}

int cbox_io_stop(struct cbox_io *io)
{
    return io->impl->stopfunc(io->impl, NULL);
}

struct cbox_midi_output *cbox_io_get_midi_output(struct cbox_io *io, const char *name, const struct cbox_uuid *uuid)
{
    if (uuid)
    {
        for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_output *midiout = p->data;
            if (!midiout->removing && cbox_uuid_equal(&midiout->uuid, uuid))
                return midiout;
        }
    }
    if (name)
    {
        for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_output *midiout = p->data;
            if (!midiout->removing && !strcmp(midiout->name, name))
                return midiout;
        }
    }
    return NULL;
}

struct cbox_midi_output *cbox_io_create_midi_output(struct cbox_io *io, const char *name, GError **error)
{
    struct cbox_midi_output *midiout = cbox_io_get_midi_output(io, name, NULL);
    if (midiout)
        return midiout;
    
    midiout = io->impl->createmidioutfunc(io->impl, name, error);
    if (!midiout)
        return NULL;
    
    io->midi_outputs = g_slist_prepend(io->midi_outputs, midiout);

    // Notify client code to connect to new outputs if needed
    if (io->cb->on_midi_outputs_changed)
        io->cb->on_midi_outputs_changed(io->cb->user_data);
    return midiout;
}

void cbox_io_destroy_midi_output(struct cbox_io *io, struct cbox_midi_output *midiout)
{
    midiout->removing = TRUE;
    
    // This is not a very efficient way to do it. However, in this case,
    // the list will rarely contain more than 10 elements, so simplicity
    // and correctness may be more important.
    GSList *copy = g_slist_copy(io->midi_outputs);
    copy = g_slist_remove(copy, midiout);

    GSList *old = io->midi_outputs;
    io->midi_outputs = copy;

    // Notify client code to disconnect the output and to make sure the RT code
    // is not using the old list anymore
    if (io->cb->on_midi_outputs_changed)
        io->cb->on_midi_outputs_changed(io->cb->user_data);
    
    g_slist_free(old);
    io->impl->destroymidioutfunc(io->impl, midiout);
}

void cbox_io_close(struct cbox_io *io)
{
    io->impl->destroyfunc(io->impl);
    io->impl = NULL;
}

