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
#include "dom.h"
#include "engine.h"
#include "instr.h"
#include "io.h"
#include "layer.h"
#include "midi.h"
#include "mididest.h"
#include "module.h"
#include "rt.h"
#include "scene.h"
#include "seq.h"
#include "song.h"
#include "stm.h"
#include "track.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_engine)

static gboolean cbox_engine_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

struct cbox_engine *cbox_engine_new(struct cbox_document *doc, struct cbox_rt *rt)
{
    struct cbox_engine *engine = malloc(sizeof(struct cbox_engine));
    CBOX_OBJECT_HEADER_INIT(engine, cbox_engine, doc);
    
    engine->rt = rt;
    engine->scenes = NULL;
    engine->scene_count = 0;
    engine->effect = NULL;
    engine->master = cbox_master_new(engine);
    engine->master->song = cbox_song_new(doc);
    engine->spb = NULL;
    engine->spb_lock = 0;
    engine->spb_retry = 0;
    
    if (rt)
        cbox_io_env_copy(&engine->io_env, &rt->io_env);
    else
    {
        engine->io_env.srate = 0; // must be overridden
        engine->io_env.buffer_size = 256;
        engine->io_env.input_count = 0;
        engine->io_env.output_count = 2;
    }

    cbox_midi_buffer_init(&engine->midibuf_aux);
    cbox_midi_buffer_init(&engine->midibuf_jack);
    cbox_midi_buffer_init(&engine->midibuf_song);
    engine->stmap = malloc(sizeof(struct cbox_song_time_mapper));
    cbox_song_time_mapper_init(engine->stmap, engine);
    cbox_midi_appsink_init(&engine->appsink, rt, &engine->stmap->tmap);

    cbox_command_target_init(&engine->cmd_target, cbox_engine_process_cmd, engine);
    CBOX_OBJECT_REGISTER(engine);
    
    return engine;
}

struct cbox_objhdr *cbox_engine_newfunc(struct cbox_class *class_ptr, struct cbox_document *doc)
{
    return NULL;
}

void cbox_engine_destroyfunc(struct cbox_objhdr *obj_ptr)
{
    struct cbox_engine *engine = (struct cbox_engine *)obj_ptr;
    while(engine->scene_count)
        CBOX_DELETE(engine->scenes[0]);
    if (engine->master->song)
    {
        CBOX_DELETE(engine->master->song);
        engine->master->song = NULL;
    }
    cbox_master_destroy(engine->master);
    engine->master = NULL;
    free(engine->stmap);
    engine->stmap = NULL;

    free(engine);
}

