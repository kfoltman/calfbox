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

#include "instr.h"
#include "io.h"
#include "layer.h"
#include "midi.h"
#include "module.h"
#include "procmain.h"
#include "scene.h"
#include <jack/jack.h>
#include <jack/types.h>
#include <jack/midiport.h>
#include <unistd.h>

struct cbox_rt_cmd_instance
{
    struct cbox_rt_cmd_definition *definition;
    void *user_data;
    int is_async;
};

struct cbox_rt *cbox_rt_new()
{
    struct cbox_rt *rt = malloc(sizeof(struct cbox_rt));
    
    rt->scene = NULL;
    rt->effect = NULL;
    rt->rb_execute = jack_ringbuffer_create(sizeof(struct cbox_rt_cmd_instance) * RT_CMD_QUEUE_ITEMS);
    rt->rb_cleanup = jack_ringbuffer_create(sizeof(struct cbox_rt_cmd_instance) * RT_CMD_QUEUE_ITEMS * 2);
    rt->io = NULL;
    rt->mpb.pattern = cbox_midi_pattern_new();
    rt->mpb.pos = 0;
    rt->mpb.time = 0;
    rt->play_pattern = 0;
    return rt;
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
                    // note filter
                    if (data[1] < lp->low_note || data[1] > lp->high_note)
                        continue;
                    // transpose
                    if (lp->transpose)
                    {
                        int note = data[1] + lp->transpose;
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
            }
            if (!cbox_midi_buffer_write_event(&lp->instrument->module->midi_input, event->time, data, event->size))
                return -i;
        }
    }
    
    return event_count;
}

