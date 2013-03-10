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
        dest->inputs[i]->bpos = 0;

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
                struct cbox_midi_event *event = cbox_midi_buffer_get_event(data, src->bpos);
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
    void **new_array = stm_array_clone_insert((void **)dest->inputs, dest->input_count, dest->input_count, src);
    void **old_array = cbox_rt_swap_pointers_and_update_count(rt, (void **)&dest->inputs, new_array, &dest->input_count, dest->input_count + 1);
    free(old_array);
}

void cbox_midi_merger_disconnect(struct cbox_midi_merger *dest, struct cbox_midi_buffer *buffer, struct cbox_rt *rt)
{
    int pos = cbox_midi_merger_find_source(dest, buffer);
    if (pos == -1)
        return;

    struct cbox_midi_source *src = dest->inputs[pos];
    void **new_array = stm_array_clone_remove((void **)dest->inputs, dest->input_count, pos);
    void **old_array = cbox_rt_swap_pointers_and_update_count(rt, (void **)&dest->inputs, new_array, &dest->input_count, dest->input_count - 1);
    free(old_array);
    free(src);
}

void cbox_midi_merger_close(struct cbox_midi_merger *dest)
{
    for (int i = 0; i < dest->input_count; i++)
        free(dest->inputs[i]);
    free(dest->inputs);
}
