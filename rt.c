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

#include "auxbus.h"
#include "instr.h"
#include "io.h"
#include "layer.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "scene.h"
#include "seq.h"
#include "song.h"
#include "track.h"
#include <assert.h>
#include <jack/jack.h>
#include <jack/types.h>
#include <jack/midiport.h>
#include <stdio.h>
#include <unistd.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_rt)

struct cbox_rt_cmd_instance
{
    struct cbox_rt_cmd_definition *definition;
    void *user_data;
    int is_async;
};

static gboolean cbox_rt_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_rt *rt = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/audio_channels", "ii", error, rt->io->input_count, rt->io->output_count);
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }    
}

struct cbox_rt *cbox_rt_new(struct cbox_document *doc)
{
    struct cbox_rt *rt = malloc(sizeof(struct cbox_rt));
    CBOX_OBJECT_HEADER_INIT(rt, cbox_rt, doc);
    rt->scene = NULL;
    rt->effect = NULL;
    rt->rb_execute = jack_ringbuffer_create(sizeof(struct cbox_rt_cmd_instance) * RT_CMD_QUEUE_ITEMS);
    rt->rb_cleanup = jack_ringbuffer_create(sizeof(struct cbox_rt_cmd_instance) * RT_CMD_QUEUE_ITEMS * 2);
    rt->io = NULL;
    rt->master = cbox_master_new(rt);
    rt->master->song = cbox_song_new(rt->master);
    rt->started = 0;
    rt->instruments = cbox_instruments_new(rt);
    rt->srate = 0;
    rt->buffer_size = 0;
    cbox_command_target_init(&rt->cmd_target, cbox_rt_process_cmd, rt);
    CBOX_SET_AS_SINGLETON(rt);
    
    return rt;
}

struct cbox_objhdr *cbox_rt_newfunc(struct cbox_class *class_ptr, struct cbox_document *doc)
{
    return NULL;
}

void cbox_rt_destroyfunc(struct cbox_objhdr *obj_ptr)
{
}

int convert_midi_from_jack(jack_port_t *port, uint32_t nframes, struct cbox_midi_buffer *destination)
{
    void *midi = jack_port_get_buffer(port, nframes);
    uint32_t event_count = jack_midi_get_event_count(midi);

    cbox_midi_buffer_clear(destination);
    for (uint32_t i = 0; i < event_count; i++)
    {
        jack_midi_event_t event;
        
        if (!jack_midi_event_get(&event, midi, i))
        {
            // XXXKF ignore sysex for now
            if (event.size >= 4)
                continue;
            
            uint8_t data[4];
            memcpy(data, event.buffer, event.size);
            if (!cbox_midi_buffer_write_event(destination, event.time, data, event.size))
                return -i;
        }
        else
            return -i;
    }
    
    return event_count;
}

int write_events_to_instrument_ports(struct cbox_midi_buffer *source, struct cbox_scene *scene)
{
    uint32_t i;
    uint32_t event_count = cbox_midi_buffer_get_count(source);

    for (i = 0; i < scene->instrument_count; i++)
    {
        cbox_midi_buffer_clear(&scene->instruments[i]->module->midi_input);
    }
    for (i = 0; i < event_count; i++)
    {
        struct cbox_midi_event *event = cbox_midi_buffer_get_event(source, i);
        
        // XXXKF ignore sysex for now
        if (event->size >= 4)
            continue;
        
        for (int l = 0; l < scene->layer_count; l++)
        {
            struct cbox_layer *lp = scene->layers[l];
            if (!lp->enabled)
                continue;
            uint8_t data[4] = {0, 0, 0, 0};
            memcpy(data, event->data_inline, event->size);
            if (data[0] < 0xF0) // per-channel messages
            {
                int cmd = data[0] >> 4;
                // filter on MIDI channel
                if (lp->in_channel >= 0 && lp->in_channel != (data[0] & 0x0F))
                    continue;
                // force output channel
                if (lp->out_channel >= 0)
                    data[0] = (data[0] & 0xF0) + (lp->out_channel & 0x0F);
                if (cmd >= 8 && cmd <= 10)
                {
                    if (cmd == 10 && lp->disable_aftertouch)
                        continue;
                    // note filter
                    if (data[1] < lp->low_note || data[1] > lp->high_note)
                        continue;
                    // transpose
                    int transpose = lp->transpose + (lp->ignore_scene_transpose ? 0 : scene->transpose);
                    if (transpose)
                    {
                        int note = data[1] + transpose;
                        if (note < 0 || note > 127)
                            continue;
                        data[1] = (uint8_t)note;
                    }
                    // fixed note
                    if (lp->fixed_note != -1)
                    {
                        data[1] = (uint8_t)lp->fixed_note;
                    }
                }
                else if (cmd == 11 && data[1] == 64 && lp->invert_sustain)
                {
                    data[2] = 127 - data[2];
                }
                else if (cmd == 13 && lp->disable_aftertouch)
                    continue;
            }
            if (!cbox_midi_buffer_write_event(&lp->instrument->module->midi_input, event->time, data, event->size))
                return -i;
            if (lp->consume)
                break;
        }
    }
    
    return event_count;
}