static void cbox_rt_process(void *user_data, struct cbox_io *io, uint32_t nframes)
{
    struct cbox_rt *rt = user_data;
    struct cbox_scene *scene = rt->scene;
    struct cbox_module *effect = rt->effect;
    struct cbox_rt_cmd_instance cmd;
    int cost;
    uint32_t i, j, n;
    
    float *out_l = jack_port_get_buffer(io->output_l, nframes);
    float *out_r = jack_port_get_buffer(io->output_r, nframes);

    struct cbox_midi_buffer midibuf;
    cbox_midi_buffer_init(&midibuf);
    if (scene)
    {
        if (!rt->play_pattern)
            convert_midi_from_jack(io->midi, nframes, &midibuf);
        else
            cbox_read_pattern(&rt->mpb, &midibuf, nframes);
        write_events_to_instrument_ports(&midibuf, scene);
    }
    
    for (i = 0; i < nframes; i ++)
    {
        out_l[i] = out_r[i] = 0.f;
    }
    
    for (n = 0; scene && n < scene->instrument_count; n++)
    {
        struct cbox_instrument *instr = scene->instruments[n];
        struct cbox_module *module = instr->module;
        struct cbox_module *effect = instr->insert;
        int event_count = instr->module->midi_input.count;
        int cur_event = 0;
        uint32_t highwatermark = 0;
        
        for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
        {
            cbox_sample_t left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            cbox_sample_t *outputs[2] = {left, right};
            
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
            if (effect)
            {
                (*effect->process_block)(effect, outputs, outputs);
            }
            for (j = 0; j < CBOX_BLOCK_SIZE; j++)
            {
                out_l[i + j] += left[j];
                out_r[i + j] += right[j];
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
    
    // Process "master" effect
    if (effect)
    {
        for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
        {
            cbox_sample_t left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
            cbox_sample_t *in_bufs[2] = {out_l + i, out_r + i};
            cbox_sample_t *out_bufs[2] = {left, right};
            (*effect->process_block)(effect, in_bufs, out_bufs);
            for (j = 0; j < CBOX_BLOCK_SIZE; j++)
            {
                out_l[i + j] = left[j];
                out_r[i + j] = right[j];
            }
        }
    }
    
    // Process command queue
    cost = 0;
    while(cost < RT_MAX_COST_PER_CALL && jack_ringbuffer_read(rt->rb_execute, (char *)&cmd, sizeof(cmd)))
    {
        cost += (cmd.definition->execute)(cmd.user_data);
        if (cmd.definition->cleanup || !cmd.is_async)
            jack_ringbuffer_write(rt->rb_cleanup, (const char *)&cmd, sizeof(cmd));
    }
        
    // Update transport
    if (rt->master->state == CMTS_ROLLING)
        rt->master->song_pos_samples += nframes;
}

void cbox_rt_start(struct cbox_rt *rt, struct cbox_io *io)
{
    rt->io = io;
    rt->cbs = malloc(sizeof(struct cbox_io_callbacks));
    rt->cbs->user_data = rt;
    rt->cbs->process = cbox_rt_process;
    rt->master = malloc(sizeof(struct cbox_master));
    cbox_master_init(rt->master, jack_get_sample_rate(io->client));

    cbox_io_start(io, rt->cbs);        
}

void cbox_rt_stop(struct cbox_rt *rt)
{
    cbox_io_stop(rt->io);
    free(rt->master);
    free(rt->cbs);
    rt->cbs = NULL;
    rt->master = NULL;
    rt->io = NULL;
}

void cbox_rt_cmd_handle_queue(struct cbox_rt *rt)
{
    struct cbox_rt_cmd_instance cmd;
    
    while(jack_ringbuffer_read(rt->rb_cleanup, (char *)&cmd, sizeof(cmd)))
    {
        cmd.definition->cleanup(cmd.user_data);
    }
}

void cbox_rt_cmd_execute_sync(struct cbox_rt *rt, struct cbox_rt_cmd_definition *def, void *user_data)
{
    struct cbox_rt_cmd_instance cmd;
    
    if (def->prepare)
        if (def->prepare(user_data))
            return;
        
    // No realtime thread - do it all in the main thread
    if (!rt->io)
    {
        def->execute(user_data);
        if (def->cleanup)
            def->cleanup(user_data);
        return;
    }
    
    cmd.definition = def;
    cmd.user_data = user_data;
    cmd.is_async = 0;
    
    jack_ringbuffer_write(rt->rb_execute, (const char *)&cmd, sizeof(cmd));
    do
    {
        struct cbox_rt_cmd_instance cmd2;
    
        if (!jack_ringbuffer_read(rt->rb_cleanup, (char *)&cmd2, sizeof(cmd2)))
        {
            // still no result in cleanup queue - wait
            usleep(10000);
            continue;
        }
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

void cbox_rt_cmd_execute_async(struct cbox_rt *rt, struct cbox_rt_cmd_definition *def, void *user_data)
{
    struct cbox_rt_cmd_instance cmd = { def, user_data, 1 };
    
    if (def->prepare)
    {
        if (def->prepare(user_data))
            return;
    }
    // No realtime thread - do it all in the main thread
    if (!rt->io)
    {
        def->execute(user_data);
        if (def->cleanup)
            def->cleanup(user_data);
        return;
    }
    
    jack_ringbuffer_write(rt->rb_execute, (const char *)&cmd, sizeof(cmd));
    
    // will be cleaned up by next sync call or by cbox_rt_cmd_handle_queue
}

struct set_scene_command
{
    struct cbox_rt *rt;
    struct cbox_scene *new_scene, *old_scene;
};

static int set_scene_command_execute(void *user_data)
{
    struct set_scene_command *cmd = user_data;
    
    cmd->old_scene = cmd->rt->scene;
    cmd->rt->scene = cmd->new_scene;
    
    return 1;
}

struct cbox_scene *cbox_rt_set_scene(struct cbox_rt *rt, struct cbox_scene *scene)
{
    static struct cbox_rt_cmd_definition scdef = { .prepare = NULL, .execute = set_scene_command_execute, .cleanup = NULL };
    
    struct set_scene_command sc = { rt, scene, NULL };
    
    cbox_rt_cmd_execute_sync(rt, &scdef, &sc);
    
    return sc.old_scene;
}


void cbox_rt_destroy(struct cbox_rt *rt)
{
    jack_ringbuffer_free(rt->rb_execute);
    jack_ringbuffer_free(rt->rb_cleanup);
    free(rt);
}
