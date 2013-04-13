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

#ifndef CBOX_ENGINE_H
#define CBOX_ENGINE_H

#include "cmd.h"
#include "dom.h"
#include "io.h"
#include "midi.h"
#include "rt.h"

CBOX_EXTERN_CLASS(cbox_engine)

#define GET_RT_FROM_cbox_engine(ptr) ((ptr)->rt)

struct cbox_engine
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    struct cbox_rt *rt;
    struct cbox_scene *scene;
    struct cbox_module *effect;
    struct cbox_master *master;
    struct cbox_midi_buffer midibuf_aux, midibuf_jack, midibuf_song;
    
    struct cbox_midi_buffer midibufs_appsink[2];
    int current_appsink_buffer;
};

// These use an RT command internally
extern struct cbox_engine *cbox_engine_new(struct cbox_rt *rt);
extern void cbox_engine_update_song_playback(struct cbox_engine *engine);
extern struct cbox_scene *cbox_engine_set_scene(struct cbox_engine *engine, struct cbox_scene *scene);
extern struct cbox_song *cbox_engine_set_song(struct cbox_engine *engine, struct cbox_song *song, int new_pos);
extern struct cbox_song *cbox_engine_set_pattern(struct cbox_engine *engine, struct cbox_midi_pattern *pattern, int new_pos);
extern void cbox_engine_set_pattern_and_destroy(struct cbox_engine *engine, struct cbox_midi_pattern *pattern);
extern void cbox_engine_send_events_to(struct cbox_engine *engine, struct cbox_midi_merger *merger, struct cbox_midi_buffer *buffer);
extern const struct cbox_midi_buffer *cbox_engine_get_input_midi_data(struct cbox_engine *engine);
extern void cbox_engine_process(struct cbox_engine *engine, struct cbox_io *io, uint32_t nframes);

extern int cbox_engine_get_sample_rate(struct cbox_engine *engine);
extern int cbox_engine_get_buffer_size(struct cbox_engine *engine);

#endif
