/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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

#include "config-api.h"
#include "dspmath.h"
#include "module.h"
#include <glib.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

static void sampler_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs);
static void sampler_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len);
static void sampler_destroy(struct cbox_module *module);

#define MAX_SAMPLER_VOICES 128

enum sample_player_type
{
    spt_inactive,
    spt_mono16,
    spt_stereo16
};

struct sampler_layer
{
    enum sample_player_type mode;
    int16_t *sample_data;
    uint32_t sample_offset;
    uint32_t loop_start;
    uint32_t loop_end;
    float gain;
    float pan;
    float freq;
};

struct sampler_voice
{
    enum sample_player_type mode;
    int16_t *sample_data;
    uint32_t pos, delta, loop_start, loop_end;
    uint32_t frac_pos, frac_delta;
    int note;
    int vel;
    int released;
    float gain;
    float pan;
};

struct sampler_module
{
    struct cbox_module module;

    int srate;
    struct sampler_voice voices[MAX_SAMPLER_VOICES];
    struct sampler_layer *layers;
    int layer_count;
};

static void process_voice_mono(struct sampler_voice *v, float **channels)
{
    if (v->released)
    {
        v->mode = spt_inactive;
        return;
    }
    float lgain = v->gain * (1 - v->pan) / 32768.0;
    float rgain = v->gain * v->pan / 32768.0;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        if (v->pos >= v->loop_end)
        {
            if (v->loop_start == (uint32_t)-1)
            {
                v->mode = spt_inactive;
                break;
            }
            v->pos = v->pos - v->loop_end + v->loop_start;
        }
        
        float fr = (v->frac_pos >> 8) * (1.0 / (256.0 * 65536.0));
        uint32_t nextsample = v->pos + 1;
        if (nextsample >= v->loop_end && v->loop_start != (uint32_t)-1)
            nextsample -= v->loop_start;
        
        float sample = fr * v->sample_data[nextsample] + (1 - fr) * v->sample_data[v->pos];
        
        if (v->frac_pos > ~v->frac_delta)
            v->pos++;
        v->frac_pos += v->frac_delta;
        v->pos += v->delta;
        
        channels[0][i] += sample * lgain;
        channels[1][i] += sample * rgain;
    }
}

static void process_voice_stereo(struct sampler_voice *v, float **channels)
{
    if (v->released)
    {
        v->mode = spt_inactive;
        return;
    }
    float lgain = v->gain * (1 - v->pan) / 32768.0;
    float rgain = v->gain * v->pan / 32768.0;
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        if (v->pos >= v->loop_end)
        {
            if (v->loop_start == (uint32_t)-1)
            {
                v->mode = spt_inactive;
                break;
            }
            v->pos = v->pos - v->loop_end + v->loop_start;
        }
        
        float fr = (v->frac_pos >> 8) * (1.0 / (256.0 * 65536.0));
        uint32_t nextsample = v->pos + 1;
        if (nextsample >= v->loop_end && v->loop_start != (uint32_t)-1)
            nextsample -= v->loop_start;
        
        float lsample = fr * v->sample_data[nextsample << 1] + (1 - fr) * v->sample_data[v->pos << 1];
        float rsample = fr * v->sample_data[1 + (nextsample << 1)] + (1 - fr) * v->sample_data[1 + (v->pos << 1)];
        
        if (v->frac_pos > ~v->frac_delta)
            v->pos++;
        v->frac_pos += v->frac_delta;
        v->pos += v->delta;
        
        channels[0][i] += lsample * lgain;
        channels[1][i] += rsample * rgain;
    }
}

void sampler_start_note(struct sampler_module *m, int note, int vel)
{
    struct sampler_layer *l = m->layers;
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        if (m->voices[i].mode == spt_inactive)
        {
            struct sampler_voice *v = &m->voices[i];
            
            double freq = l->freq * pow(2.0, (note - 69) / 12.0);
            uint64_t freq64 = freq * 65536.0 * 65536.0 / m->srate;
            
            v->sample_data = l->sample_data;
            v->pos = l->sample_offset;
            v->frac_pos = 0;
            v->loop_start = l->loop_start;
            v->loop_end = l->loop_end;
            v->delta = freq64 >> 32;
            v->frac_delta = freq64 & 0xFFFFFFFF;
            v->gain = l->gain * vel / 127.0;
            v->pan = l->pan;
            v->note = note;
            v->vel = vel;
            v->mode = l->mode;
            v->released = 0;
            break;
        }
    }
}

