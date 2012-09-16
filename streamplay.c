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
#include "rt.h"
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

#define CBOX_STREAM_PLAYER_ERROR cbox_stream_player_error_quark()

enum CboxStreamPlayerError
{
    CBOX_STREAM_PLAYER_ERROR_FAILED,
};

GQuark cbox_stream_player_error_quark()
{
    return g_quark_from_string("cbox-stream-player-error-quark");
}

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

enum stream_state_phase
{
    STOPPED,
    PLAYING,
    STOPPING,
    STARTING
};

struct stream_state
{
    SNDFILE *sndfile;
    SF_INFO info;
    uint64_t readptr;
    uint64_t restart;
    uint64_t readptr_new;
    
    volatile int buffer_in_use;
    
    struct stream_player_cue_point cp_start, cp_loop, cp_readahead[MAX_READAHEAD_BUFFERS];
    int cp_readahead_ready[MAX_READAHEAD_BUFFERS];
    struct stream_player_cue_point *pcp_current, *pcp_next;
    
    jack_ringbuffer_t *rb_for_reading, *rb_just_read;
    float gain, fade_gain, fade_increment;
    enum stream_state_phase phase;

    pthread_t thr_preload;
    int thread_started;
    
    gchar *filename;
};

struct stream_player_module
{
    struct cbox_module module;
    
    struct stream_state *stream;
    float fade_increment;
};

static void init_cue(struct stream_state *ss, struct stream_player_cue_point *pt, uint32_t size, uint64_t pos)
{
    pt->data = malloc(size * sizeof(float) * ss->info.channels);
    pt->size = size;
    pt->length = 0;
    pt->queued = 0;
    pt->position = pos;
}