static void cbox_rt_process(void *user_data, struct cbox_io *io, uint32_t nframes)
{
    struct cbox_rt *rt = user_data;
    struct cbox_scene *scene = NULL;
    struct cbox_module *effect = rt->effect;
    struct cbox_rt_cmd_instance cmd;
    int cost;
    uint32_t i, j, n;
    
    cbox_midi_buffer_init(&rt->midibuf_aux);
    
    // Process command queue
    cost = 0;
    while(cost < RT_MAX_COST_PER_CALL && jack_ringbuffer_peek(rt->rb_execute, (char *)&cmd, sizeof(cmd)))
    {
        int result = (cmd.definition->execute)(cmd.user_data);
        if (!result)
            break;
        cost += result;
        jack_ringbuffer_read_advance(rt->rb_execute, sizeof(cmd));
        if (cmd.definition->cleanup || !cmd.is_async)
        {
            jack_ringbuffer_write(rt->rb_cleanup, (const char *)&cmd, sizeof(cmd));
        }
    }
    scene = rt->scene;
        
    for (i = 0; i < io->input_count; i++)
        io->input_buffers[i] = jack_port_get_buffer(io->inputs[i], nframes);
    
    for (i = 0; i < io->input_count; i++)
    {
        if (IS_RECORDING_SOURCE_CONNECTED(io->rec_mono_inputs[i]))
            cbox_recording_source_push(&io->rec_mono_inputs[i], (const float **)&io->input_buffers[i], nframes);
    }
    for (i = 0; i < io->input_count / 2; i++)
    {
        if (IS_RECORDING_SOURCE_CONNECTED(io->rec_stereo_inputs[i]))
        {
            const float *buf[2] = { io->input_buffers[i * 2], io->input_buffers[i * 2 + 1] };
            cbox_recording_source_push(&io->rec_stereo_inputs[i], buf, nframes);
        }
    }
    
    for (i = 0; i < io->output_count; i++)
        io->output_buffers[i] = jack_port_get_buffer(io->outputs[i], nframes);

    if (scene)
    {
        struct cbox_midi_buffer midibuf_jack, midibuf_pattern, midibuf_total, *midibufsrcs[3];
        cbox_midi_buffer_init(&midibuf_total);
        
        int pos[3] = {0, 0, 0};
        cbox_midi_buffer_init(&midibuf_jack);
        cbox_midi_buffer_init(&midibuf_pattern);
        convert_midi_from_jack(io->midi, nframes, &midibuf_jack);
        if (rt->master->spb)
            cbox_song_playback_render(rt->master->spb, &midibuf_pattern, nframes);
        midibufsrcs[0] = &midibuf_jack;
        midibufsrcs[1] = &midibuf_pattern;
        midibufsrcs[2] = &rt->midibuf_aux;
        cbox_midi_buffer_merge(&midibuf_total, midibufsrcs, 3, pos);
        
        write_events_to_instrument_ports(&midibuf_total, scene);
    }
    
    for (n = 0; n < io->output_count; n++)
        for (i = 0; i < nframes; i ++)
            io->output_buffers[n][i] = 0.f;
    if (scene)
    {
        for (n = 0; n < scene->aux_bus_count; n++)
        {
            for (i = 0; i < nframes; i ++)
            {
                scene->aux_buses[n]->input_bufs[0][i] = 0.f;
                scene->aux_buses[n]->input_bufs[1][i] = 0.f;
            }
        }
    }
    
    for (n = 0; scene && n < scene->instrument_count; n++)
    {
        struct cbox_instrument *instr = scene->instruments[n];
        struct cbox_module *module = instr->module;
        int event_count = instr->module->midi_input.count;
        int cur_event = 0;
        uint32_t highwatermark = 0;
        cbox_sample_t channels[CBOX_MAX_AUDIO_PORTS][CBOX_BLOCK_SIZE];
        cbox_sample_t *outputs[CBOX_MAX_AUDIO_PORTS];
        for (i = 0; i < module->outputs; i++)
            outputs[i] = channels[i];
        
        for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
        {            
            if (i >= highwatermark)
            {
                while(cur_event < event_count)
                {
                    struct cbox_midi_event *event = cbox_midi_buffer_get_event(&module->midi_input, cur_event);
                    if (event)
                    {
                        if (event->time <= i)
                            (*module->process_event)(module, cbox_midi_event_get_data(event), event->size);
                        else
                        {
                            highwatermark = event->time;
                            break;
                        }
                    }
                    else
                        break;
                    
                    cur_event++;
                }
            }
            (*module->process_block)(module, NULL, outputs);
            for (int o = 0; o < module->outputs / 2; o++)
            {
                struct cbox_instrument_output *oobj = &instr->outputs[o];
                struct cbox_module *insert = oobj->insert;
                float gain = oobj->gain;
                if (IS_RECORDING_SOURCE_CONNECTED(oobj->rec_dry))
                    cbox_recording_source_push(&oobj->rec_dry, (const float **)(outputs + 2 * o), CBOX_BLOCK_SIZE);
                if (insert && !insert->bypass)
                    (*insert->process_block)(insert, outputs + 2 * o, outputs + 2 * o);
                if (IS_RECORDING_SOURCE_CONNECTED(oobj->rec_wet))
                    cbox_recording_source_push(&oobj->rec_wet, (const float **)(outputs + 2 * o), CBOX_BLOCK_SIZE);
                float *leftbuf, *rightbuf;
                if (o < module->aux_offset / 2)
                {
                    int leftch = oobj->output_bus * 2;
                    int rightch = leftch + 1;
                    leftbuf = io->output_buffers[leftch];
                    rightbuf = io->output_buffers[rightch];
                }
                else
                {
                    int bus = o - module->aux_offset / 2;
                    struct cbox_aux_bus *busobj = instr->aux_outputs[bus];
                    if (busobj == NULL)
                        continue;
                    leftbuf = busobj->input_bufs[0];
                    rightbuf = busobj->input_bufs[1];
                }
                for (j = 0; j < CBOX_BLOCK_SIZE; j++)
                {
                    leftbuf[i + j] += gain * channels[2 * o][j];
                    rightbuf[i + j] += gain * channels[2 * o + 1][j];
                }
            }
        }
        while(cur_event < event_count)
        {
            struct cbox_midi_event *event = cbox_midi_buffer_get_event(&module->midi_input, cur_event);
            if (event)
            {
                (*module->process_event)(module, cbox_midi_event_get_data(event), event->size);
            }
            else
                break;
            
            cur_event++;
        }
    }
    
    if (scene)
    {
        for (n = 0; n < scene->aux_bus_count; n++)
        {
            struct cbox_aux_bus *bus = scene->aux_buses[n];
            float left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            float *outputs[2] = {left, right};
            for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
            {
                float *inputs[2];
                inputs[0] = &bus->input_bufs[0][i];
                inputs[1] = &bus->input_bufs[1][i];
                bus->module->process_block(bus->module, inputs, outputs);
                for (int j = 0; j < CBOX_BLOCK_SIZE; j++)
                {
                    io->output_buffers[0][i + j] += left[j];
                    io->output_buffers[1][i + j] += right[j];
                }
            }
        }
    }

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

    for (i = 0; i < io->output_count; i++)
    {
        if (IS_RECORDING_SOURCE_CONNECTED(io->rec_mono_outputs[i]))
            cbox_recording_source_push(&io->rec_mono_outputs[i], (const float **)&io->output_buffers[i], nframes);
    }
    for (i = 0; i < io->output_count / 2; i++)
    {
        if (IS_RECORDING_SOURCE_CONNECTED(io->rec_stereo_outputs[i]))
        {
            const float *buf[2] = { io->output_buffers[i * 2], io->output_buffers[i * 2 + 1] };
            cbox_recording_source_push(&io->rec_stereo_outputs[i], buf, nframes);
        }
    }
}

