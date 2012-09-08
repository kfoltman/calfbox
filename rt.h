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

#ifndef CBOX_PROCMAIN_H
#define CBOX_PROCMAIN_H

#include <stdint.h>
#include <jack/ringbuffer.h>

#include "cmd.h"
#include "dom.h"
#include "midi.h"

#define RT_CMD_QUEUE_ITEMS 1024
#define RT_MAX_COST_PER_CALL 100

struct cbox_instruments;
struct cbox_scene;
struct cbox_io;
struct cbox_midi_pattern;
struct cbox_song;
struct cbox_rt_cmd_instance;

struct cbox_rt_cmd_definition
{
    int (*prepare)(void *user_data); // non-zero to skip the whole thing
    int (*execute)(void *user_data); // returns cost
    void (*cleanup)(void *user_data);
};

CBOX_EXTERN_CLASS(cbox_rt)

struct cbox_rt
{
    CBOX_OBJECT_HEADER()
    
    struct cbox_scene *scene;
    struct cbox_module *effect;
    
    struct cbox_io *io;
    struct cbox_io_callbacks *cbs;
    struct cbox_master *master;
    struct cbox_midi_buffer midibuf_aux;
    
    jack_ringbuffer_t *rb_execute, *rb_cleanup;
    
    struct cbox_command_target cmd_target;
    int started;
    int srate;
    int buffer_size;
};

extern struct cbox_rt *cbox_rt_new(struct cbox_document *doc);

extern void cbox_rt_set_io(struct cbox_rt *rt, struct cbox_io *io);
extern void cbox_rt_set_offline(struct cbox_rt *rt, int sample_rate, int buffer_size);
extern void cbox_rt_start(struct cbox_rt *rt);
extern void cbox_rt_handle_cmd_queue(struct cbox_rt *rt);
extern void cbox_rt_stop(struct cbox_rt *rt);

// Those are for calling from the main thread. I will add a RT-thread version later.
extern void cbox_rt_execute_cmd_sync(struct cbox_rt *rt, struct cbox_rt_cmd_definition *cmd, void *user_data);
extern void cbox_rt_execute_cmd_async(struct cbox_rt *rt, struct cbox_rt_cmd_definition *cmd, void *user_data);
extern void *cbox_rt_swap_pointers(struct cbox_rt *rt, void **ptr, void *new_value);
extern void *cbox_rt_swap_pointers_and_update_count(struct cbox_rt *rt, void **ptr, void *new_value, int *pcount, int new_count);

extern void cbox_rt_array_insert(struct cbox_rt *rt, void ***ptr, int *pcount, int index, void *new_value);
extern void *cbox_rt_array_remove(struct cbox_rt *rt, void ***ptr, int *pcount, int index);

// These use an RT command internally
extern struct cbox_scene *cbox_rt_set_scene(struct cbox_rt *rt, struct cbox_scene *scene);
extern struct cbox_song *cbox_rt_set_song(struct cbox_rt *rt, struct cbox_song *song, int new_pos);
extern struct cbox_song *cbox_rt_set_pattern(struct cbox_rt *rt, struct cbox_midi_pattern *pattern, int new_pos);
extern void cbox_rt_set_pattern_and_destroy(struct cbox_rt *rt, struct cbox_midi_pattern *pattern);
extern void cbox_rt_send_events(struct cbox_rt *rt, struct cbox_midi_buffer *buffer);

extern int cbox_rt_get_sample_rate(struct cbox_rt *rt);
extern int cbox_rt_get_buffer_size(struct cbox_rt *rt);

extern void cbox_rt_destroy(struct cbox_rt *rt);

#endif