static void load_at_cue(struct stream_state *ss, struct stream_player_cue_point *pt)
{
    if (pt->position != NO_SAMPLE_LOOP)
    {
        sf_seek(ss->sndfile, pt->position, 0);
        pt->length = sf_readf_float(ss->sndfile, pt->data, pt->size);
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

struct stream_player_cue_point *get_cue(struct stream_state *ss, uint64_t pos)
{
    int i;
    
    if (is_contained(&ss->cp_loop, pos))
        return &ss->cp_loop;
    if (is_contained(&ss->cp_start, pos))
        return &ss->cp_start;
    
    for (i = 0; i < MAX_READAHEAD_BUFFERS; i++)
    {
        if (ss->cp_readahead_ready[i] && is_contained(&ss->cp_readahead[i], pos))
            return &ss->cp_readahead[i];
    }
    return NULL;
}

struct stream_player_cue_point *get_queued_buffer(struct stream_state *ss, uint64_t pos)
{
    int i;
    
    for (i = 0; i < MAX_READAHEAD_BUFFERS; i++)
    {
        if (!ss->cp_readahead_ready[i] && is_queued(&ss->cp_readahead[i], pos))
            return &ss->cp_readahead[i];
    }
    return NULL;
}

void request_load(struct stream_state *ss, int buf_idx, uint64_t pos)
{
    unsigned char cidx = (unsigned char)buf_idx;
    struct stream_player_cue_point *pt = &ss->cp_readahead[buf_idx];
    int wlen = 0;
    
    ss->cp_readahead_ready[buf_idx] = 0;    
    pt->position = pos;
    pt->length = 0;
    pt->queued = 1;

    wlen = jack_ringbuffer_write(ss->rb_for_reading, &cidx, 1);
    assert(wlen);
}

int get_unused_buffer(struct stream_state *ss)
{
    int i = 0;
    int notbad = -1;
    
    // return first buffer that is not currently played or in queue; XXXKF this is a very primitive strategy, a good one would at least use the current play position
    for (i = 0; i < MAX_READAHEAD_BUFFERS; i++)
    {
        int64_t rel;
        if (&ss->cp_readahead[i] == ss->pcp_current)
            continue;
        if (ss->cp_readahead[i].queued)
            continue;
        // If there's any unused buffer, return it
        if (ss->cp_readahead[i].position == NO_SAMPLE_LOOP)
            return i;
        // If this has already been played, return it
        rel = ss->readptr - ss->cp_readahead[i].position;
        if (rel >= ss->cp_readahead[i].length)
            return i;
        // Use as second chance
        notbad = i;
    }
    return notbad;
}

static void *sample_preload_thread(void *user_data)
{
    struct stream_state *ss = user_data;
    
    do {
        unsigned char buf_idx;
        if (!jack_ringbuffer_read(ss->rb_for_reading, &buf_idx, 1))
        {
            usleep(5000);
            continue;
        }
        if (buf_idx == 255)
            break;
        // fprintf(stderr, "Preload: %d, %lld\n", (int)buf_idx, (long long)m->cp_readahead[buf_idx].position);
        load_at_cue(ss, &ss->cp_readahead[buf_idx]);
        // fprintf(stderr, "Preloaded\n", (int)buf_idx, (long long)m->cp_readahead[buf_idx].position);
        jack_ringbuffer_write(ss->rb_just_read, &buf_idx, 1);
    } while(1);
        
}

void stream_player_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct stream_player_module *m = (struct stream_player_module *)module;
}

static void request_next(struct stream_state *ss, uint64_t pos)
{
    // Check if we've requested a next buffer, if not, request it
    
    // First verify if our idea of 'next' buffer is correct
    // XXXKF This is technically incorrect, it won't tell whether the next "block" that's there
    // isn't actually a single sample. I worked it around by ensuring end of blocks are always
    // at CUE_BUFFER_SIZE boundary, and this works well, but causes buffers to be of uneven size.
    if (ss->pcp_next && (is_contained(ss->pcp_next, pos) || is_queued(ss->pcp_next, pos)))
    {
        // We're still waiting for the requested buffer, but that's OK
        return;
    }
    
    // We don't know the next buffer, or the next buffer doesn't contain
    // the sample we're looking for.
    ss->pcp_next = get_queued_buffer(ss, pos);
    if (!ss->pcp_next)
    {
        // It hasn't even been requested - request it
        int buf_idx = get_unused_buffer(ss);
        if(buf_idx == -1)
        {
            printf("Ran out of buffers\n");
            return;
        }
        request_load(ss, buf_idx, pos);
        ss->pcp_next = &ss->cp_readahead[buf_idx];
        
        // printf("@%lld: Requested load into buffer %d at %lld\n", (long long)m->readptr, buf_idx, (long long) pos);
    }
}

static void copy_samples(struct stream_state *ss, cbox_sample_t **outputs, float *data, int count, int ofs, int pos)
{
    int i;
    float gain = ss->gain * ss->fade_gain;
    if (ss->phase == STARTING)
    {
        ss->fade_gain += ss->fade_increment;
        if (ss->fade_gain >= 1)
        {
            ss->fade_gain = 1;
            ss->phase = PLAYING;
        }
    }
    else
    if (ss->phase == STOPPING)
    {
        ss->fade_gain -= ss->fade_increment;
        if (ss->fade_gain < 0)
        {
            ss->fade_gain = 0;
            ss->phase = STOPPED;
        }
    }
    float new_gain = ss->gain * ss->fade_gain;
    float gain_delta = (new_gain - gain) * (1.0 / CBOX_BLOCK_SIZE);
    
    if (ss->info.channels == 1)
    {
        for (i = 0; i < count; i++)
        {
            outputs[0][ofs + i] = outputs[1][ofs + i] = gain * data[pos + i];
            gain += gain_delta;
        }
    }
    else
    if (ss->info.channels == 2)
    {
        for (i = 0; i < count; i++)
        {
            outputs[0][ofs + i] = gain * data[pos << 1];
            outputs[1][ofs + i] = gain * data[(pos << 1) + 1];
            gain += gain_delta;
            pos++;
        }
    }
    else
    {
        uint32_t ch = ss->info.channels;
        for (i = 0; i < count; i++)
        {
            outputs[0][ofs + i] = gain * data[pos * ch];
            outputs[1][ofs + i] = gain * data[pos * ch + 1];
            gain += gain_delta;
            pos++;
        }
    }
    ss->readptr += count;
    if (ss->readptr >= (uint32_t)ss->info.frames)
    {
        ss->readptr = ss->restart;
    }
}

void stream_player_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct stream_player_module *m = (struct stream_player_module *)module;
    struct stream_state *ss = m->stream;
    int i, optr;
    unsigned char buf_idx;
    
    if (!ss || ss->readptr == NO_SAMPLE_LOOP)
    {
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            outputs[0][i] = outputs[1][i] = 0;
        }
        return;
    }

    // receive buffer completion messages from the queue
    while(jack_ringbuffer_read(ss->rb_just_read, &buf_idx, 1))
    {
        ss->cp_readahead_ready[buf_idx] = 1;
    }
    
    optr = 0;
    do {
        if (ss->phase == STOPPED)
            break;
        if (ss->readptr == NO_SAMPLE_LOOP)
        {
            ss->phase = STOPPED;
            break;
        }

        if (ss->pcp_current && !is_contained(ss->pcp_current, ss->readptr))
            ss->pcp_current = NULL;
        
        if (!ss->pcp_current)
        {
            if (ss->pcp_next && is_contained(ss->pcp_next, ss->readptr))
            {
                ss->pcp_current = ss->pcp_next;
                ss->pcp_next = NULL;
            }
            else
                ss->pcp_current = get_cue(ss, ss->readptr);
        }
        
        if (!ss->pcp_current)
        {
            printf("Underrun at %d\n", (int)ss->readptr);
            // Underrun; request/wait for next block and output zeros
            request_next(ss, ss->readptr);
            break;
        }
        assert(!ss->pcp_current->queued);
        
        uint64_t data_end = ss->pcp_current->position + ss->pcp_current->length;
        uint32_t data_left = data_end - ss->readptr;
        
        // If we're close to running out of space, prefetch the next bit
        if (data_left < PREFETCH_THRESHOLD && data_end < ss->info.frames)
            request_next(ss, data_end);
        
        float *data = ss->pcp_current->data;
        uint32_t pos = ss->readptr - ss->pcp_current->position;
        uint32_t count = data_end - ss->readptr;
        if (count > CBOX_BLOCK_SIZE - optr)
            count = CBOX_BLOCK_SIZE - optr;
        
        // printf("Copy samples: copying %d, optr %d, %lld = %d @ [%lld - %lld], left %d\n", count, optr, (long long)m->readptr, pos, (long long)m->pcp_current->position, (long long)data_end, (int)data_left);
        copy_samples(ss, outputs, data, count, optr, pos);
        optr += count;
    } while(optr < CBOX_BLOCK_SIZE);
    
    for (i = optr; i < CBOX_BLOCK_SIZE; i++)
    {
        outputs[0][i] = outputs[1][i] = 0;
    }
}

