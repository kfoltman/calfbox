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

#include "config-api.h"
#include "midi.h"

int cbox_midi_buffer_write_event(struct cbox_midi_buffer *buffer, uint32_t time, uint8_t *data, uint32_t size)
{
    struct cbox_midi_event *evt;
    
    if (buffer->count >= CBOX_MIDI_MAX_EVENTS)
        return 0;
    if (size > 4 && size > CBOX_MIDI_MAX_LONG_DATA - buffer->long_data_size)
        return 0;
    evt = &buffer->events[buffer->count++];
    evt->time = time;
    evt->size = size;
    if (size <= 4)
    {
        memcpy(evt->data_inline, data, size);
    }
    else
    {
        evt->data_ext = buffer->long_data + buffer->long_data_size;
        memcpy(evt->data_ext, data, size);
        buffer->long_data_size += size;
    }
    return 1;
}

int cbox_midi_buffer_copy_event(struct cbox_midi_buffer *buffer, const struct cbox_midi_event *event, int ofs)
{
    struct cbox_midi_event *evt;
    
    if (buffer->count >= CBOX_MIDI_MAX_EVENTS)
        return 0;
    if (event->size > 4 && event->size > CBOX_MIDI_MAX_LONG_DATA - buffer->long_data_size)
        return 0;
    evt = &buffer->events[buffer->count++];
    evt->time = event->time + ofs;
    evt->size = event->size;
    if (event->size <= 4)
    {
        memcpy(evt->data_inline, event->data_inline, event->size);
    }
    else
    {
        evt->data_ext = buffer->long_data + buffer->long_data_size;
        memcpy(evt->data_ext, event->data_ext, event->size);
        buffer->long_data_size += event->size;
    }
    return 1;
}

int note_from_string(const char *note)
{
    static const int semis[] = {9, 11, 0, 2, 4, 5, 7};
    int pos;
    int nn = tolower(note[0]);
    int nv;
    if (nn >= '0' && nn <= '9')
        return atoi(note);
    if (nn < 'a' && nn > 'g')
        return -1;
    nv = semis[nn - 'a'];
    
    for (pos = 1; note[pos] == 'b' || note[pos] == '#'; pos++)
        nv += (note[pos] == 'b') ? -1 : +1;
    
    if ((note[pos] == '-' && note[pos + 1] >= '1' && note[pos + 1] <= '2' && note[pos + 2] == '\0') || (note[pos] >= '0' && note[pos] <= '9' && note[pos + 1] == '\0'))
    {
        return nv + 12 * (2 + atoi(note + pos));
    }
    
    return -1;
}

void cbox_midi_buffer_merge(struct cbox_midi_buffer *output, struct cbox_midi_buffer **inputs, int count, int *positions)
{
    while(1)
    {
        int first_event_time = 0, first_event_input = -1;
        struct cbox_midi_event *first_event = NULL;
        for (int i = 0; i < count; i++)
        {
            if (positions[i] < inputs[i]->count)
            {
                struct cbox_midi_event *event = cbox_midi_buffer_get_event(inputs[i], positions[i]);
                if (first_event == NULL || event->time < first_event->time)
                {
                    first_event_input = i;
                    first_event = event;
                }
            }
        }
        if (first_event)
        {
            cbox_midi_buffer_copy_event(output, first_event, 0);
            positions[first_event_input]++;
        }
        else
            break;
    }
}

int cbox_config_get_note(const char *cfg_section, const char *key, int def_value)
{
    const char *cv = cbox_config_get_string(cfg_section, key);
    if (cv)
        return note_from_string(cv);
    return def_value;
}