void cbox_rt_set_io(struct cbox_rt *rt, struct cbox_io *io)
{
    assert(!rt->started);
    rt->io = io;
    if (io)
    {
        rt->srate = jack_get_sample_rate(io->client);
        rt->buffer_size = io->buffer_size;
        cbox_master_set_sample_rate(rt->master, rt->srate);
    }
    else
    {
        rt->srate = 0;
        rt->buffer_size = 0;
    }
}

void cbox_rt_set_offline(struct cbox_rt *rt, int sample_rate, int buffer_size)
{
    assert(!rt->started);
    rt->io = NULL;
    rt->srate = sample_rate;
    rt->buffer_size = buffer_size;
    cbox_master_set_sample_rate(rt->master, rt->srate);
}

void cbox_rt_start(struct cbox_rt *rt)
{
    if (rt->io)
    {
        rt->started = 1;
        rt->cbs = malloc(sizeof(struct cbox_io_callbacks));
        rt->cbs->user_data = rt;
        rt->cbs->process = cbox_rt_process;

        cbox_io_start(rt->io, rt->cbs);
    }
}

void cbox_rt_stop(struct cbox_rt *rt)
{
    if (rt->io)
    {
        cbox_io_stop(rt->io);
        free(rt->cbs);
        rt->cbs = NULL;
        rt->started = 0;
    }
}

