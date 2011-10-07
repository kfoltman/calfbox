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

struct cbox_recording_source;

struct cbox_recorder
{
    void *user_data;
    struct cbox_command_target cmd_target;
    
    void (*attach)(struct cbox_recorder *handler, struct cbox_recording_source *src);
    void (*record_block)(struct cbox_recorder *handler, const float **buffers, uint32_t numsamples);
    void (*detach)(struct cbox_recorder *handler);
    void (*destroy)(struct cbox_recorder *handler);
};

struct cbox_recording_source
{
    struct cbox_command_target cmd_target;
    
    struct cbox_recorder **handlers;
    int handler_count;
    uint32_t max_numsamples;
    int channels;
};

#define IS_RECORDING_SOURCE_CONNECTED(src) ((src).handler_count != 0)

extern void cbox_recording_source_init(struct cbox_recording_source *src, uint32_t max_numsamples, int channels);
extern void cbox_recording_source_attach(struct cbox_recording_source *src, struct cbox_recorder *rec);
extern int cbox_recording_source_detach(struct cbox_recording_source *src, struct cbox_recorder *rec);
extern void cbox_recording_source_change(struct cbox_recording_source *src, uint32_t max_numsamples, int channels);
extern void cbox_recording_source_push(struct cbox_recording_source *src, const float **buffers, uint32_t numsamples);
extern void cbox_recording_source_uninit(struct cbox_recording_source *src);

extern struct cbox_recorder *cbox_recorder_new_stream(const char *filename);

#endif
