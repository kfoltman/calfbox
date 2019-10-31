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

#include "prefetch_pipe.h"
#include "tarfile.h"
#include "wavebank.h"
#include <assert.h>
#include <malloc.h>
#include <memory.h>
#include <stdlib.h>
#include <unistd.h>

// Don't bother fetching less than 4 (mono) or 8 KB (stereo)

void cbox_prefetch_pipe_init(struct cbox_prefetch_pipe *pipe, uint32_t buffer_size, uint32_t min_buffer_frames)
{
    pipe->data = malloc(buffer_size);
    pipe->buffer_size = buffer_size;
    pipe->min_buffer_frames = min_buffer_frames;
    pipe->sndfile = NULL;
    pipe->state = pps_free;
}

gboolean cbox_prefetch_pipe_openfile(struct cbox_prefetch_pipe *pipe)
{
    if (pipe->waveform->taritem)
        pipe->sndfile = cbox_tarfile_opensndfile(pipe->waveform->tarfile, pipe->waveform->taritem, &pipe->sndstream, &pipe->info);
    else
        pipe->sndfile = sf_open(pipe->waveform->canonical_name, SFM_READ, &pipe->info);
    if (!pipe->sndfile)
        return FALSE;
    pipe->file_pos_frame = sf_seek(pipe->sndfile, pipe->waveform->preloaded_frames, SEEK_SET);
    if (pipe->file_loop_end > pipe->info.frames)
        pipe->file_loop_end = pipe->info.frames;
    pipe->buffer_loop_end = pipe->buffer_size / (sizeof(int16_t) * pipe->info.channels);
    pipe->produced = pipe->file_pos_frame;
    pipe->write_ptr = 0;
    pipe->state = pps_active;
    
    return TRUE;
}

void cbox_prefetch_pipe_consumed(struct cbox_prefetch_pipe *pipe, uint32_t frames)
{
    pipe->consumed += frames;
}

void cbox_prefetch_pipe_fetch(struct cbox_prefetch_pipe *pipe)
{
    gboolean retry;
    do {
        retry = FALSE;
        // XXXKF take consumption rate into account

        // How many frames left to consume
        int32_t supply = pipe->produced - pipe->consumed;
        if (supply < 0)
        {
            // Overrun already occurred. Cut the losses by skipping already missed
            // part.
            uint32_t overrun = -supply;
            
            // XXXKF This may or may not be stupid. I didn't put much thought into it.
            pipe->produced += overrun;
            pipe->file_pos_frame = sf_seek(pipe->sndfile, overrun, SEEK_CUR);
            pipe->write_ptr += overrun;
            if (pipe->write_ptr >= pipe->buffer_loop_end)
                pipe->write_ptr %= pipe->buffer_loop_end;
        }
        //
        if ((uint32_t)supply >= pipe->buffer_loop_end)
            return;
        
        // How many frames to read to fill the full prefetch size
        int32_t readsize = pipe->buffer_loop_end - supply;
        if (readsize < (int32_t)pipe->min_buffer_frames)
            return;
        
        if (pipe->write_ptr == pipe->buffer_loop_end)
            pipe->write_ptr = 0;
        
        // If reading across buffer boundary, only read the part up to buffer
        // end, and then retry from start of the buffer.
        if (pipe->write_ptr + readsize > pipe->buffer_loop_end)
        {
            readsize = pipe->buffer_loop_end - pipe->write_ptr;
            retry = TRUE;
        }
        // If past the file loop end, restart at file loop start
        if (pipe->file_pos_frame >= pipe->file_loop_end)
        {
            if (pipe->file_loop_start == (uint32_t)-1 || (pipe->loop_count && pipe->play_count >= pipe->loop_count - 1))
            {
                pipe->finished = TRUE;
                for (int i = 0; i < readsize * pipe->info.channels; i++)
                    pipe->data[pipe->write_ptr * pipe->info.channels + i] = rand();
                break;
            }
            else
            {
                pipe->play_count++;
                pipe->file_pos_frame = pipe->file_loop_start;
                sf_seek(pipe->sndfile, pipe->file_loop_start, SEEK_SET);
            }
        }
        // If reading across file loop boundary, read up to loop end and 
        // retry to restart
        if (pipe->file_pos_frame + readsize > pipe->file_loop_end)
        {
            readsize = pipe->file_loop_end - pipe->file_pos_frame;
            retry = TRUE;
        }
        
        int32_t actread = sf_readf_short(pipe->sndfile, pipe->data + pipe->write_ptr * pipe->info.channels, readsize);
        pipe->produced += actread;
        pipe->file_pos_frame += actread;
        pipe->write_ptr += actread;
    } while(retry);
}

void cbox_prefetch_pipe_closefile(struct cbox_prefetch_pipe *pipe)
{
    assert(pipe->state == pps_closing);
    assert(pipe->sndfile);
    sf_close(pipe->sndfile);
    pipe->sndfile = NULL;
    pipe->state = pps_free;
}

void cbox_prefetch_pipe_close(struct cbox_prefetch_pipe *pipe)
{
    if (pipe->sndfile)
        cbox_prefetch_pipe_closefile(pipe);
    if (pipe->data)
    {
        free(pipe->data);
        pipe->data = NULL;
    }
}

