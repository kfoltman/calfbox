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

#include "assert.h"
#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "module.h"
#include <errno.h>
#include <glib.h>
#include <jack/ringbuffer.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <pthread.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CUE_BUFFER_SIZE 16000
#define PREFETCH_THRESHOLD (CUE_BUFFER_SIZE / 4)
#define MAX_READAHEAD_BUFFERS 3

#define NO_SAMPLE_LOOP ((uint64_t)-1ULL)

struct stream_player_cue_point
{
    volatile uint64_t position;
    volatile uint32_t size, length;
    float *data;
    int queued;
};

struct stream_player_module
{
    struct cbox_module module;

    SNDFILE *sndfile;
    SF_INFO info;
    uint64_t readptr;
    uint64_t restart;
    
    volatile int buffer_in_use;
    
    struct stream_player_cue_point cp_start, cp_loop, cp_readahead[MAX_READAHEAD_BUFFERS];
    int cp_readahead_ready[MAX_READAHEAD_BUFFERS];
    struct stream_player_cue_point *pcp_current, *pcp_next;
    
    jack_ringbuffer_t *rb_for_reading, *rb_just_read;
    
    pthread_t thr_preload;
};

static void init_cue(struct stream_player_module *m, struct stream_player_cue_point *pt, uint32_t size, uint64_t pos)
{
    pt->data = malloc(size * sizeof(float) * m->info.channels);
    pt->size = size;
    pt->length = 0;
    pt->queued = 0;
    pt->position = pos;
}

static void load_at_cue(struct stream_player_module *m, struct stream_player_cue_point *pt)
{
    if (pt->position != NO_SAMPLE_LOOP)
    {
        sf_seek(m->sndfile, pt->position, 0);
        pt->length = sf_readf_float(m->sndfile, pt->data, pt->size);
    }
    pt->queued = 0;
}

static int is_contained(struct stream_player_cue_point *pt, uint64_t ofs)
{
    return pt->position != NO_SAMPLE_LOOP && ofs >= pt->position && ofs < pt->position + pt->length;
}

static int is_queued(struct stream_player_cue_point *pt, uint64_t ofs)
{
    return pt->queued && pt->position != NO_SAMPLE_LOOP && ofs >= pt->position && ofs < pt->position + pt->size;
}

struct stream_player_cue_point *get_cue(struct stream_player_module *m, uint64_t pos)
{
    int i;
    
    if (is_contained(&m->cp_loop, pos))
        return &m->cp_loop;
    if (is_contained(&m->cp_start, pos))
        return &m->cp_start;
    
    for (i = 0; i < MAX_READAHEAD_BUFFERS; i++)
    {
        if (m->cp_readahead_ready[i] && is_contained(&m->cp_readahead[i], pos))
            return &m->cp_readahead[i];
    }
    return NULL;
}

struct stream_player_cue_point *get_queued_buffer(struct stream_player_module *m, uint64_t pos)
{
    int i;
    
    for (i = 0; i < MAX_READAHEAD_BUFFERS; i++)
    {
        if (!m->cp_readahead_ready[i] && is_queued(&m->cp_readahead[i], pos))
            return &m->cp_readahead[i];
    }
    return NULL;
}

void request_load(struct stream_player_module *m, int buf_idx, uint64_t pos)
{
    unsigned char cidx = (unsigned char)buf_idx;
    struct stream_player_cue_point *pt = &m->cp_readahead[buf_idx];
    int wlen = 0;
    
    m->cp_readahead_ready[buf_idx] = 0;    
    pt->position = pos;
    pt->length = 0;
    pt->queued = 1;

    wlen = jack_ringbuffer_write(m->rb_for_reading, &cidx, 1);
    assert(wlen);
}

int get_unused_buffer(struct stream_player_module *m)
{
    int i = 0;
    int notbad = -1;
    
    // return first buffer that is not currently played or in queue; XXXKF this is a very primitive strategy, a good one would at least use the current play position
    for (i = 0; i < MAX_READAHEAD_BUFFERS; i++)
    {
        int64_t rel;
        if (&m->cp_readahead[i] == m->pcp_current)
            continue;
        if (m->cp_readahead[i].queued)
            continue;
        // If there's any unused buffer, return it
        if (m->cp_readahead[i].position == NO_SAMPLE_LOOP)
            return i;
        // If this has already been played, return it
        rel = m->readptr - m->cp_readahead[i].position;
        if (rel >= m->cp_readahead[i].length)
            return i;
        // Use as second chance
        notbad = i;
    }
    return notbad;
}