void cbox_rt_handle_cmd_queue(struct cbox_rt *rt)
{
    struct cbox_rt_cmd_instance cmd;
    
    while(jack_ringbuffer_read(rt->rb_cleanup, (char *)&cmd, sizeof(cmd)))
    {
        cmd.definition->cleanup(cmd.user_data);
    }
}

static void wait_write_space(jack_ringbuffer_t *rb)
{
    int t = 0;
    while (jack_ringbuffer_write_space(rb) < sizeof(struct cbox_rt_cmd_instance))
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
    if (!rt->started)
    {
        def->execute(user_data);
        if (def->cleanup)
            def->cleanup(user_data);
        return;
    }
    
    cmd.definition = def;
    cmd.user_data = user_data;
    cmd.is_async = 0;
    
    wait_write_space(rt->rb_execute);
    jack_ringbuffer_write(rt->rb_execute, (const char *)&cmd, sizeof(cmd));
    do
    {
        struct cbox_rt_cmd_instance cmd2;
        
        if (jack_ringbuffer_read_space(rt->rb_cleanup) < sizeof(cmd2))
        {
            // still no result in cleanup queue - wait
            usleep(10000);
            continue;
        }
        jack_ringbuffer_read(rt->rb_cleanup, (char *)&cmd2, sizeof(cmd2));
        if (!memcmp(&cmd, &cmd2, sizeof(cmd)))
        {
            if (def->cleanup)
                def->cleanup(user_data);
            break;
        }
        // async command - clean it up
        if (cmd2.definition->cleanup)
            cmd2.definition->cleanup(cmd2.user_data);
    } while(1);
}