static void stream_state_destroy(struct stream_state *ss)
{
    unsigned char cmd = 255;
    
    if (ss->rb_for_reading && ss->thread_started)
    {
        jack_ringbuffer_write(ss->rb_for_reading, &cmd, 1);
        pthread_join(ss->thr_preload, NULL);
    }
    
    if (ss->rb_for_reading)
        jack_ringbuffer_free(ss->rb_for_reading);
    if (ss->rb_just_read)
        jack_ringbuffer_free(ss->rb_just_read);
    if (ss->sndfile)
        sf_close(ss->sndfile);
    if (ss->filename)
        g_free(ss->filename);
    free(ss);
}

void stream_player_destroyfunc(struct cbox_module *module)
{
    struct stream_player_module *m = (struct stream_player_module *)module;
    if (m->stream)
        stream_state_destroy(m->stream);
}

static struct stream_state *stream_state_new(const char *context, const gchar *filename, uint64_t loop, float fade_increment, GError **error)
{
    struct stream_state *stream = malloc(sizeof(struct stream_state));
    memset(&stream->info, 0, sizeof(stream->info));
    stream->sndfile = sf_open(filename, SFM_READ, &stream->info);
    
    if (!stream->sndfile)
    {
        g_set_error(error, CBOX_STREAM_PLAYER_ERROR, CBOX_STREAM_PLAYER_ERROR_FAILED, "instrument '%s': cannot open file '%s': %s", context, filename, sf_strerror(NULL));
        free(stream);
        return NULL;
    }
    g_message("Frames %d channels %d", (int)stream->info.frames, (int)stream->info.channels);
    
    stream->rb_for_reading = jack_ringbuffer_create(MAX_READAHEAD_BUFFERS + 1);
    stream->rb_just_read = jack_ringbuffer_create(MAX_READAHEAD_BUFFERS + 1);
    
