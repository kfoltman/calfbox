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

#include "app.h"
#include "errors.h"
#include "procmain.h"
#include "recsrc.h"
#include "stm.h"

static gboolean cbox_recording_source_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

void cbox_recording_source_init(struct cbox_recording_source *src, uint32_t max_numsamples, int channels)
{
    src->handlers = NULL;
    src->handler_count = 0;
    src->max_numsamples = max_numsamples;
    src->channels = channels;
    cbox_command_target_init(&src->cmd_target, cbox_recording_source_process_cmd, src);
}

void cbox_recording_source_attach(struct cbox_recording_source *src, struct cbox_recorder *rec)
{
    rec->attach(rec, src);
    void **new_array = stm_array_clone_insert((void **)src->handlers, src->handler_count, 0, rec);
    cbox_rt_swap_pointers_and_update_count(app.rt, (void **)&src->handlers, new_array, &src->handler_count, src->handler_count + 1);
}

int cbox_recording_source_detach(struct cbox_recording_source *src, struct cbox_recorder *rec)
{
    int index = -1;
    for (int i = 0; i < src->handler_count; i++)
    {
        if (src->handlers[i] == rec)
        {
            index = i;
            break;
        }
    }
    if (index == -1)
        return 0;
    
    rec->detach(rec);
    void **new_array = stm_array_clone_remove((void **)src->handlers, src->handler_count, index);
    cbox_rt_swap_pointers_and_update_count(app.rt, (void **)&src->handlers, new_array, &src->handler_count, src->handler_count - 1);
    return 1;
}

void cbox_recording_source_change(struct cbox_recording_source *src, uint32_t max_numsamples, int channels)
{
    for (int i = 0; i < src->handler_count; i++)
        src->handlers[i]->detach(src->handlers[i]);
    src->max_numsamples = max_numsamples;
    src->channels = channels;
    for (int i = 0; i < src->handler_count; i++)
        src->handlers[i]->attach(src->handlers[i], src);
}

void cbox_recording_source_push(struct cbox_recording_source *src, const float **buffers, uint32_t numsamples)
{
    for (int i = 0; i < src->handler_count; i++)
        src->handlers[i]->record_block(src->handlers[i], buffers, numsamples);
}

void cbox_recording_source_uninit(struct cbox_recording_source *src)
{
    STM_ARRAY_FREE(src->handlers, src->handler_count, (src->handlers[i]->detach));
}

gboolean cbox_recording_source_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    cbox_set_command_error(error, cmd);
    return FALSE;
}