static gboolean cbox_engine_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_engine *engine = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        for (uint32_t i = 0; i < engine->scene_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/scene", "o", error, engine->scenes[i]))
                return FALSE;
        }
        return CBOX_OBJECT_DEFAULT_STATUS(engine, fb, error);
    }
    else if (!strcmp(cmd->command, "/render_stereo") && !strcmp(cmd->arg_types, "i"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (engine->rt && engine->rt->io)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot use render function in real-time mode.");
            return FALSE;
        }
        struct cbox_midi_buffer midibuf_song;
        cbox_midi_buffer_init(&midibuf_song);
        int nframes = CBOX_ARG_I(cmd, 0);
        float *data = malloc(2 * nframes * sizeof(float));
        float *data_i = malloc(2 * nframes * sizeof(float));
        float *buffers[2] = { data, data + nframes };
        for (int i = 0; i < nframes; i++)
        {
            buffers[0][i] = 0.f;
            buffers[1][i] = 0.f;
        }
        cbox_engine_process(engine, NULL, nframes, buffers, 2);
        for (int i = 0; i < nframes; i++)
        {
            data_i[i * 2] = buffers[0][i];
            data_i[i * 2 + 1] = buffers[1][i];
        }
        free(data);

        if (!cbox_execute_on(fb, NULL, "/data", "b", error, cbox_blob_new_acquire_data(data_i, nframes * 2 * sizeof(float))))
            return FALSE;
        return TRUE;
    }
    else if (!strncmp(cmd->command, "/master_effect/",15))
    {
        return cbox_module_slot_process_cmd(&engine->effect, fb, cmd, cmd->command + 14, CBOX_GET_DOCUMENT(engine), engine->rt, engine, error);
    }
    else if (!strcmp(cmd->command, "/new_scene") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct cbox_scene *s = cbox_scene_new(CBOX_GET_DOCUMENT(engine), engine);

        return s ? cbox_execute_on(fb, NULL, "/uuid", "o", error, s) : FALSE;
    }
    else if (!strcmp(cmd->command, "/new_recorder") && !strcmp(cmd->arg_types, "s"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct cbox_recorder *rec = cbox_recorder_new_stream(engine, engine->rt, CBOX_ARG_S(cmd, 0));

        return rec ? cbox_execute_on(fb, NULL, "/uuid", "o", error, rec) : FALSE;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

void cbox_engine_process(struct cbox_engine *engine, struct cbox_io *io, uint32_t nframes, float **output_buffers, uint32_t output_channels)
{
    struct cbox_module *effect = engine->effect;
    uint32_t i, j;
    
    cbox_midi_buffer_clear(&engine->midibuf_aux);
    cbox_midi_buffer_clear(&engine->midibuf_song);
    if (io)
        cbox_io_get_midi_data(io, &engine->midibuf_jack);
    else
        cbox_midi_buffer_clear(&engine->midibuf_jack);
    
    // Copy MIDI input to the app-sink
    cbox_midi_appsink_supply(&engine->appsink, &engine->midibuf_jack, io ? io->free_running_frame_counter : 0);
    
    // Clear external track outputs
    if (engine->spb)
        cbox_song_playback_prepare_render(engine->spb);

    if (engine->rt)
        cbox_rt_handle_rt_commands(engine->rt);
    
    // Combine various sources of events (song, non-RT thread, JACK input)
    if (engine->spb)
    {
        engine->frame_start_song_pos = engine->spb->song_pos_samples;
        cbox_song_playback_render(engine->spb, &engine->midibuf_song, nframes);
    }
    
    for (uint32_t i = 0; i < engine->scene_count; i++)
        cbox_scene_render(engine->scenes[i], nframes, output_buffers, output_channels);
    
    // Process "master" effect
    if (effect)
    {
        for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
        {
            cbox_sample_t left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            cbox_sample_t *in_bufs[2] = {output_buffers[0] + i, output_buffers[1] + i};
            cbox_sample_t *out_bufs[2] = {left, right};
            (*effect->process_block)(effect, in_bufs, out_bufs);
            for (j = 0; j < CBOX_BLOCK_SIZE; j++)
            {
                output_buffers[0][i + j] = left[j];
                output_buffers[1][i + j] = right[j];
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_engine_add_scene(struct cbox_engine *engine, struct cbox_scene *scene)
{
    assert(scene->engine == engine);
    
    cbox_rt_array_insert(engine->rt, (void ***)&engine->scenes, &engine->scene_count, -1, scene);
}

void cbox_engine_remove_scene(struct cbox_engine *engine, struct cbox_scene *scene)
{
    assert(scene->engine == engine);
    cbox_rt_array_remove_by_value(engine->rt, (void ***)&engine->scenes, &engine->scene_count, scene);
}

////////////////////////////////////////////////////////////////////////////////////////

#define cbox_engine_set_song_playback_args(ARG) ARG(struct cbox_song_playback *, old_song) ARG(struct cbox_song_playback *, new_song) ARG(uint32_t, new_time_ppqn)

DEFINE_ASYNC_RT_FUNC(cbox_engine, engine, cbox_engine_set_song_playback)
{
    // If there's no new song, silence all ongoing notes. Otherwise, copy the
    // ongoing notes to the new playback structure so that the notes get released
    // when playback is stopped (or possibly earlier).
    if (engine->spb)
    {
        if (new_song)
            cbox_song_playback_apply_old_state(new_song);

        if (cbox_song_playback_active_notes_release(engine->spb, new_song, new_time_ppqn == (uint32_t)-1 ? old_song->song_pos_ppqn : new_time_ppqn, &engine->midibuf_aux) < 0)
        {
            RT_CALL_AGAIN_LATER();
            return;
        }
    }
    engine->spb = new_song;
    engine->master->spb = new_song;
    if (new_song)
    {
        if (new_time_ppqn == (uint32_t)-1)
        {
            int old_time_ppqn = old_song ? old_song->song_pos_ppqn : 0;
            cbox_song_playback_seek_samples(engine->master->spb, old_song ? old_song->song_pos_samples : 0);
            // If tempo change occurred anywhere before playback point, then
            // sample-based position corresponds to a completely different location.
            // So, if new sample-based position corresponds to different PPQN
            // position, seek again using PPQN position.
            if (old_song && abs(new_song->song_pos_ppqn - old_time_ppqn) > 1)
                cbox_song_playback_seek_ppqn(engine->master->spb, old_time_ppqn, FALSE);
        }
        else
            cbox_song_playback_seek_ppqn(engine->master->spb, new_time_ppqn, FALSE);
    }
}

ASYNC_PREPARE_FUNC(cbox_engine, engine, cbox_engine_set_song_playback)
{
    // If update is already in progress, reschedule another at the end of it
    if (engine->spb_lock)
    {
        engine->spb_retry = 1;
        return 1;
    }
    ++engine->spb_lock;
    args->old_song = engine->spb;
    args->new_song = cbox_song_playback_new(engine->master->song, engine->master, engine, args->old_song);

    return 0;
}

ASYNC_CLEANUP_FUNC(cbox_engine, engine, cbox_engine_set_song_playback)
{
    --engine->spb_lock;
    assert(!engine->spb_lock);
    if (args->old_song)
        cbox_song_playback_destroy(args->old_song);
    // If another update was requested while this one was in progress, repeat
    // the operation
    if (engine->spb_retry) {
        engine->spb_retry = 0;
        cbox_engine_set_song_playback(engine, NULL, NULL, args->new_time_ppqn);
    }
}

void cbox_engine_update_song(struct cbox_engine *engine, int new_pos_ppqn)
{
    cbox_engine_set_song_playback(engine, NULL, NULL, new_pos_ppqn);
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_engine_update_song_playback(struct cbox_engine *engine)
{
    cbox_engine_update_song(engine, -1);
}

////////////////////////////////////////////////////////////////////////////////////////

uint32_t cbox_engine_current_pos_samples(struct cbox_engine *engine)
{
    uint32_t pos = engine->frame_start_song_pos + engine->song_pos_offset;
    if (engine->spb && engine->spb->loop_start_ppqn < engine->spb->loop_end_ppqn)
        pos = cbox_song_playback_correct_for_looping(engine->spb, pos);
    return pos;
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_engine_update_input_connections(struct cbox_engine *engine)
{
    for (uint32_t i = 0; i < engine->scene_count; i++)
        cbox_scene_update_connected_inputs(engine->scenes[i]);
}

void cbox_engine_update_output_connections(struct cbox_engine *engine)
{
    for (uint32_t i = 0; i < engine->scene_count; i++)
        cbox_scene_update_connected_outputs(engine->scenes[i]);
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_engine_send_events_to(struct cbox_engine *engine, struct cbox_midi_merger *merger, struct cbox_midi_buffer *buffer)
{
    if (!engine || !buffer)
        return;
    if (merger)
        cbox_midi_merger_push(merger, buffer, engine->rt);
    else
    {
        for (uint32_t i = 0; i < engine->scene_count; i++)
            cbox_midi_merger_push(&engine->scenes[i]->scene_input_merger, buffer, engine->rt);
        if (!engine->rt || !engine->rt->io)
            return;
        for (GSList *p = engine->rt->io->midi_outputs; p; p = p->next)
        {
            struct cbox_midi_output *midiout = p->data;
            cbox_midi_merger_push(&midiout->merger, buffer, engine->rt);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_engine_on_tempo_sync(struct cbox_engine *engine, double beats_per_minute)
{
    if (!engine->master)
        return;
    if (beats_per_minute && beats_per_minute != engine->master->tempo && beats_per_minute != engine->master->new_tempo) {
        engine->master->new_tempo = beats_per_minute;
    }
}

////////////////////////////////////////////////////////////////////////////////////////

gboolean cbox_engine_on_transport_sync(struct cbox_engine *engine, enum cbox_transport_state state, uint32_t frame)
{
    if (state == ts_stopping)
    {
        if (engine->master->state == CMTS_ROLLING)
            engine->master->state = engine->spb ? CMTS_STOPPING : CMTS_STOP;
        
        return engine->master->state == CMTS_STOP;
    }
    if (state == ts_starting)
    {
        if (engine->master->state == CMTS_STOPPING)
            return FALSE;
        if (engine->master->state == CMTS_ROLLING)
        {
            if (engine->spb->song_pos_samples == frame)
                return TRUE;
            engine->master->state = CMTS_STOPPING;
            return FALSE;
        }
        if (engine->spb && engine->spb->song_pos_samples != frame)
        {
            cbox_song_playback_seek_samples(engine->spb, frame);
        }
        engine->frame_start_song_pos = frame;
        return TRUE;
    }
    if (state == ts_rolling)
    {
        // When starting with JACK transport rolling, there is no
        // ts_starting message in first place (because there can't be without
        // interfering with other applications). Seek immediately.
        if (engine->spb && engine->spb->song_pos_samples != frame)
        {
            cbox_song_playback_seek_samples(engine->spb, frame);
        }
        else
            engine->frame_start_song_pos = frame;
        engine->master->state = CMTS_ROLLING;
        return TRUE;
    }
    if (state == ts_stopped)
    {
        if (engine->master->state == CMTS_ROLLING)
            engine->master->state = CMTS_STOPPING;
        
        if (engine->master->state == CMTS_STOP && engine->spb && engine->spb->song_pos_samples != frame)
        {
            cbox_song_playback_seek_samples(engine->spb, frame);
        }
        
        return engine->master->state == CMTS_STOP;
    }
    return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////

struct cbox_midi_merger *cbox_engine_get_midi_output(struct cbox_engine *engine, struct cbox_uuid *uuid)
{
    struct cbox_objhdr *objhdr = cbox_document_get_object_by_uuid(CBOX_GET_DOCUMENT(engine), uuid);
    if (!objhdr)
        return NULL;
    struct cbox_scene *scene = (struct cbox_scene *)objhdr;
    if (!CBOX_IS_A(scene, cbox_scene))
        return NULL;
    return &scene->scene_input_merger;
}
