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
#include "recsrc.h"
#include "rt.h"
#include "stm.h"

CBOX_CLASS_DEFINITION_ROOT(cbox_recorder)

static gboolean cbox_recording_source_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

void cbox_recording_source_init(struct cbox_recording_source *src, struct cbox_document *doc, uint32_t max_numsamples, int channels)
{
    src->doc = doc;
    src->handlers = NULL;
    src->handler_count = 0;
    src->max_numsamples = max_numsamples;
    src->channels = channels;
    cbox_command_target_init(&src->cmd_target, cbox_recording_source_process_cmd, src);
}

void cbox_recording_source_attach(struct cbox_recording_source *src, struct cbox_recorder *rec)
{
    rec->attach(rec, src);
    cbox_rt_array_insert(app.rt, (void ***)&src->handlers, &src->handler_count, 0, rec);
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
    cbox_rt_array_remove(app.rt, (void ***)&src->handlers, &src->handler_count, index);
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
    STM_ARRAY_FREE_OBJS(src->handlers, src->handler_count);
    src->handler_count = 0;
}

gboolean cbox_recording_source_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_recording_source *src = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        for (int i = 0; i < src->handler_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/handler", "io", error, i + 1, src->handlers[i]))
                return FALSE;            
        }
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/attach") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_objhdr *objhdr = CBOX_ARG_O(cmd, 0, error);
        if (!objhdr)
            return FALSE;
        struct cbox_recorder *rec = CBOX_H2O(objhdr);
        cbox_recording_source_attach(src, rec);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/detach") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_objhdr *objhdr = CBOX_ARG_O(cmd, 0, error);
        if (!objhdr)
            return FALSE;
        struct cbox_recorder *rec = CBOX_H2O(objhdr);
        cbox_recording_source_detach(src, rec);
        return TRUE;
    }
    else    
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

void cbox_recorder_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_recorder *recorder = CBOX_H2O(objhdr);
    recorder->destroy(recorder);
}