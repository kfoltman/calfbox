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

struct cbox_engine *cbox_engine_new(struct cbox_rt *rt)
{
    struct cbox_engine *engine = malloc(sizeof(struct cbox_engine));
    CBOX_OBJECT_HEADER_INIT(engine, cbox_engine, CBOX_GET_DOCUMENT(rt));
    
    engine->rt = rt;
    engine->scene = NULL;
    engine->effect = NULL;
    engine->master = cbox_master_new(engine);
    engine->master->song = cbox_song_new(CBOX_GET_DOCUMENT(rt));
    cbox_midi_buffer_init(&engine->midibuf_aux);
    cbox_midi_buffer_init(&engine->midibuf_jack);
    cbox_midi_buffer_init(&engine->midibuf_song);

    cbox_midi_buffer_init(&engine->midibufs_appsink[0]);
    cbox_midi_buffer_init(&engine->midibufs_appsink[1]);
    engine->current_appsink_buffer = 0;

    cbox_command_target_init(&engine->cmd_target, cbox_engine_process_cmd, engine);
    CBOX_OBJECT_REGISTER(engine);
    cbox_document_set_service(CBOX_GET_DOCUMENT(rt), "engine", &engine->_obj_hdr);
    
    return engine;
}

struct cbox_objhdr *cbox_engine_newfunc(struct cbox_class *class_ptr, struct cbox_document *doc)
{
    return NULL;
}

void cbox_engine_destroyfunc(struct cbox_objhdr *obj_ptr)
{
    struct cbox_engine *engine = (struct cbox_engine *)obj_ptr;
    if (engine->master->song)
    {
        CBOX_DELETE(engine->master->song);
        engine->master->song = NULL;
    }
    cbox_master_destroy(engine->master);
    engine->master = NULL;

    free(engine);
}

static gboolean cbox_engine_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    // struct cbox_engine *engine = ct->user_data;
    return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

void cbox_engine_process(struct cbox_engine *engine, struct cbox_io *io, uint32_t nframes)
{
    struct cbox_module *effect = engine->effect;
    uint32_t i, j;
    
    cbox_midi_buffer_clear(&engine->midibuf_aux);
    cbox_midi_buffer_clear(&engine->midibuf_song);
    cbox_io_get_midi_data(io, &engine->midibuf_jack);
    
    // Copy MIDI input to the app-sink with no timing information
    struct cbox_midi_buffer *appsink = &engine->midibufs_appsink[engine->current_appsink_buffer];
    for (int i = 0; i < engine->midibuf_jack.count; i++)
    {
        const struct cbox_midi_event *event = cbox_midi_buffer_get_event(&engine->midibuf_jack, i);
        if (event)
        {
            if (!cbox_midi_buffer_can_store_msg(appsink, event->size))
                break;
            cbox_midi_buffer_copy_event(appsink, event, 0);
        }
    }
    
    cbox_rt_handle_rt_commands(engine->rt);
    
    // Combine various sources of events (song, non-RT thread, JACK input)
    if (engine->scene && engine->scene->spb)
        cbox_song_playback_render(engine->scene->spb, &engine->midibuf_song, nframes);

    if (engine->scene)
        cbox_scene_render(engine->scene, nframes, io->output_buffers);
    
    // Process "master" effect
    if (effect)
    {
        for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
        {
            cbox_sample_t left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            cbox_sample_t *in_bufs[2] = {io->output_buffers[0] + i, io->output_buffers[1] + i};
            cbox_sample_t *out_bufs[2] = {left, right};
            (*effect->process_block)(effect, in_bufs, out_bufs);
            for (j = 0; j < CBOX_BLOCK_SIZE; j++)
            {
                io->output_buffers[0][i + j] = left[j];
                io->output_buffers[1][i + j] = right[j];
            }
        }
    }

}

////////////////////////////////////////////////////////////////////////////////////////

struct cbox_scene *cbox_engine_set_scene(struct cbox_engine *engine, struct cbox_scene *scene)
{
    if (scene == engine->scene)
        return scene;

