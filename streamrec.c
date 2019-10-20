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

#include "engine.h"
#include "errors.h"
#include "recsrc.h"
#include "rt.h"
#include <assert.h>
#include <glib.h>
#include <malloc.h>
#include <pthread.h>
#include <semaphore.h>
#include <sndfile.h>
#include <string.h>
#include <unistd.h>

// XXXKF the syncing model here is flawed in several ways:
// - it's not possible to do block-accurate syncing
// - it's not possible to flush the output buffer and stop recording
// - rb_for_writing is being written from two threads (audio and UI),
//   which is not guaranteed to work
// - 

// 1/8s for 44.1kHz stereo float
#define STREAM_BUFFER_SIZE 16384
#define STREAM_BUFFER_COUNT 8

#define STREAM_CMD_QUIT (-1)
#define STREAM_CMD_SYNC (-2)

struct recording_buffer
{
    float data[STREAM_BUFFER_SIZE];
    uint32_t write_ptr;
};

struct stream_recorder
{
    struct cbox_recorder iface;
    struct recording_buffer buffers[STREAM_BUFFER_COUNT];

    struct cbox_rt *rt;
    struct cbox_engine *engine;
    gchar *filename;
    SNDFILE *volatile sndfile;
    SF_INFO info;
    pthread_t thr_writeout;
    sem_t sem_sync_completed;
    
    struct recording_buffer *cur_buffer;
    uint32_t write_ptr;

    struct cbox_fifo *rb_for_writing, *rb_just_written;
};

static void *stream_recorder_thread(void *user_data)
{
    struct stream_recorder *self = user_data;
    
    do {
        int8_t buf_idx;
        if (!cbox_fifo_read_atomic(self->rb_for_writing, &buf_idx, 1))
        {
            usleep(10000);
            continue;
        }
        if (buf_idx == STREAM_CMD_QUIT)
            break;
        if (buf_idx == STREAM_CMD_SYNC)
        {
            // this assumes that the recorder is already detached from any source
            if (self->cur_buffer && self->cur_buffer->write_ptr)
                sf_write_float(self->sndfile, self->cur_buffer->data, self->cur_buffer->write_ptr);
            
            sf_command(self->sndfile, SFC_UPDATE_HEADER_NOW, NULL, 0);
            sf_write_sync(self->sndfile);
            sem_post(&self->sem_sync_completed);
            continue;
        }
        else
        {
            sf_write_float(self->sndfile, self->buffers[buf_idx].data, self->buffers[buf_idx].write_ptr);
            self->buffers[buf_idx].write_ptr = 0;
            cbox_fifo_write_atomic(self->rb_just_written, &buf_idx, 1);
            sf_command(self->sndfile, SFC_UPDATE_HEADER_NOW, NULL, 0);
        }
    } while(1);
    return NULL;
}

static gboolean stream_recorder_attach(struct cbox_recorder *handler, struct cbox_recording_source *src, GError **error)
{
    struct stream_recorder *self = handler->user_data;
    
    if (self->sndfile)
    {
        if (error)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Recorder already attached to a different source");
        return FALSE;
    }

    memset(&self->info, 0, sizeof(self->info));
    self->info.frames = 0;
    self->info.samplerate = self->engine->io_env.srate;
    self->info.channels = src->channels;
    self->info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT; // XXXKF make format configurable on instantiation
    self->info.sections = 0;
    self->info.seekable = 0;
    
    self->sndfile = sf_open(self->filename, SFM_WRITE, &self->info);
    if (!self->sndfile)
    {
        if (error)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot open sound file '%s': %s", self->filename, sf_strerror(NULL));
        return FALSE;
    }
    
    pthread_create(&self->thr_writeout, NULL, stream_recorder_thread, self);
    return TRUE;
}

void stream_recorder_record_block(struct cbox_recorder *handler, const float **buffers, uint32_t offset, uint32_t numsamples)
{
    struct stream_recorder *self = handler->user_data;

    if (!self->sndfile)
        return;
    
    if (self->cur_buffer && (self->cur_buffer->write_ptr + numsamples * self->info.channels) * sizeof(float) >= STREAM_BUFFER_SIZE)
    {
        int8_t idx = self->cur_buffer - self->buffers;
        cbox_fifo_write_atomic(self->rb_for_writing, &idx, 1);
        self->cur_buffer = NULL;
    }
    if (!self->cur_buffer)
    {
        int8_t buf_idx = -1;
        if (!cbox_fifo_read_atomic(self->rb_just_written, &buf_idx, 1)) // underrun
            return;
        self->cur_buffer = &self->buffers[buf_idx];
    }
    
    unsigned int nc = self->info.channels;
    
    float *wbuf = self->cur_buffer->data + self->cur_buffer->write_ptr;
    for (unsigned int c = 0; c < nc; c++)
        for (uint32_t i = 0; i < numsamples; i++)
            wbuf[c + i * nc] = buffers[c][i];
    self->cur_buffer->write_ptr += nc * numsamples;
}

gboolean stream_recorder_detach(struct cbox_recorder *handler, GError **error)
{
    struct stream_recorder *self = handler->user_data;
    
    if (!self->sndfile)
    {
        if (error)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No sound file associated with stream recorder");
        return FALSE;
    }

    int8_t cmd = STREAM_CMD_SYNC;
    cbox_fifo_write_atomic(self->rb_for_writing, (char *)&cmd, 1);
    sem_wait(&self->sem_sync_completed);
    return TRUE;
}

void stream_recorder_destroy(struct cbox_recorder *handler)
{
    struct stream_recorder *self = handler->user_data;
    
    if (self->sndfile)
    {
        int8_t cmd = STREAM_CMD_QUIT;
        cbox_fifo_write_atomic(self->rb_for_writing, (char *)&cmd, 1);
        pthread_join(self->thr_writeout, NULL);
    }
    
    cbox_fifo_destroy(self->rb_for_writing);
    cbox_fifo_destroy(self->rb_just_written);
    free(self);
}


static gboolean stream_recorder_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct stream_recorder *rec = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!cbox_execute_on(fb, NULL, "/filename", "s", error, rec->filename))
            return FALSE;
        return CBOX_OBJECT_DEFAULT_STATUS(&rec->iface, fb, error);
    }
    return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

struct cbox_recorder *cbox_recorder_new_stream(struct cbox_engine *engine, struct cbox_rt *rt, const char *filename)
{
    struct stream_recorder *self = malloc(sizeof(struct stream_recorder));
    self->rt = rt;
    self->engine = engine;
    CBOX_OBJECT_HEADER_INIT(&self->iface, cbox_recorder, CBOX_GET_DOCUMENT(engine));
    cbox_command_target_init(&self->iface.cmd_target, stream_recorder_process_cmd, self);
    
    self->iface.user_data = self;
    self->iface.attach = stream_recorder_attach;
    self->iface.record_block = stream_recorder_record_block;
    self->iface.detach = stream_recorder_detach;
    self->iface.destroy = stream_recorder_destroy;
    
    self->sndfile = NULL;
    self->filename = g_strdup(filename);
    self->cur_buffer = NULL;

    self->rb_for_writing = cbox_fifo_new(STREAM_BUFFER_COUNT + 1);
    self->rb_just_written = cbox_fifo_new(STREAM_BUFFER_COUNT + 1);
    sem_init(&self->sem_sync_completed, 0, 0);
    
    CBOX_OBJECT_REGISTER(&self->iface);

    for (uint8_t i = 0; i < STREAM_BUFFER_COUNT; i++)
        cbox_fifo_write_atomic(self->rb_just_written, (char *)&i, 1);
    
    return &self->iface;
}
