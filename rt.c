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

#include "dom.h"
#include "engine.h"
#include "io.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "stm.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_rt)

static void cbox_rt_process(void *user_data, struct cbox_io *io, uint32_t nframes);

struct cbox_rt_cmd_instance
{
    struct cbox_rt_cmd_definition *definition;
    void *user_data;
    int *completed_ptr; // for synchronous commands only
};

static gboolean cbox_rt_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_rt *rt = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (rt->io)
        {
            GError *cerror = NULL;
            if (cbox_io_get_disconnect_status(rt->io, &cerror))
            {
                return cbox_execute_on(fb, NULL, "/audio_channels", "ii", error, rt->io->io_env.input_count, rt->io->io_env.output_count) &&
                    cbox_execute_on(fb, NULL, "/state", "is", error, 1, "OK") &&
                    CBOX_OBJECT_DEFAULT_STATUS(rt, fb, error);
            }
            else
            {
                return cbox_execute_on(fb, NULL, "/audio_channels", "ii", error, rt->io->io_env.input_count, rt->io->io_env.output_count) &&
                    cbox_execute_on(fb, NULL, "/state", "is", error, -1, cerror ? cerror->message : "Unknown error") &&
                    CBOX_OBJECT_DEFAULT_STATUS(rt, fb, error);
            }
        }
        else
            return cbox_execute_on(fb, NULL, "/audio_channels", "ii", error, 0, 2) &&
                cbox_execute_on(fb, NULL, "/state", "is", error, 0, "Offline") &&
                CBOX_OBJECT_DEFAULT_STATUS(rt, fb, error);
    }
    else if (!strcmp(cmd->command, "/cycle") && !strcmp(cmd->arg_types, ""))
    {
        if (rt->io && !cbox_io_get_disconnect_status(rt->io, NULL))
        {
            return cbox_io_cycle(rt->io, fb, error);
        }
        else
        {
            if (rt->io)
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Already connected");
            else
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot cycle connection in off-line mode");
            return FALSE;
        }
    }
    else if (!strcmp(cmd->command, "/flush") && !cmd->arg_types[0]) {
        cbox_rt_handle_cmd_queue(rt);
        return TRUE;
    }
    else if (!strncmp(cmd->command, "/engine/", 8))
        return cbox_execute_sub(&rt->engine->cmd_target, fb, cmd, cmd->command + 7, error);
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

struct cbox_rt *cbox_rt_new(struct cbox_document *doc)
{
    struct cbox_rt *rt = malloc(sizeof(struct cbox_rt));
    CBOX_OBJECT_HEADER_INIT(rt, cbox_rt, doc);
    rt->rb_execute = cbox_fifo_new(sizeof(struct cbox_rt_cmd_instance) * RT_CMD_QUEUE_ITEMS);
    rt->rb_cleanup = cbox_fifo_new(sizeof(struct cbox_rt_cmd_instance) * RT_CMD_QUEUE_ITEMS * 2);
    rt->io = NULL;
    rt->engine = NULL;
    rt->started = FALSE;
    rt->disconnected = FALSE;
    rt->io_env.srate = 0;
    rt->io_env.buffer_size = 0;
    
    cbox_command_target_init(&rt->cmd_target, cbox_rt_process_cmd, rt);
    CBOX_OBJECT_REGISTER(rt);
    cbox_document_set_service(doc, "rt", &rt->_obj_hdr);
    
    return rt;
}

struct cbox_objhdr *cbox_rt_newfunc(struct cbox_class *class_ptr, struct cbox_document *doc)
{
    return NULL;
}

void cbox_rt_destroyfunc(struct cbox_objhdr *obj_ptr)
{
    struct cbox_rt *rt = (void *)obj_ptr;
    cbox_fifo_destroy(rt->rb_execute);
    cbox_fifo_destroy(rt->rb_cleanup);

    free(rt);
}

static void cbox_rt_on_disconnected(void *user_data)
{
    struct cbox_rt *rt = user_data;
    rt->disconnected = TRUE;
}

static void cbox_rt_on_reconnected(void *user_data)
{
    struct cbox_rt *rt = user_data;
    rt->disconnected = FALSE;
}

static void cbox_rt_on_midi_outputs_changed(void *user_data)
{
    struct cbox_rt *rt = user_data;
    if (rt->engine)
        cbox_engine_update_song_playback(rt->engine);
}

static void cbox_rt_on_midi_inputs_changed(void *user_data)
{
    struct cbox_rt *rt = user_data;
    if (rt->engine)
        cbox_engine_update_input_connections(rt->engine);
}

void cbox_rt_on_update_io_env(struct cbox_rt *rt)
{
    if (rt->engine)
    {
        cbox_io_env_copy(&rt->engine->io_env, &rt->io_env);
        cbox_master_set_sample_rate(rt->engine->master, rt->io_env.srate);
    }
}

