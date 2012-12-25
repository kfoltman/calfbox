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

gboolean cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params, GError **error)
{
    return cbox_io_init_jack(io, params, error);
}

int cbox_io_get_sample_rate(struct cbox_io *io)
{
    return io->impl->getsampleratefunc(io->impl);
}

void cbox_io_poll_ports(struct cbox_io *io)
{
    io->impl->pollfunc(io->impl);
}

int cbox_io_get_midi_data(struct cbox_io *io, struct cbox_midi_buffer *destination)
{
    return io->impl->getmidifunc(io->impl, destination);
}

int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb)
{
    io->cb = cb;
    return io->impl->startfunc(io->impl, NULL);
}

gboolean cbox_io_get_disconnect_status(struct cbox_io *io, GError **error)
{
    return io->impl->getstatusfunc(io->impl, error);
}

gboolean cbox_io_cycle(struct cbox_io *io, GError **error)
{
    return io->impl->cyclefunc(io->impl, error);
}

int cbox_io_stop(struct cbox_io *io)
{
    return io->impl->stopfunc(io->impl, NULL);
}

void cbox_io_close(struct cbox_io *io)
{
    io->impl->destroyfunc(io->impl);
    io->impl = NULL;
}