static void *prefetch_thread(void *user_data)
{
    struct cbox_prefetch_stack *stack = user_data;
    
    while(!stack->finished)
    {
        usleep(1000);
        for (int i = 0; i < stack->pipe_count; i++)
        {
            struct cbox_prefetch_pipe *pipe = &stack->pipes[i];
            switch(pipe->state)
            {
            case pps_free:
            case pps_finished:
            case pps_error:
                break;
            case pps_opening:
                if (!cbox_prefetch_pipe_openfile(pipe))
                    pipe->state = pps_error;
                assert(pipe->state != pps_opening);
                break;
            case pps_active:
                if (pipe->returned)
                    pipe->state = pps_closing;
                else
                    cbox_prefetch_pipe_fetch(pipe);
                break;
            case pps_closing:
                cbox_prefetch_pipe_closefile(pipe);
                break;
            default:
                break;
            }
        }
    }
    return 0;
}

struct cbox_prefetch_stack *cbox_prefetch_stack_new(int npipes, uint32_t buffer_size, uint32_t min_buffer_frames)
{
    struct cbox_prefetch_stack *stack = calloc(1, sizeof(struct cbox_prefetch_stack));
    stack->pipes = calloc(npipes, sizeof(struct cbox_prefetch_pipe));
    stack->next_free_pipe = calloc(npipes, sizeof(int));
    
    for (int i = 0; i < npipes; i++)
    {
        cbox_prefetch_pipe_init(&stack->pipes[i], buffer_size, min_buffer_frames);
        stack->next_free_pipe[i] = i - 1;
    }
    stack->pipe_count = npipes;
    stack->last_free_pipe = npipes - 1;
    stack->finished = FALSE;
    
    if (pthread_create(&stack->thr_prefetch, NULL, prefetch_thread, stack))
    {
        // XXXKF set thread priority
        g_warning("Cannot create a prefetch thread. Exiting.\n");
        return NULL;
    }
    
    return stack;
}

struct cbox_prefetch_pipe *cbox_prefetch_stack_pop(struct cbox_prefetch_stack *stack, struct cbox_waveform *waveform, uint32_t file_loop_start, uint32_t file_loop_end, uint32_t loop_count)
{
    // The stack may include some pipes that are already returned but not yet 
    // fully prepared for opening a new file
    int *ppos = &stack->last_free_pipe;
    while(*ppos != -1 && stack->pipes[*ppos].state != pps_free)
        ppos = &stack->next_free_pipe[*ppos];
    if (*ppos == -1) {
        for (int i = 0; i < stack->pipe_count; ++i) {
            printf("Pipe %d state %d next-free %d\n", i, stack->pipes[i].state, stack->next_free_pipe[i]);
        }
        printf("last_free_pipe %d\n", stack->last_free_pipe);
        return NULL;
    }
    
    int pos = *ppos;
    struct cbox_prefetch_pipe *pipe = &stack->pipes[pos];
    
    *ppos = stack->next_free_pipe[pos];
    stack->next_free_pipe[pos] = -1;
    
    pipe->waveform = waveform;
    if (file_loop_start == (uint32_t)-1 && loop_count)
        file_loop_start = 0;
    pipe->file_loop_start = file_loop_start;
    pipe->file_loop_end = file_loop_end;
    pipe->buffer_loop_end = 0;
    pipe->finished = FALSE;
    pipe->returned = FALSE;
    pipe->produced = waveform->preloaded_frames;
    pipe->consumed = 0;
    pipe->play_count = 0;
    pipe->loop_count = loop_count;
    
    __sync_synchronize();
    pipe->state = pps_opening;
    return pipe;
}

void cbox_prefetch_stack_push(struct cbox_prefetch_stack *stack, struct cbox_prefetch_pipe *pipe)
{
    switch(pipe->state)
    {
    case pps_free:
        assert(0);
        break;
    case pps_error:
    case pps_closed:
        pipe->state = pps_free;
        break;
    case pps_opening:
        // Close the file as soon as open operation completes
        pipe->returned = TRUE;
        break;
    default:
        pipe->state = pps_closing;
        break;
    }

    __sync_synchronize();

    int pos = pipe - stack->pipes;
    assert(stack->next_free_pipe[pos] == -1);
    stack->next_free_pipe[pos] = stack->last_free_pipe;
    stack->last_free_pipe = pos;
    
    __sync_synchronize();
}

int cbox_prefetch_stack_get_active_pipe_count(struct cbox_prefetch_stack *stack)
{
    int count = 0;
    for (int i = 0; i < stack->pipe_count; i++)
    {
        if (stack->pipes[i].state != pps_free)
            count++;
    }
    return count;
}

void cbox_prefetch_stack_destroy(struct cbox_prefetch_stack *stack)
{
    void *result = NULL;
    stack->finished = TRUE;
    pthread_join(stack->thr_prefetch, &result);
    for (int i = 0; i < stack->pipe_count; i++)
        cbox_prefetch_pipe_close(&stack->pipes[i]);
    free(stack->next_free_pipe);
    free(stack->pipes);
    free(stack);
}