void cbox_rt_set_io(struct cbox_rt *rt, struct cbox_io *io)
{
    assert(!rt->started);
    rt->io = io;
    if (io)
    {
        cbox_io_env_copy(&rt->io_env, &io->io_env);
        cbox_rt_on_update_io_env(rt);
    }
    else
    {
        cbox_io_env_clear(&rt->io_env);
    }
}

void cbox_rt_set_offline(struct cbox_rt *rt, int sample_rate, int buffer_size)
{
    assert(!rt->started);
    rt->io = NULL;
    rt->io_env.srate = sample_rate;
    rt->io_env.buffer_size = buffer_size;
    rt->io_env.input_count = 0;
    rt->io_env.output_count = 2;
    cbox_rt_on_update_io_env(rt);
}

void cbox_rt_on_started(void *user_data)
{
    struct cbox_rt *rt = user_data;
    rt->started = 1;
}

void cbox_rt_on_stopped(void *user_data)
{
    struct cbox_rt *rt = user_data;
    rt->started = 0;
}

gboolean cbox_rt_on_transport_sync(void *user_data, enum cbox_transport_state state, uint32_t frame)
{
    struct cbox_rt *rt = user_data;
    if (!rt->engine)
        return TRUE;
    return cbox_engine_on_transport_sync(rt->engine, state, frame);
}

gboolean cbox_rt_on_tempo_sync(void *user_data, double tempo)
{
    struct cbox_rt *rt = user_data;
    if (rt->engine)
        cbox_engine_on_tempo_sync(rt->engine, tempo);
    return TRUE;
}

void cbox_rt_start(struct cbox_rt *rt, struct cbox_command_target *fb)
{
    if (rt->io)
    {
        rt->cbs = calloc(1, sizeof(struct cbox_io_callbacks));
        rt->cbs->user_data = rt;
        rt->cbs->process = cbox_rt_process;
        rt->cbs->on_started = cbox_rt_on_started;
        rt->cbs->on_stopped = cbox_rt_on_stopped;
        rt->cbs->on_disconnected = cbox_rt_on_disconnected;
        rt->cbs->on_reconnected = cbox_rt_on_reconnected;
        rt->cbs->on_midi_inputs_changed = cbox_rt_on_midi_inputs_changed;
        rt->cbs->on_midi_outputs_changed = cbox_rt_on_midi_outputs_changed;
        rt->cbs->on_transport_sync = cbox_rt_on_transport_sync;
        rt->cbs->on_tempo_sync = cbox_rt_on_tempo_sync;

        assert(!rt->started);
        cbox_io_start(rt->io, rt->cbs, fb);
        assert(rt->started);
    }
}

void cbox_rt_stop(struct cbox_rt *rt)
{
    if (rt->io)
    {
        assert(rt->started);
        cbox_io_stop(rt->io);
        free(rt->cbs);
        rt->cbs = NULL;
        assert(!rt->started);
    }
}

void cbox_rt_handle_cmd_queue(struct cbox_rt *rt)
{
    struct cbox_rt_cmd_instance cmd;
    
    while(cbox_fifo_read_atomic(rt->rb_cleanup, &cmd, sizeof(cmd)))
    {
        assert(!cmd.completed_ptr);
        cmd.definition->cleanup(cmd.user_data);
    }
}

static void wait_write_space(struct cbox_fifo *rb)
{
    int t = 0;
    while (cbox_fifo_writespace(rb) < sizeof(struct cbox_rt_cmd_instance))
    {
        // wait until some space frees up in the execute queue
        usleep(1000);
        t++;
        if (t >= 1000)
        {
            fprintf(stderr, "Execute queue full, waiting...\n");
            t = 0;
        }
    }
}

void cbox_rt_execute_cmd_sync(struct cbox_rt *rt, struct cbox_rt_cmd_definition *def, void *user_data)
{
    struct cbox_rt_cmd_instance cmd;
    
    if (def->prepare)
        if (def->prepare(user_data))
            return;
        
    // No realtime thread - do it all in the main thread
    if (!rt || !rt->started || rt->disconnected)
    {
        while (!def->execute(user_data))
            ;
        if (def->cleanup)
            def->cleanup(user_data);
        return;
    }
    
    int completed = 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.definition = def;
    cmd.user_data = user_data;
    cmd.completed_ptr = &completed;
    
    wait_write_space(rt->rb_execute);
    cbox_fifo_write_atomic(rt->rb_execute, &cmd, sizeof(cmd));
    do
    {
        if (completed)
        {
            if (def->cleanup)
                def->cleanup(user_data);
            break;
        }
        struct cbox_rt_cmd_instance cmd2;
        
        if (!cbox_fifo_read_atomic(rt->rb_cleanup, &cmd2, sizeof(cmd2)))
        {
            // still no result in cleanup queue - wait
            usleep(10000);
            continue;
        }
        // async command or something from outer layer - clean it up
        cmd2.definition->cleanup(cmd2.user_data);
    } while(1);
}