    stream->phase = STOPPED;
    stream->readptr = 0;
    stream->restart = loop;
    stream->pcp_current = &stream->cp_start;
    stream->pcp_next = NULL;
    stream->gain = 1.0;
    stream->fade_gain = 0.0;
    stream->fade_increment = fade_increment;
    stream->thread_started = 0;
    stream->filename = g_strdup(filename);
    
    init_cue(stream, &stream->cp_start, CUE_BUFFER_SIZE, 0);
    load_at_cue(stream, &stream->cp_start);
    if (stream->restart > 0 && (stream->restart % CUE_BUFFER_SIZE) > 0)
        init_cue(stream, &stream->cp_loop, CUE_BUFFER_SIZE + (CUE_BUFFER_SIZE - (stream->restart % CUE_BUFFER_SIZE)), stream->restart);
    else
        init_cue(stream, &stream->cp_loop, CUE_BUFFER_SIZE, stream->restart);
    load_at_cue(stream, &stream->cp_loop);
    for (int i = 0; i < MAX_READAHEAD_BUFFERS; i++)
    {
        init_cue(stream, &stream->cp_readahead[i], CUE_BUFFER_SIZE, NO_SAMPLE_LOOP);
        stream->cp_readahead_ready[i] = 0;
    }
    if (pthread_create(&stream->thr_preload, NULL, sample_preload_thread, stream))
    {
        stream_state_destroy(stream);
        g_set_error(error, CBOX_STREAM_PLAYER_ERROR, CBOX_STREAM_PLAYER_ERROR_FAILED, "cannot create streaming thread: %s", strerror(errno));
        return NULL;
    }
    stream->thread_started = 1;
    
    return stream;
}

///////////////////////////////////////////////////////////////////////////////////

static int stream_player_seek_execute(void *p)
{
    struct stream_player_module *m = p;
    
    m->stream->readptr = m->stream->readptr_new;
    
    return 1;
}

static struct cbox_rt_cmd_definition stream_seek_command = {
    .prepare = NULL,
    .execute = stream_player_seek_execute,
    .cleanup = NULL
};

///////////////////////////////////////////////////////////////////////////////////

static int stream_player_play_execute(void *p)
{
    struct stream_player_module *m = p;
    
    if (m->stream->readptr == NO_SAMPLE_LOOP)
        m->stream->readptr = 0;
    if (m->stream->phase != PLAYING)
    {
        if (m->stream->readptr == 0)
        {
            m->stream->fade_gain = 1.0;
            m->stream->phase = PLAYING;
        }
        else
            m->stream->phase = STARTING;
    }
    return 1;
}

static struct cbox_rt_cmd_definition stream_play_command = {
    .prepare = NULL,
    .execute = stream_player_play_execute,
    .cleanup = NULL
};

///////////////////////////////////////////////////////////////////////////////////

static int stream_player_stop_execute(void *p)
{
    struct stream_player_module *m = p;
    
    if (m->stream->phase != STOPPED)
        m->stream->phase = STOPPING;
    return 1;
}

static struct cbox_rt_cmd_definition stream_stop_command = {
    .prepare = NULL,
    .execute = stream_player_stop_execute,
    .cleanup = NULL
};

///////////////////////////////////////////////////////////////////////////////////

struct load_command_data
{
    struct stream_player_module *module;
    gchar *context;
    gchar *filename;
    int loop_start;
    struct stream_state *stream, *old_stream;
    GError **error;
};

static int stream_player_load_prepare(void *p)
{
    struct load_command_data *c = p;
    
    if (!c->filename)
        return 0;
    c->stream = stream_state_new(c->context, c->filename, c->loop_start, c->module->fade_increment, c->error);
    c->old_stream = NULL;
    if (!c->stream)
    {
        g_free(c->filename);
        return -1;
    }
    return 0;
}

static int stream_player_load_execute(void *p)
{
    struct load_command_data *c = p;
    
    c->old_stream = c->module->stream;
    c->module->stream = c->stream;
    return 1;
}

static void stream_player_load_cleanup(void *p)
{
    struct load_command_data *c = p;
    
    if (c->filename)
        g_free(c->filename);
    if (c->old_stream && c->old_stream != c->stream)
        stream_state_destroy(c->old_stream);
}

static struct cbox_rt_cmd_definition stream_load_command = {
    .prepare = stream_player_load_prepare,
    .execute = stream_player_load_execute,
    .cleanup = stream_player_load_cleanup
};

