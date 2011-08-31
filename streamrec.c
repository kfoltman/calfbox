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

#include "app.h"
#include "recsrc.h"
#include <assert.h>
#include <glib.h>
#include <jack/ringbuffer.h>
#include <malloc.h>
#include <pthread.h>
#include <semaphore.h>
#include <sndfile.h>
#include <string.h>
#include <unistd.h>

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
    
    gchar *filename;
    SNDFILE *sndfile;
    SF_INFO info;
    pthread_t thr_writeout;
    sem_t sem_sync_completed;
    
    struct recording_buffer *cur_buffer;
    uint32_t write_ptr;

    jack_ringbuffer_t *rb_for_writing, *rb_just_written;
};

static void *stream_recorder_thread(void *user_data)
{
    struct stream_recorder *self = user_data;
    
    do {
        int8_t buf_idx;
        if (!jack_ringbuffer_read(self->rb_for_writing, &buf_idx, 1))
        {
            usleep(10000);
            continue;
        }
        if (buf_idx == STREAM_CMD_QUIT)
            break;
        if (buf_idx == STREAM_CMD_SYNC)
        {
            sf_command(self->sndfile, SFC_UPDATE_HEADER_NOW, NULL, 0);
            sf_write_sync(self->sndfile);
            sem_post(&self->sem_sync_completed);
            continue;
        }
        else
        {
            sf_write_float(self->sndfile, self->buffers[buf_idx].data, self->buffers[buf_idx].write_ptr);
            self->buffers[buf_idx].write_ptr = 0;
            jack_ringbuffer_write(self->rb_just_written, &buf_idx, 1);
        }
    } while(1);
    
}

static void stream_recorder_attach(struct cbox_recorder *handler, struct cbox_recording_source *src)
{
    struct stream_recorder *self = handler->user_data;
    
    if (!self->sndfile)
    {
        memset(&self->info, 0, sizeof(self->info));
        self->info.frames = 0;
        self->info.samplerate = cbox_io_get_sample_rate(&app.io);
        self->info.channels = src->channels;
        self->info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT; // XXXKF 
        self->info.sections = 0;
        self->info.seekable = 0;
        
        self->sndfile = sf_open(self->filename, SFM_WRITE, &self->info);
        if (!self->sndfile)
            g_warning("SF error: %s", sf_strerror(NULL));
        
        self->thr_writeout = pthread_create(&self->thr_writeout, NULL, stream_recorder_thread, self);
    }
    else
        assert(self->info.channels == src->channels); // changes of channel counts are supported
}

void stream_recorder_record_block(struct cbox_recorder *handler, const float **buffers, uint32_t numsamples)
{
    struct stream_recorder *self = handler->user_data;

    if (!self->sndfile)
        return;
    
    if (self->cur_buffer && (self->cur_buffer->write_ptr + numsamples * self->info.channels) * sizeof(float) >= STREAM_BUFFER_SIZE)
    {
        int8_t idx = self->cur_buffer - self->buffers;
        jack_ringbuffer_write(self->rb_for_writing, &idx, 1);
        self->cur_buffer = NULL;
    }
    if (!self->cur_buffer)
    {
        int8_t buf_idx = -1;
        if (!jack_ringbuffer_read(self->rb_just_written, &buf_idx, 1)) // underrun
            return;
        self->cur_buffer = &self->buffers[buf_idx];
    }
    
    unsigned int nc = self->info.channels;
    
    float *wbuf = self->cur_buffer->data + self->cur_buffer->write_ptr;
    for (unsigned int c = 0; c < nc; c++)
        for (int i = 0; i < numsamples; i++)
            wbuf[c + i * nc] = buffers[c][i];
    self->cur_buffer->write_ptr += nc * numsamples;
}

void stream_recorder_detach(struct cbox_recorder *handler)
{
    struct stream_recorder *self = handler->user_data;
    
    if (self->sndfile)
    {
        int8_t cmd = STREAM_CMD_SYNC;
        jack_ringbuffer_write(self->rb_for_writing, &cmd, 1);
        sem_wait(&self->sem_sync_completed);
    }
}

void stream_recorder_destroy(struct cbox_recorder *handler)
{
    struct stream_recorder *self = handler->user_data;
    
    if (self->sndfile)
    {
        int8_t cmd = STREAM_CMD_QUIT;
        jack_ringbuffer_write(self->rb_for_writing, &cmd, 1);
        pthread_join(self->thr_writeout, NULL);
    }
    
    free(self);
}


struct cbox_recorder *cbox_recorder_new_stream(const char *filename)
{
    struct stream_recorder *self = malloc(sizeof(struct stream_recorder));
    self->iface.user_data = self;
    self->iface.attach = stream_recorder_attach;
    self->iface.record_block = stream_recorder_record_block;
    self->iface.detach = stream_recorder_detach;
    self->iface.destroy = stream_recorder_destroy;
    
    self->sndfile = NULL;
    self->filename = g_strdup(filename);
    self->cur_buffer = NULL;

    self->rb_for_writing = jack_ringbuffer_create(STREAM_BUFFER_COUNT + 1);
    self->rb_just_written = jack_ringbuffer_create(STREAM_BUFFER_COUNT + 1);
    sem_init(&self->sem_sync_completed, 0, 0);

    for (uint8_t i = 0; i < STREAM_BUFFER_COUNT; i++)
        jack_ringbuffer_write(self->rb_just_written, &i, 1);
    
    return &self->iface;
}