int cbox_rt_execute_cmd_async(struct cbox_rt *rt, struct cbox_rt_cmd_definition *def, void *user_data)
{
    struct cbox_rt_cmd_instance cmd = { def, user_data, NULL };
    
    if (def->prepare)
    {
        int error = def->prepare(user_data);
        if (error)
            return error;
    }
    // No realtime thread - do it all in the main thread
    if (!rt || !rt->started || rt->disconnected)
    {
        while (!def->execute(user_data))
            ;
        if (def->cleanup)
            def->cleanup(user_data);
    } else {
        wait_write_space(rt->rb_execute);
        cbox_fifo_write_atomic(rt->rb_execute, &cmd, sizeof(cmd));
        // will be cleaned up by next sync call or by cbox_rt_cmd_handle_queue
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_rt_process(void *user_data, struct cbox_io *io, uint32_t nframes)
{
    struct cbox_rt *rt = user_data;
    if (rt->engine)
        cbox_engine_process(rt->engine, io, nframes, io->output_buffers, io->io_env.output_count);
    else
        cbox_rt_handle_rt_commands(rt);
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_rt_handle_rt_commands(struct cbox_rt *rt)
{
    struct cbox_rt_cmd_instance cmd;

    // Process command queue, needs engine's MIDI aux buf to be initialised to work
    int cost = 0;
    while(cost < RT_MAX_COST_PER_CALL && cbox_fifo_peek(rt->rb_execute, &cmd, sizeof(cmd)))
    {
        int result = (cmd.definition->execute)(cmd.user_data);
        if (!result)
            break;
        cost += result;
        cbox_fifo_consume(rt->rb_execute, sizeof(cmd));
        if (cmd.completed_ptr)
            *cmd.completed_ptr = 1;
        else if (cmd.definition->cleanup)
        {
            gboolean success = cbox_fifo_write_atomic(rt->rb_cleanup, (const char *)&cmd, sizeof(cmd));
            if (!success)
                g_error("Clean-up FIFO full. Main thread deadlock?");
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////

#define cbox_rt_swap_pointers_args(ARG) ARG(void **, ptr) ARG(void *, new_value)

DEFINE_RT_FUNC(void *, cbox_rt, rt, cbox_rt_swap_pointers)
{
    void *old_value = *ptr;
    *ptr = new_value;
    return old_value;
}

#define cbox_rt_swap_pointers_into_args(ARG) ARG(void **, ptr) ARG(void *, new_value) ARG(void **, old_value_p)

DEFINE_RT_VOID_FUNC(cbox_rt, rt, cbox_rt_swap_pointers_into)
{
    *old_value_p = *ptr;
    *ptr = new_value;
}

#define cbox_rt_swap_pointers_and_update_count_args(ARG) ARG(void **, ptr) ARG(void *, new_value) ARG(int *, pcount) ARG(int, new_count)

DEFINE_RT_FUNC(void *, cbox_rt, rt, cbox_rt_swap_pointers_and_update_count)
{
    void *old_value = *ptr;
    *ptr = new_value;
    if (pcount)
        *pcount = new_count;
    return old_value;
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_rt_array_insert(struct cbox_rt *rt, void ***ptr, int *pcount, int index, void *new_value)
{
    assert(index >= -1);
    assert(index <= *pcount);
    assert(*pcount >= 0);
    void **new_array = stm_array_clone_insert(*ptr, *pcount, index, new_value);
    free(cbox_rt_swap_pointers_and_update_count(rt, (void **)ptr, new_array, pcount, *pcount + 1));            
}

void *cbox_rt_array_remove(struct cbox_rt *rt, void ***ptr, int *pcount, int index)
{
    if (index == -1)
        index = *pcount - 1;
    assert(index >= 0);
    assert(index < *pcount);
    assert(*pcount > 0);
    void *p = (*ptr)[index];
    void **new_array = stm_array_clone_remove(*ptr, *pcount, index);
    free(cbox_rt_swap_pointers_and_update_count(rt, (void **)ptr, new_array, pcount, *pcount - 1));            
    return p;
}

gboolean cbox_rt_array_remove_by_value(struct cbox_rt *rt, void ***ptr, int *pcount, void *value_to_remove)
{
    for (int i = 0; i < *pcount; i++)
    {
        if ((*ptr)[i] == value_to_remove)
        {
            cbox_rt_array_remove(rt, ptr, pcount, i);
            return TRUE;
        }
    }
    return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////

struct cbox_midi_merger *cbox_rt_get_midi_output(struct cbox_rt *rt, struct cbox_uuid *uuid)
{
    if (rt->engine)
    {
        struct cbox_midi_merger *merger = cbox_engine_get_midi_output(rt->engine, uuid);
        if (merger)
            return merger;
    }
    if (!rt->io)
        return NULL;
    
    struct cbox_midi_output *midiout = cbox_io_get_midi_output(rt->io, NULL, uuid);    
    return midiout ? &midiout->merger : NULL;
}

////////////////////////////////////////////////////////////////////////////////////////

