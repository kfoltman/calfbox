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
#include "scene.h"
#include "stm.h"

CBOX_CLASS_DEFINITION_ROOT(cbox_recorder)

static gboolean cbox_recording_source_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

void cbox_recording_source_init(struct cbox_recording_source *src, struct cbox_scene *scene, uint32_t max_numsamples, int channels)
{
    src->scene = scene;
    src->handlers = NULL;
    src->handler_count = 0;
    src->max_numsamples = max_numsamples;
    src->channels = channels;
    cbox_command_target_init(&src->cmd_target, cbox_recording_source_process_cmd, src);
}

gboolean cbox_recording_source_attach(struct cbox_recording_source *src, struct cbox_recorder *rec, GError **error)
{
    if (!rec->attach(rec, src, error))
        return FALSE;
    cbox_rt_array_insert(app.rt, (void ***)&src->handlers, &src->handler_count, 0, rec);
    return TRUE;
}

int cbox_recording_source_detach(struct cbox_recording_source *src, struct cbox_recorder *rec, GError **error)
{
    int index = -1;
    for (uint32_t i = 0; i < src->handler_count; i++)
    {
        if (src->handlers[i] == rec)
        {
            index = i;
            break;
        }
    }
    if (index == -1)
    {
        if (error)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Recorder is not attached to this source");
        return 0;
    }
    
    cbox_rt_array_remove(app.rt, (void ***)&src->handlers, &src->handler_count, index);
    // XXXKF: when converting to async API, the array_remove must be done synchronously or
    // detach needs to be called in the cleanup part of the remove command, otherwise detach
    // may be called on 'live' recorder, which may cause unpredictable results.
    return rec->detach(rec, error);
}

void cbox_recording_source_push(struct cbox_recording_source *src, const float **buffers, uint32_t numsamples)
{
    for (uint32_t i = 0; i < src->handler_count; i++)
        src->handlers[i]->record_block(src->handlers[i], buffers, numsamples);
}

void cbox_recording_source_uninit(struct cbox_recording_source *src)
{
    STM_ARRAY_FREE_OBJS(src->handlers, src->handler_count);
    src->handlers = NULL;
    src->handler_count = 0;
}

gboolean cbox_recording_source_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_recording_source *src = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        for (uint32_t i = 0; i < src->handler_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/handler", "o", error, src->handlers[i]))
                return FALSE;            
        }
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/attach") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_objhdr *objhdr = CBOX_ARG_O(cmd, 0, src->scene, cbox_recorder, error);
        if (!objhdr)
            return FALSE;
        struct cbox_recorder *rec = CBOX_H2O(objhdr);
        return cbox_recording_source_attach(src, rec, error);
    }
    else
    if (!strcmp(cmd->command, "/detach") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_objhdr *objhdr = CBOX_ARG_O(cmd, 0, src->scene, cbox_recorder, error);
        if (!objhdr)
            return FALSE;
        struct cbox_recorder *rec = CBOX_H2O(objhdr);
        return cbox_recording_source_detach(src, rec, error);
    }
    else    
        return cbox_set_command_error(error, cmd);
}

void cbox_recorder_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_recorder *recorder = CBOX_H2O(objhdr);
    recorder->destroy(recorder);
}