void *sample_preload_thread(void *user_data)
{
    struct stream_player_module *m = user_data;
    
    do {
        unsigned char buf_idx;
        if (!jack_ringbuffer_read(m->rb_for_reading, &buf_idx, 1))
        {
            usleep(5000);
            continue;
        }
        if (buf_idx == 255)
            break;
        // fprintf(stderr, "Preload: %d, %lld\n", (int)buf_idx, (long long)m->cp_readahead[buf_idx].position);
        load_at_cue(m, &m->cp_readahead[buf_idx]);
        // fprintf(stderr, "Preloaded\n", (int)buf_idx, (long long)m->cp_readahead[buf_idx].position);
        jack_ringbuffer_write(m->rb_just_read, &buf_idx, 1);
    } while(1);
        
}

void stream_player_process_event(void *user_data, const uint8_t *data, uint32_t len)
{
    struct stream_player_module *m = user_data;
}

static void request_next(struct stream_player_module *m, uint64_t pos)
{
    // Check if we've requested a next buffer, if not, request it
    
    // First verify if our idea of 'next' buffer is correct
    // XXXKF This is technically incorrect, it won't tell whether the next "block" that's there
    // isn't actually a single sample. I worked it around by ensuring end of blocks are always
    // at CUE_BUFFER_SIZE boundary, and this works well, but causes buffers to be of uneven size.
    if (m->pcp_next && (is_contained(m->pcp_next, pos) || is_queued(m->pcp_next, pos)))
    {
        // We're still waiting for the requested buffer, but that's OK
        return;
    }
    
    // We don't know the next buffer, or the next buffer doesn't contain
    // the sample we're looking for.
    m->pcp_next = get_queued_buffer(m, pos);
    if (!m->pcp_next)
    {
        // It hasn't even been requested - request it
        int buf_idx = get_unused_buffer(m);
        if(buf_idx == -1)
        {
            printf("Ran out of buffers\n");
            return;
        }
        request_load(m, buf_idx, pos);
        m->pcp_next = &m->cp_readahead[buf_idx];
        
        // printf("@%lld: Requested load into buffer %d at %lld\n", (long long)m->readptr, buf_idx, (long long) pos);
    }
}

static void copy_samples(struct stream_player_module *m, cbox_sample_t **outputs, float *data, int count, int ofs, int pos)
{
    int i;
    
    if (m->info.channels == 1)
    {
        for (i = 0; i < count; i++)
        {
            outputs[0][ofs + i] = outputs[1][ofs + i] = data[pos + i];
        }
    }
    else
    if (m->info.channels == 2)
    {
        for (i = 0; i < count; i++)
        {
            outputs[0][ofs + i] = data[pos << 1];
            outputs[1][ofs + i] = data[(pos << 1) + 1];
            pos++;
        }
    }
    else
    {
        uint32_t ch = m->info.channels;
        for (i = 0; i < count; i++)
        {
            outputs[0][ofs + i] = data[pos * ch];
            outputs[1][ofs + i] = data[pos * ch + 1];
            pos++;
        }
    }
    m->readptr += count;
    if (m->readptr >= (uint32_t)m->info.frames)
    {
        m->readptr = m->restart;
    }
}