    if (scene)
    {
        scene->spb = engine->master->spb;
        cbox_midi_merger_connect(&scene->scene_input_merger, &engine->midibuf_aux, engine->rt);
        cbox_midi_merger_connect(&scene->scene_input_merger, &engine->midibuf_jack, engine->rt);
        cbox_midi_merger_connect(&scene->scene_input_merger, &engine->midibuf_song, engine->rt);
    }
    if (engine->scene)
    {
        cbox_midi_merger_disconnect(&engine->scene->scene_input_merger, &engine->midibuf_aux, engine->rt);
        cbox_midi_merger_disconnect(&engine->scene->scene_input_merger, &engine->midibuf_jack, engine->rt);
        cbox_midi_merger_disconnect(&engine->scene->scene_input_merger, &engine->midibuf_song, engine->rt);
    }
    
    return cbox_rt_swap_pointers(engine->rt, (void **)&engine->scene, scene);
}

////////////////////////////////////////////////////////////////////////////////////////

#define cbox_engine_set_song_playback_args(ARG) ARG(struct cbox_song_playback *, new_song) ARG(int, new_time_ppqn)

DEFINE_RT_VOID_FUNC(cbox_engine, engine, cbox_engine_set_song_playback)
{
    // If there's no new song, silence all ongoing notes. Otherwise, copy the
    // ongoing notes to the new playback structure so that the notes get released
    // when playback is stopped (or possibly earlier).
    if (engine->scene && engine->scene->spb)
    {
        if (new_song)
            cbox_song_playback_apply_old_state(new_song);

        if (cbox_song_playback_active_notes_release(engine->scene->spb, &engine->midibuf_aux) < 0)
        {
            RT_CALL_AGAIN_LATER();
            return;
        }
    }
    struct cbox_song_playback *old_song = engine->master->spb;
    if (engine->scene)
        engine->scene->spb = new_song;
    engine->master->spb = new_song;
    if (new_song)
    {
        if (new_time_ppqn == -1)
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

void cbox_engine_update_song(struct cbox_engine *engine, int new_pos_ppqn)
{
    struct cbox_song_playback *old_song, *new_song;
    old_song = engine->scene ? engine->scene->spb : NULL;
    new_song = cbox_song_playback_new(engine->master->song, engine->master, engine, old_song );
    cbox_engine_set_song_playback(engine, new_song, new_pos_ppqn);
    if (old_song)
        cbox_song_playback_destroy(old_song);
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_engine_update_song_playback(struct cbox_engine *engine)
{
    cbox_engine_update_song(engine, -1);
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
        if (engine->scene)
            cbox_midi_merger_push(&engine->scene->scene_input_merger, buffer, engine->rt);
        for (GSList *p = engine->rt->io->midi_outputs; p; p = p->next)
        {
            struct cbox_midi_output *midiout = p->data;
            cbox_midi_merger_push(&midiout->merger, buffer, engine->rt);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////

#define cbox_engine_get_input_midi_data__args(ARG) 

DEFINE_RT_FUNC(const struct cbox_midi_buffer *, cbox_engine, engine, cbox_engine_get_input_midi_data_)
{
    const struct cbox_midi_buffer *ret = NULL;
    if (engine->midibufs_appsink[engine->current_appsink_buffer].count)
    {
        // return the current buffer, switch to the new, empty one
        ret = &engine->midibufs_appsink[engine->current_appsink_buffer];
        engine->current_appsink_buffer = 1 - engine->current_appsink_buffer;
        cbox_midi_buffer_clear(&engine->midibufs_appsink[engine->current_appsink_buffer]);
    }

    return ret;
}

const struct cbox_midi_buffer *cbox_engine_get_input_midi_data(struct cbox_engine *engine)
{
    // This checks the counter from the 'wrong' thread, but that's OK, it's
    // just to avoid doing any RT work when input buffer is completely empty.
    // Any further access/manipulation is done via RT cmd.
    if (!engine->midibufs_appsink[engine->current_appsink_buffer].count)
        return NULL;
    return cbox_engine_get_input_midi_data_(engine);
}
