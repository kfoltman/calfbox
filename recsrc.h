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

#ifndef CBOX_RECSRC_H
#define CBOX_RECSRC_H

#include "cmd.h"
#include "dom.h"

struct cbox_recording_source;
struct cbox_rt;
CBOX_EXTERN_CLASS(cbox_recorder)

struct cbox_recorder
{
    CBOX_OBJECT_HEADER()    
    void *user_data;
    struct cbox_command_target cmd_target;
    
    gboolean (*attach)(struct cbox_recorder *handler, struct cbox_recording_source *src, GError **error);
    void (*record_block)(struct cbox_recorder *handler, const float **buffers, uint32_t numsamples);
    gboolean (*detach)(struct cbox_recorder *handler, GError **error);
    void (*destroy)(struct cbox_recorder *handler);
};

struct cbox_recording_source
{
    struct cbox_command_target cmd_target;
    struct cbox_scene *scene;
    
    struct cbox_recorder **handlers;
    int handler_count;
    uint32_t max_numsamples;
    int channels;
};

#define IS_RECORDING_SOURCE_CONNECTED(src) ((src).handler_count != 0)

extern void cbox_recording_source_init(struct cbox_recording_source *src, struct cbox_scene *scene, uint32_t max_numsamples, int channels);
extern gboolean cbox_recording_source_attach(struct cbox_recording_source *src, struct cbox_recorder *rec, GError **error);
extern int cbox_recording_source_detach(struct cbox_recording_source *src, struct cbox_recorder *rec, GError **error);
extern void cbox_recording_source_push(struct cbox_recording_source *src, const float **buffers, uint32_t numsamples);
extern void cbox_recording_source_uninit(struct cbox_recording_source *src);

extern struct cbox_recorder *cbox_recorder_new_stream(struct cbox_rt *rt, const char *filename);

#endif