void sampler_stop_note(struct sampler_module *m, int note, int vel)
{
    struct sampler_layer *l = m->layers;
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices[i];
        if (v->mode != spt_inactive && v->note == note)
            v->released = 1;
    }
}

void sampler_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct sampler_module *m = (struct sampler_module *)module;
    
    //float channels[2][CBOX_BLOCK_SIZE];
    
    for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        outputs[0][i] = outputs[1][i] = 0.f;
    
    for (int i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        if (m->voices[i].mode != spt_inactive)
        {
            if (m->voices[i].mode == spt_stereo16)
                process_voice_stereo(&m->voices[i], outputs);
            else
                process_voice_mono(&m->voices[i], outputs);
        }
    }    
}

void sampler_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct sampler_module *m = (struct sampler_module *)module;
    if (len > 0)
    {
        int cmd = data[0] >> 4;
        int chn = data[0] & 15;
        switch(cmd)
        {
            case 8:
                sampler_stop_note(m, data[1], data[2]);
                break;

            case 9:
                if (data[2] > 0)
                    sampler_start_note(m, data[1], data[2]);
                break;
            
            case 10:
                // polyphonic pressure not handled
                break;
            
            case 11:
                // cc
                break;

            case 12:
                // pc
                break;

            case 13:
                // ca
                break;

            case 14:
                // pb
                break;

            }
    }
}

struct cbox_module *sampler_create(void *user_data, const char *cfg_section, int srate)
{
    int result = 0;
    int i;
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct sampler_module *m = malloc(sizeof(struct sampler_module));
    cbox_module_init(&m->module, m);
    m->module.process_event = sampler_process_event;
    m->module.process_block = sampler_process_block;
    m->module.destroy = sampler_destroy;
    m->srate = srate;
    
    char *filename = cbox_config_get_string(cfg_section, "file");
    SF_INFO info;
    SNDFILE *sndfile = sf_open(filename, SFM_READ, &info);
    if (!sndfile)
    {
        g_error("%s: cannot open file '%s': %s", cfg_section, filename, sf_strerror(NULL));
        return NULL;
    }
    if (info.channels != 1 && info.channels != 2)
    {
        g_error("%s: cannot open file '%s': unsupported channel count %d", cfg_section, filename, (int)info.channels);
        return NULL;
    }
    
    m->layers = malloc(sizeof(struct sampler_layer) * 1);
    m->layer_count = 1;
    m->layers[0].sample_data = malloc(info.channels * 2 * (info.frames + 1));
    m->layers[0].sample_offset = 0;
    m->layers[0].freq = info.samplerate ? info.samplerate : 44100;
    m->layers[0].loop_start = -1;
    m->layers[0].loop_end = info.frames;
    m->layers[0].gain = 1;
    m->layers[0].pan = 0.5;
    m->layers[0].mode = info.channels == 2 ? spt_stereo16 : spt_mono16;
    for (i = 0; i < info.channels * info.frames; i++)
        m->layers[0].sample_data[i] = 0;
    sf_readf_short(sndfile, m->layers[0].sample_data, info.frames);
    sf_close(sndfile);
    
    for (i = 0; i < MAX_SAMPLER_VOICES; i++)
        m->voices[i].mode = spt_inactive;
    
    return &m->module;
}

void sampler_destroy(struct cbox_module *module)
{
    struct sampler_module *m = (struct sampler_module *)module;
    
}

struct cbox_module_livecontroller_metadata sampler_controllers[] = {
};

struct cbox_module_keyrange_metadata sampler_keyranges[] = {
};

DEFINE_MODULE(sampler, 0, 2)