void cbox_rt_execute_cmd_async(struct cbox_rt *rt, struct cbox_rt_cmd_definition *def, void *user_data)
{
    struct cbox_rt_cmd_instance cmd = { def, user_data, 1 };
    
    if (def->prepare)
    {
        if (def->prepare(user_data))
            return;
    }
    // No realtime thread - do it all in the main thread
    if (!rt->started)
    {
        def->execute(user_data);
        if (def->cleanup)
            def->cleanup(user_data);
        return;
    }
    
    wait_write_space(rt->rb_execute);
    jack_ringbuffer_write(rt->rb_execute, (const char *)&cmd, sizeof(cmd));
    
    // will be cleaned up by next sync call or by cbox_rt_cmd_handle_queue
}

////////////////////////////////////////////////////////////////////////////////////////

struct set_song_command
{
    struct cbox_rt *rt;
    struct cbox_song_playback *new_song, *old_song;
    int new_time_ppqn;
};

static int set_song_command_execute(void *user_data)
{
    struct set_song_command *cmd = user_data;
    
    if (cmd->rt->master->spb &&
        cbox_song_playback_active_notes_release(cmd->rt->master->spb, &cmd->rt->midibuf_aux) < 0)
        return 0;
    cmd->old_song = cmd->rt->master->spb;
    cmd->rt->master->spb = cmd->new_song;
    if (cmd->new_song)
    {
        if (cmd->new_time_ppqn == -1)
            cbox_song_playback_seek_samples(cmd->rt->master->spb, cmd->old_song ? cmd->old_song->song_pos_samples : 0);
        else
            cbox_song_playback_seek_ppqn(cmd->rt->master->spb, cmd->new_time_ppqn, FALSE);
    }
    
    return 1;
}

void cbox_rt_update_song(struct cbox_rt *rt, int new_pos_ppqn)
{
    static struct cbox_rt_cmd_definition def = { .prepare = NULL, .execute = set_song_command_execute, .cleanup = NULL };
    
    struct set_song_command cmd = { rt, cbox_song_playback_new(rt->master->song), NULL, new_pos_ppqn };
    cbox_rt_execute_cmd_sync(rt, &def, &cmd);
    if (cmd.old_song)
        cbox_song_playback_destroy(cmd.old_song);
}

////////////////////////////////////////////////////////////////////////////////////////

struct cbox_song *cbox_rt_set_pattern(struct cbox_rt *rt, struct cbox_midi_pattern *pattern, int new_pos_ppqn)
{
    struct cbox_track *track = cbox_track_new();
    cbox_track_add_item(track, 0, pattern, 0, pattern->loop_end);
    cbox_track_update_playback(track, rt->master);

#if 0    
    struct cbox_track *track2 = cbox_track_new();
    struct cbox_midi_pattern *metro = cbox_midi_pattern_new_metronome(4);
    cbox_track_add_item(track2, 0, metro, 0, metro->loop_end);
    cbox_track_update_playback(track2, rt->master);
#endif
    
    struct cbox_song *song = cbox_song_new(rt->master);
    cbox_song_add_track(song, track);
#if 0
    cbox_song_add_track(song, track2);
#endif

    song->loop_start_ppqn = 0;
    song->loop_end_ppqn = pattern->loop_end;
    
    struct cbox_song *old_song = rt->master->song;
    rt->master->song = song;
    cbox_rt_update_song(rt, new_pos_ppqn);
    return old_song;
}

////////////////////////////////////////////////////////////////////////////////////////

struct send_events_command
{
    struct cbox_rt *rt;
    
    struct cbox_midi_buffer *buffer;
    int pos;
    int time_delta;
};

