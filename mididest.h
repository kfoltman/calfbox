/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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

#ifndef CBOX_MIDIDEST_H
#define CBOX_MIDIDEST_H

#include "midi.h"
#include <glib.h>

struct cbox_command_target;
struct cbox_rt;

struct cbox_midi_source
{
    struct cbox_midi_source *next;
    struct cbox_midi_buffer *data;
    int bpos;
    gboolean streaming;
};

struct cbox_midi_merger
{
    struct cbox_midi_source *inputs;
    struct cbox_midi_buffer *output;
};

void cbox_midi_merger_init(struct cbox_midi_merger *dest, struct cbox_midi_buffer *output);
void cbox_midi_merger_render_to(struct cbox_midi_merger *dest, struct cbox_midi_buffer *output);
static inline void cbox_midi_merger_render(struct cbox_midi_merger *dest)
{
    if (dest->output)
        cbox_midi_merger_render_to(dest, dest->output);
}
struct cbox_midi_source **cbox_midi_merger_find_source(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer);
void cbox_midi_merger_connect(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt);
void cbox_midi_merger_disconnect(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt);
void cbox_midi_merger_push(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt);
void cbox_midi_merger_close(struct cbox_midi_merger *dest);

#define GET_RT_FROM_cbox_midi_appsink(appsink) ((appsink)->rt)

struct cbox_midi_appsink
{
    struct cbox_rt *rt;
    struct cbox_midi_buffer midibufs[2];
    int current_buffer;
};

extern void cbox_midi_appsink_init(struct cbox_midi_appsink *appsink, struct cbox_rt *rt);
extern void cbox_midi_appsink_supply(struct cbox_midi_appsink *appsink, struct cbox_midi_buffer *buffer);
extern const struct cbox_midi_buffer *cbox_midi_appsink_get_input_midi_data(struct cbox_midi_appsink *appsink);
extern gboolean cbox_midi_appsink_send_to(struct cbox_midi_appsink *appsink, struct cbox_command_target *fb, GError **error);

#endif