void stream_player_process_block(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct stream_player_module *m = user_data;
    int i, optr;
    unsigned char buf_idx;
    
    if (m->readptr == NO_SAMPLE_LOOP)
    {
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            outputs[0][i] = outputs[1][i] = 0;
        }
        return;
    }

    // receive buffer completion messages from the queue
    while(jack_ringbuffer_read(m->rb_just_read, &buf_idx, 1))
    {
        m->cp_readahead_ready[buf_idx] = 1;
    }
    
    optr = 0;
    do {
        if (m->readptr == NO_SAMPLE_LOOP)
            break;

        if (m->pcp_current && !is_contained(m->pcp_current, m->readptr))
            m->pcp_current = NULL;
        
        if (!m->pcp_current)
        {
            if (m->pcp_next && is_contained(m->pcp_next, m->readptr))
            {
                m->pcp_current = m->pcp_next;
                m->pcp_next = NULL;
            }
            else
                m->pcp_current = get_cue(m, m->readptr);
        }
        
        if (!m->pcp_current)
        {
            printf("Underrun at %d\n", (int)m->readptr);
            // Underrun; request/wait for next block and output zeros
            request_next(m, m->readptr);
            break;
        }
        assert(!m->pcp_current->queued);
        
        uint64_t data_end = m->pcp_current->position + m->pcp_current->length;
        uint32_t data_left = data_end - m->readptr;
        
        // If we're close to running out of space, prefetch the next bit
        if (data_left < PREFETCH_THRESHOLD && data_end < m->info.frames)
            request_next(m, data_end);
        
        float *data = m->pcp_current->data;
        uint32_t pos = m->readptr - m->pcp_current->position;
        uint32_t count = data_end - m->readptr;
        if (count > CBOX_BLOCK_SIZE - optr)
            count = CBOX_BLOCK_SIZE - optr;
        
        // printf("Copy samples: copying %d, optr %d, %lld = %d @ [%lld - %lld], left %d\n", count, optr, (long long)m->readptr, pos, (long long)m->pcp_current->position, (long long)data_end, (int)data_left);
        copy_samples(m, outputs, data, count, optr, pos);
        optr += count;
    } while(optr < CBOX_BLOCK_SIZE);
    
    for (i = optr; i < CBOX_BLOCK_SIZE; i++)
    {
        outputs[0][i] = outputs[1][i] = 0;
    }
}

struct cbox_module *stream_player_create(void *user_data, const char *cfg_section)
{
    int i;
    int rest;
    static int inited = 0;
    
    if (!inited)
    {
        inited = 1;
    }
    
    struct stream_player_module *m = malloc(sizeof(struct stream_player_module));
    char *filename = cbox_config_get_string(cfg_section, "file");
    if (!filename)
    {
        g_error("%s: filename not specified", cfg_section);
        return NULL;
    }
    m->module.user_data = m;
    m->module.process_event = stream_player_process_event;
    m->module.process_block = stream_player_process_block;
    
    m->sndfile = sf_open(filename, SFM_READ, &m->info);
    
    if (!m->sndfile)
    {
        g_error("%s: cannot open file '%s': %s", cfg_section, filename, sf_strerror(NULL));
        return NULL;
    }
    g_message("Frames %d channels %d", (int)m->info.frames, (int)m->info.channels);
    
    m->rb_for_reading = jack_ringbuffer_create(MAX_READAHEAD_BUFFERS + 1);
    m->rb_just_read = jack_ringbuffer_create(MAX_READAHEAD_BUFFERS + 1);
    
    m->readptr = 0;
    m->restart = (uint64_t)(int64_t)cbox_config_get_int(cfg_section, "loop", -1);
    m->pcp_current = &m->cp_start;
    // for testing
    m->pcp_current = NULL;
    m->pcp_next = NULL;
    
    init_cue(m, &m->cp_start, CUE_BUFFER_SIZE, 0);
    load_at_cue(m, &m->cp_start);
    if (m->restart > 0 && (m->restart % CUE_BUFFER_SIZE) > 0)
        init_cue(m, &m->cp_loop, CUE_BUFFER_SIZE + (CUE_BUFFER_SIZE - (m->restart % CUE_BUFFER_SIZE)), m->restart);
    else
        init_cue(m, &m->cp_loop, CUE_BUFFER_SIZE, m->restart);
    load_at_cue(m, &m->cp_loop);
    for (i = 0; i < MAX_READAHEAD_BUFFERS; i++)
        init_cue(m, &m->cp_readahead[i], CUE_BUFFER_SIZE, NO_SAMPLE_LOOP);
    
    if (pthread_create(&m->thr_preload, NULL, sample_preload_thread, m))
    {
        g_error("Failed to create audio prefetch thread", strerror(errno));
        return NULL;
    }
    
    
    return &m->module;
}

// XXXKF not used yet, I'll add it to the API some day
void stream_player_destroy(void *user_data)
{
    struct stream_player_module *m = user_data;
    unsigned char cmd = 255;
    
    jack_ringbuffer_write(m->rb_for_reading, &cmd, 1);
    pthread_join(m->thr_preload, NULL);
    
    jack_ringbuffer_free(m->rb_for_reading);
    jack_ringbuffer_free(m->rb_just_read);
    sf_close(m->sndfile);
}

struct cbox_module_keyrange_metadata stream_player_keyranges[] = {
};

struct cbox_module_livecontroller_metadata stream_player_controllers[] = {
};

DEFINE_MODULE(stream_player, 0, 2)

