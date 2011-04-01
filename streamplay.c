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

#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "module.h"
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

struct stream_player_module
{
    struct cbox_module module;

    SNDFILE *sndfile;
    SF_INFO info;
    float *data;
    uint32_t readptr;
    uint32_t restart;
};

void stream_player_process_event(void *user_data, const uint8_t *data, uint32_t len)
{
    struct stream_player_module *m = user_data;
}

void stream_player_process_block(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct stream_player_module *m = user_data;
    int i;
    
    if (m->readptr >= (uint32_t)m->info.frames)
    {
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            outputs[0][i] = outputs[1][i] = 0;
        }
        return;
    }

    uint32_t pos = m->readptr;
    uint32_t count = m->info.frames - m->readptr;
    if (count > CBOX_BLOCK_SIZE)
        count = CBOX_BLOCK_SIZE;
    
    if (m->info.channels == 1)
    {
        for (i = 0; i < count; i++)
        {
            outputs[0][i] = outputs[1][i] = m->data[pos + i];
        }
    }
    else
    if (m->info.channels == 2)
    {
        for (i = 0; i < count; i++)
        {
            outputs[0][i] = m->data[pos << 1];
            outputs[1][i] = m->data[(pos << 1)];
            pos++;
        }
    }
    else
    {
        uint32_t ch = m->info.channels;
        for (i = 0; i < count; i++)
        {
            outputs[0][i] = m->data[pos * ch];
            outputs[1][i] = m->data[pos * ch + 1];
            pos++;
        }
    }
    m->readptr += count;
    if (m->readptr >= (uint32_t)m->info.frames)
    {
        m->readptr = m->restart;
    }
}

struct cbox_module *stream_player_create(void *user_data, const char *cfg_section)
{
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
    
    m->data = malloc(m->info.frames * m->info.channels * sizeof(float));
    
    sf_readf_float(m->sndfile, m->data, m->info.frames);
    
    sf_close(m->sndfile);
    
    m->readptr = 0;
    m->restart = 0;
    
    return &m->module;
}


struct cbox_module_keyrange_metadata stream_player_keyranges[] = {
};

struct cbox_module_livecontroller_metadata stream_player_controllers[] = {
};

DEFINE_MODULE(stream_player, 0, 2)

