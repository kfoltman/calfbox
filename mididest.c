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

#include "blob.h"
#include "mididest.h"
#include "rt.h"
#include "stm.h"

void cbox_midi_merger_init(struct cbox_midi_merger *dest, struct cbox_midi_buffer *output)
{
    dest->inputs = NULL;
    dest->input_count = 0;
    dest->output = output;
    if (dest->output)
        cbox_midi_buffer_clear(dest->output);
}

// void cbox_midi_buffer_merge(struct cbox_midi_buffer *output, struct cbox_midi_buffer **inputs, int count, int *positions)
void cbox_midi_merger_render_to(struct cbox_midi_merger *dest, struct cbox_midi_buffer *output)
{
    if (!output)
        return;
    cbox_midi_buffer_clear(output);
    for (int i = 0; i < dest->input_count; i++)
    {
        if (dest->inputs[i]->streaming)
            dest->inputs[i]->bpos = 0;
    }

    while(1)
    {
        struct cbox_midi_source *earliest_source = NULL;
        uint32_t earliest_time = (uint32_t)-1;
        
        uint32_t mask = 0;
        int icount = dest->input_count;
        int spos = 0;
        
        for (int i = spos; i < icount; i++)
        {
            if (mask & (1 << i))
                continue;
            struct cbox_midi_source *src = dest->inputs[i];
            struct cbox_midi_buffer *data = src->data;
            if (src->bpos < data->count)
            {
                const struct cbox_midi_event *event = cbox_midi_buffer_get_event(data, src->bpos);
                if (event->time < earliest_time)
                {
                    earliest_source = src;
                    earliest_time = event->time;
                }
            }
            else
            {
                mask |= 1 << i;
                // Narrow down the range from top and bottom
                if (i == spos)
                    spos = i + 1;
                if (i == icount - 1)
                    icount = i;
            }
        }
        if (earliest_source)
        {
            cbox_midi_buffer_copy_event(output, cbox_midi_buffer_get_event(earliest_source->data, earliest_source->bpos), earliest_time);
            earliest_source->bpos++;
        }
        else
            break;
    }    
}

int cbox_midi_merger_find_source(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer)
{
    for (int i = 0; i < dest->input_count; i++)
        if (dest->inputs[i]->data == buffer)
            return i;
    return -1;
}

void cbox_midi_merger_connect(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt)
{
    if (cbox_midi_merger_find_source(dest, buffer) != -1)
        return;
    
    struct cbox_midi_source *src = calloc(1, sizeof(struct cbox_midi_source));
    src->data = buffer;
    src->bpos = 0;
    src->streaming = TRUE;
    cbox_rt_array_insert(rt, (void ***)&dest->inputs, &dest->input_count, dest->input_count, src);
}

void cbox_midi_merger_disconnect(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt)
{
    int pos = cbox_midi_merger_find_source(dest, buffer);
    if (pos == -1)
        return;

    cbox_rt_array_remove(rt, (void ***)&dest->inputs, &dest->input_count, pos);
}

void cbox_midi_merger_push(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt)
{
    if (!buffer->count)
        return;
    struct cbox_midi_source src;
    src.data = buffer;
    src.bpos = 0;
    src.streaming = FALSE;
    cbox_rt_array_insert(rt, (void ***)&dest->inputs, &dest->input_count, dest->input_count, &src);
    while(src.bpos < buffer->count)
        cbox_rt_handle_cmd_queue(rt); 
    cbox_rt_array_remove(rt, (void ***)&dest->inputs, &dest->input_count, dest->input_count - 1);
}

void cbox_midi_merger_close(struct cbox_midi_merger *dest)
{
    for (int i = 0; i < dest->input_count; i++)
        free(dest->inputs[i]);
    free(dest->inputs);
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_midi_appsink_init(struct cbox_midi_appsink *appsink, struct cbox_rt *rt)
{
    appsink->rt = rt;
    cbox_midi_buffer_init(&appsink->midibufs[0]);
    cbox_midi_buffer_init(&appsink->midibufs[1]);
    appsink->current_buffer = 0;
}

void cbox_midi_appsink_supply(struct cbox_midi_appsink *appsink, struct cbox_midi_buffer *buffer)
{
    struct cbox_midi_buffer *sinkbuf = &appsink->midibufs[appsink->current_buffer];
    for (int i = 0; i < buffer->count; i++)
    {
        const struct cbox_midi_event *event = cbox_midi_buffer_get_event(buffer, i);
        if (event)
        {
            if (!cbox_midi_buffer_can_store_msg(sinkbuf, event->size))
                break;
            cbox_midi_buffer_copy_event(sinkbuf, event, 0);
        }
    }
}

#define cbox_midi_appsink_get_input_midi_data__args(ARG) 

DEFINE_RT_FUNC(const struct cbox_midi_buffer *, cbox_midi_appsink, appsink, cbox_midi_appsink_get_input_midi_data_)
{
    const struct cbox_midi_buffer *ret = NULL;
    if (appsink->midibufs[appsink->current_buffer].count)
    {
        // return the current buffer, switch to the new, empty one
        ret = &appsink->midibufs[appsink->current_buffer];
        appsink->current_buffer = 1 - appsink->current_buffer;
        cbox_midi_buffer_clear(&appsink->midibufs[appsink->current_buffer]);
    }

    return ret;
}

const struct cbox_midi_buffer *cbox_midi_appsink_get_input_midi_data(struct cbox_midi_appsink *appsink)
{
    // This checks the counter from the 'wrong' thread, but that's OK, it's
    // just to avoid doing any RT work when input buffer is completely empty.
    // Any further access/manipulation is done via RT cmd.
    if (!appsink->midibufs[appsink->current_buffer].count)
        return NULL;
    return cbox_midi_appsink_get_input_midi_data_(appsink);
}

gboolean cbox_midi_appsink_send_to(struct cbox_midi_appsink *appsink, struct cbox_command_target *fb, GError **error)
{
    const struct cbox_midi_buffer *midi_in = cbox_midi_appsink_get_input_midi_data(appsink);
    // If no feedback, the input events are lost - probably better than if
    // they filled up the input buffer needlessly.
    if (fb && midi_in)
    {
        for (int i = 0; i < midi_in->count; i++)
        {
            const struct cbox_midi_event *event = cbox_midi_buffer_get_event(midi_in, i);
            const uint8_t *data = cbox_midi_event_get_data(event);
            // XXXKF doesn't handle SysEx properly yet, only 3-byte values
            if (event->size <= 3)
            {
                if (!cbox_execute_on(fb, NULL, "/io/midi/simple_event", "iii" + (3 - event->size), error, data[0], data[1], data[2]))
                    return FALSE;
            }
            else
            {
                struct cbox_blob blob;
                blob.data = (uint8_t *)data;
                blob.size = event->size;
                if (!cbox_execute_on(fb, NULL, "/io/midi/long_event", "b", error, &blob))
                    return FALSE;
            }
        }
    }
    return TRUE;
}

