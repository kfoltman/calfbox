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
    for (struct cbox_midi_source *p = dest->inputs; p; p = p->next)
    {
        if (p->streaming)
            p->bpos = 0;
    }

    struct cbox_midi_source *first = dest->inputs;
    struct cbox_midi_source *first_not = NULL;
    while(first)
    {
        struct cbox_midi_source *earliest_source = NULL;
        uint32_t earliest_time = (uint32_t)-1;
        
        for (struct cbox_midi_source *p = first; p != first_not; p = p->next)
        {
            struct cbox_midi_buffer *data = p->data;
            if (p->bpos < data->count)
            {
                const struct cbox_midi_event *event = cbox_midi_buffer_get_event(data, p->bpos);
                if (event->time < earliest_time)
                {
                    earliest_source = p;
                    earliest_time = event->time;
                }
            }
            else
            {
                // Narrow down the range from top and bottom
                if (p == first)
                    first = p->next;
                if (p->next == first_not)
                {
                    first_not = p;
                    break;
                }
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

struct cbox_midi_source **cbox_midi_merger_find_source(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer)
{
    for (struct cbox_midi_source **pp = &dest->inputs; *pp; pp = &((*pp)->next))
        if ((*pp)->data == buffer)
            return pp;
    return NULL;
}

void cbox_midi_merger_connect(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt)
{
    if (cbox_midi_merger_find_source(dest, buffer) != NULL)
        return;
    
    struct cbox_midi_source *src = calloc(1, sizeof(struct cbox_midi_source));
    src->data = buffer;
    src->bpos = 0;
    src->streaming = TRUE;
    src->next = dest->inputs;
    cbox_rt_swap_pointers(rt, (void **)&dest->inputs, src);
}

void cbox_midi_merger_disconnect(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt)
{
    struct cbox_midi_source **pp = cbox_midi_merger_find_source(dest, buffer);
    if (!pp)
        return;

    struct cbox_midi_source *ms = *pp;
    cbox_rt_swap_pointers(rt, (void **)pp, ms->next);
    free(ms);
}

void cbox_midi_merger_push(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt)
{
    if (!buffer->count)
        return;
    struct cbox_midi_source src;
    src.data = buffer;
    src.bpos = 0;
    src.streaming = FALSE;
    src.next = dest->inputs;
    cbox_rt_swap_pointers(rt, (void **)&dest->inputs, &src);
    while(src.bpos < buffer->count)
        cbox_rt_handle_cmd_queue(rt); 
    cbox_rt_swap_pointers(rt, (void **)&dest->inputs, src.next);
}

void cbox_midi_merger_close(struct cbox_midi_merger *dest)
{
    struct cbox_midi_source *p;
    while(dest->inputs)
    {
        p = dest->inputs;
        dest->inputs = p->next;
        free(p);
    }
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

