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

#ifndef CBOX_MIDI_H
#define CBOX_MIDI_H

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct cbox_midi_event
{
    uint32_t time;
    uint32_t size;
    union {
        uint8_t data_inline[4]; /* up to 4 bytes */
        uint8_t *data_ext; /* if larger than 4 bytes */
    };
};

#define CBOX_MIDI_MAX_EVENTS 256
#define CBOX_MIDI_MAX_LONG_DATA 256

struct cbox_midi_buffer
{
    uint32_t count;
    uint32_t long_data_size;
    struct cbox_midi_event events[CBOX_MIDI_MAX_EVENTS];
    uint8_t long_data[CBOX_MIDI_MAX_LONG_DATA];
};

static inline void cbox_midi_buffer_init(struct cbox_midi_buffer *buffer)
{
    buffer->count = 0;
    buffer->long_data_size = 0;
}

static inline void cbox_midi_buffer_clear(struct cbox_midi_buffer *buffer)
{
    buffer->count = 0;
    buffer->long_data_size = 0;
}

static inline void cbox_midi_buffer_copy(struct cbox_midi_buffer *dst, const struct cbox_midi_buffer *src)
{
    dst->count = src->count;
    dst->long_data_size = src->long_data_size;
    memcpy(dst->events, src->events, src->count * sizeof(struct cbox_midi_event));
    memcpy(dst->long_data, src->long_data, src->long_data_size);
    // for any long events, update data pointers
    for (int i = 0; i < src->count; i++)
    {
        if (dst->events[i].size > 4)
            dst->events[i].data_ext += &dst->long_data[0] - &src->long_data[0];
    }
}

static inline uint32_t cbox_midi_buffer_get_count(struct cbox_midi_buffer *buffer)
{
    return buffer->count;
}

static inline uint32_t cbox_midi_buffer_get_last_event_time(struct cbox_midi_buffer *buffer)
{
    if (!buffer->count)
        return 0;
    return buffer->events[buffer->count - 1].time;
}

static inline int cbox_midi_buffer_can_store_msg(struct cbox_midi_buffer *buffer, int size)
{
    if (buffer->count >= CBOX_MIDI_MAX_EVENTS)
        return 0;
    if (size < 4)
        return 1;
    return buffer->long_data_size + size <= CBOX_MIDI_MAX_LONG_DATA;
}

static inline struct cbox_midi_event *cbox_midi_buffer_get_event(struct cbox_midi_buffer *buffer, uint32_t pos)
{
    if (pos >= buffer->count)
        return NULL;
    return &buffer->events[pos];
}

static inline uint8_t *cbox_midi_event_get_data(struct cbox_midi_event *evt)
{
    return evt->size > 4 ? evt->data_ext : evt->data_inline;
}

static inline int midi_cmd_size(uint8_t cmd)
{
    static const int sizes[] = { 3, 3, 3, 3, 2, 2, 3, 1 };
    if (cmd < 128)
        return 0;
    return sizes[(cmd >> 4) - 8];
}

extern int cbox_midi_buffer_write_event(struct cbox_midi_buffer *buffer, uint32_t time, uint8_t *data, uint32_t size);

extern int cbox_midi_buffer_write_inline(struct cbox_midi_buffer *buffer, uint32_t time, ...);

extern int cbox_midi_buffer_copy_event(struct cbox_midi_buffer *buffer, const struct cbox_midi_event *event, int new_time);

extern int note_from_string(const char *note);

extern int cbox_config_get_note(const char *cfg_section, const char *key, int def_value);

#endif