static int send_events_command_execute(void *user_data)
{
    struct send_events_command *cmd = user_data;
    
    // all done?
    if (cmd->pos >= cbox_midi_buffer_get_count(cmd->buffer))
        return 1;
    
    int last_time = cbox_midi_buffer_get_last_event_time(&cmd->rt->midibuf_aux);
    while (cmd->pos < cbox_midi_buffer_get_count(cmd->buffer))
    {
        struct cbox_midi_event *event = cbox_midi_buffer_get_event(cmd->buffer, cmd->pos);
        int time = event->time - cmd->time_delta;
        if (time < last_time)
            time = last_time;
        cbox_midi_buffer_copy_event(&cmd->rt->midibuf_aux, event, time);
        cmd->pos++;
    }
    assert(cmd->rt->buffer_size);
    cmd->time_delta += cmd->rt->buffer_size;
    
    return (cmd->pos >= cbox_midi_buffer_get_count(cmd->buffer)) ? 1 : 0;
}

void cbox_rt_send_events(struct cbox_rt *rt, struct cbox_midi_buffer *buffer)
{
    if (cbox_midi_buffer_get_count(buffer) == 0)
        return;
    
    static struct cbox_rt_cmd_definition def = { .prepare = NULL, .execute = send_events_command_execute, .cleanup = NULL };
    
    struct send_events_command cmd = { rt, buffer, 0, 0 };
    
    cbox_rt_execute_cmd_sync(rt, &def, &cmd);
}

////////////////////////////////////////////////////////////////////////////////////////

struct swap_pointers_command
{
    void **ptr;
    void *old_value;
    void *new_value;
    int *pcount;
    int new_count;
};

static int swap_pointers_command_execute(void *user_data)
{
    struct swap_pointers_command *cmd = user_data;
    
    cmd->old_value = *cmd->ptr;
    *cmd->ptr = cmd->new_value;
    if (cmd->pcount)
        *cmd->pcount = cmd->new_count;
    
    return 1;
}

void *cbox_rt_swap_pointers(struct cbox_rt *rt, void **ptr, void *new_value)
{
    if (rt)
    {
        static struct cbox_rt_cmd_definition scdef = { .prepare = NULL, .execute = swap_pointers_command_execute, .cleanup = NULL };
        
        struct swap_pointers_command sc = { ptr, NULL, new_value, NULL, 0 };
        
        cbox_rt_execute_cmd_sync(rt, &scdef, &sc);

        return sc.old_value;
    } else {
        void *old_ptr = *ptr;
        *ptr = new_value;
        return old_ptr;
    }
}

void *cbox_rt_swap_pointers_and_update_count(struct cbox_rt *rt, void **ptr, void *new_value, int *pcount, int new_count)
{
    if (rt)
    {
        static struct cbox_rt_cmd_definition scdef = { .prepare = NULL, .execute = swap_pointers_command_execute, .cleanup = NULL };
        
        struct swap_pointers_command sc = { ptr, NULL, new_value, pcount, new_count };
        
        cbox_rt_execute_cmd_sync(rt, &scdef, &sc);
        
        return sc.old_value;
    }
    else
    {
        void *old_ptr = *ptr;
        *ptr = new_value;
        *pcount = new_count;
        return old_ptr;        
    }
}

////////////////////////////////////////////////////////////////////////////////////////

struct cbox_scene *cbox_rt_set_scene(struct cbox_rt *rt, struct cbox_scene *scene)
{
    return cbox_rt_swap_pointers(rt, (void **)&rt->scene, scene);
}

////////////////////////////////////////////////////////////////////////////////////////

int cbox_rt_get_sample_rate(struct cbox_rt *rt)
{
    assert(rt->srate);
    return rt->srate;
}

int cbox_rt_get_buffer_size(struct cbox_rt *rt)
{
    assert(rt->buffer_size);
    return rt->buffer_size;
}

////////////////////////////////////////////////////////////////////////////////////////

void cbox_rt_destroy(struct cbox_rt *rt)
{
    if (rt->master->song)
        cbox_song_destroy(rt->master->song);
    jack_ringbuffer_free(rt->rb_execute);
    jack_ringbuffer_free(rt->rb_cleanup);

    free(rt->master);
    rt->master = NULL;

    cbox_instruments_destroy(rt->instruments);
    rt->instruments = NULL;

    free(rt);
}