///////////////////////////////////////////////////////////////////////////////////

static gboolean require_stream(struct stream_player_module *m, GError **error)
{
    if (!m->stream)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No stream loaded");
        return FALSE;
    }
    return TRUE;
}

gboolean stream_player_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct stream_player_module *m = (struct stream_player_module *)ct->user_data;
    if (!strcmp(cmd->command, "/seek") && !strcmp(cmd->arg_types, "i"))
    {
        if (!require_stream(m, error))
            return FALSE;
        m->stream->readptr_new = *(int *)cmd->arg_values[0];
        cbox_rt_execute_cmd_async(m->module.rt, &stream_seek_command, m);
    }
    else if (!strcmp(cmd->command, "/play") && !strcmp(cmd->arg_types, ""))
    {
        if (!require_stream(m, error))
            return FALSE;
        cbox_rt_execute_cmd_async(m->module.rt, &stream_play_command, m);
    }
    else if (!strcmp(cmd->command, "/stop") && !strcmp(cmd->arg_types, ""))
    {
        if (!require_stream(m, error))
            return FALSE;
        cbox_rt_execute_cmd_async(m->module.rt, &stream_stop_command, m);
    }
    else if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (m->stream)
        {
            return cbox_execute_on(fb, NULL, "/filename", "s", error, m->stream->filename) &&
                cbox_execute_on(fb, NULL, "/pos", "i", error, m->stream->readptr) &&
                cbox_execute_on(fb, NULL, "/length", "i", error, m->stream->info.frames) &&
                cbox_execute_on(fb, NULL, "/playing", "i", error, m->stream->phase != STOPPED);
        }
        else
            return
                cbox_execute_on(fb, NULL, "/filename", "s", error, "");
    }
    else if (!strcmp(cmd->command, "/load") && !strcmp(cmd->arg_types, "si"))
    {
        struct load_command_data *c = malloc(sizeof(struct load_command_data));
        c->context = m->module.instance_name;
        c->module = m;
        c->stream = NULL;
        c->old_stream = NULL;
        c->filename = g_strdup((gchar *)cmd->arg_values[0]);
        c->loop_start = *(int *)cmd->arg_values[1];
        c->error = error;
        cbox_rt_execute_cmd_sync(m->module.rt, &stream_load_command, c);
        gboolean success = c->stream != NULL;
        free(c);
        return success;
    }
    else if (!strcmp(cmd->command, "/unload") && !strcmp(cmd->arg_types, ""))
    {
        struct load_command_data *c = malloc(sizeof(struct load_command_data));
        c->context = m->module.instance_name;
        c->module = m;
        c->stream = NULL;
        c->old_stream = NULL;
        c->filename = NULL;
        c->loop_start = 0;
        c->error = error;
        cbox_rt_execute_cmd_sync(m->module.rt, &stream_load_command, c);
        gboolean success = c->stream == NULL;
        free(c);
        return success;        
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown command '%s'", cmd->command);
        return FALSE;
    }
    return TRUE;
}

MODULE_CREATE_FUNCTION(stream_player)
{
    int rest;
    static int inited = 0;
    
    if (!inited)
    {
        inited = 1;
    }
    
    struct stream_player_module *m = malloc(sizeof(struct stream_player_module));
    gchar *filename = cbox_config_get_string(cfg_section, "file");
    CALL_MODULE_INIT(m, 0, 2, stream_player);
    m->module.process_event = stream_player_process_event;
    m->module.process_block = stream_player_process_block;
    m->fade_increment = 1.0 / (cbox_config_get_float(cfg_section, "fade_time", 0.01) * (m->module.srate / CBOX_BLOCK_SIZE));
    if (filename)
    {
        m->stream = stream_state_new(cfg_section, filename, (uint64_t)(int64_t)cbox_config_get_int(cfg_section, "loop", -1), m->fade_increment, error);
        if (!m->stream)
        {
            CBOX_DELETE(&m->module);
            return NULL;
        }
    }
    else
        m->stream = NULL;
    
    return &m->module;
}

struct cbox_module_keyrange_metadata stream_player_keyranges[] = {
};

struct cbox_module_livecontroller_metadata stream_player_controllers[] = {
};

DEFINE_MODULE(stream_player, 0, 2)

