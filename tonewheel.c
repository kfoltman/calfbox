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

#include "dspmath.h"
#include "module.h"
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

// a0 a1 a2 b1 b2 for scanner vibrato filter @4kHz with sr=44.1:    0.057198 0.114396 0.057198 -1.218829 0.447620

static int64_t scanner_a0 = (int64_t)(0.057198 * 1048576);
static int64_t scanner_b1 = (int64_t)(-1.218829 * 1048576);
static int64_t scanner_b2 = (int64_t)(0.447620 * 1048576);

static int sine_table[2048];
static int complex_table[2048];
static int distortion_table[8192];

struct biquad
{
    int x1;
    int y1;
    int x2;
    int y2;
};

struct tonewheel_organ_module
{
    struct cbox_module module;

    uint32_t frequency[91];
    uint32_t phase[91];
    uint32_t keymasks[4];
    int amp_scaling[91];
    struct biquad scanner_delay[18];
    int lowpass_x1, lowpass_y1;
    float percussion;
    int do_filter;
    int cc91;
    uint32_t vibrato_phase;
};

static const int drawbars[9] = {0, 19, 12, 24, 24 + 7, 36, 36 + 4, 36 + 7, 48};
static int pedal_drawbar_settings[2] = {8, 2};
static int manual_drawbar_settings[9] = {8, 3, 8, 0, 0, 0, 0, 0, 3};

void tonewheel_organ_process_event(void *user_data, const uint8_t *data, uint32_t len)
{
    struct tonewheel_organ_module *m = user_data;
    if (len > 0)
    {
        if (data[0] == 0x90)
        {
            int key = data[1] & 127;
            m->keymasks[key >> 5] |= 1 << (key & 31);
            if (m->percussion < 0 && key >= 36)
                m->percussion = 16.0;
        }
        if (data[0] == 0x80)
        {
            int key = data[1] & 127;
            m->keymasks[key >> 5] &= ~(1 << (key & 31));
            
            if (!(m->keymasks[1] & 0xFFFFFFF0) && !m->keymasks[2] && !m->keymasks[3])
                m->percussion = -1;
        }
        if (data[0] == 0xB0)
        {
            if (data[1] >= 21 && data[1] <= 28)
                manual_drawbar_settings[data[1] - 21] = data[2] * 8 / 127;
            if (data[1] == 82)
                manual_drawbar_settings[8] = data[2] * 8 / 127;
            if (data[1] == 64)
                m->do_filter = data[2] >= 64;
            if (data[1] == 91)
                m->cc91 = data[2];
        }
    }
}

inline int check_keymask(uint32_t *keymasks, int note)
{
    if (note < 0 || note > 127)
        return 0;
    return 0 != (keymasks[note >> 5] & (1 << (note & 31)));
}

inline int tonegenidx_pedals(int note, int shift)
{
    if (note < 24 || note > 24 + 11)
        return 91;
    
    note -= 24;
    return note + shift;
}

inline int tonegenidx(int note, int shift)
{
    // ignore everything below the lowest key
    if (note < 36)
        return 91;
    
    note -= 36;
    
    // harmonic foldback in the first octave of the manual
    if (note < 12 && shift < 12)
        return note + 12;
    
    while (note + shift > 90)
        note -= 12;
    
    return note + shift;
}

static void set_tonewheels(struct tonewheel_organ_module *m, int tonegens[92])
{
    int n, i;
    
    memset(tonegens, 0, 92 * sizeof(tonegens[0]));
    // pedalboard
    for (n = 24; n < 24 + 12; n++)
    {
        if (check_keymask(m->keymasks, n))
        {
            tonegens[tonegenidx_pedals(n, 0)] += 3 * 16 * pedal_drawbar_settings[0];
            tonegens[tonegenidx_pedals(n, 12)] += 3 * 16 * pedal_drawbar_settings[1];
        }
    }
    // manual
    for (n = 36; n < 36 + 61; n++)
    {
        if (check_keymask(m->keymasks, n))
        {
            for (i = 0; i < 9; i++)
            {
                int tg = tonegenidx(n, drawbars[i]);
                tonegens[tg] += manual_drawbar_settings[i] * 16;
            }
            if (m->percussion > 0)
                tonegens[tonegenidx(n, 24+7)] += m->percussion * 8;
        }
    }
    for (n = 0; n < 91; n++)
    {
        int tgalt = n >= 48 ? n - 48 : n + 48;
        if (tgalt < 91)
            tonegens[tgalt] += tonegens[n] >> 5;
    }
}

void tonewheel_organ_process_block(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct tonewheel_organ_module *m = user_data;
    int n, i;
    //float a01, b1, x1, y1, x, q;
    int a01, b1, x1, y1;
    
    static const uint32_t frac_mask = (1 << 21) - 1;
    
    int internal_out[CBOX_BLOCK_SIZE];
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        internal_out[i] = 0;
    }
    // 91 tonewheels + 1 dummy
    int tonegens[92];
    set_tonewheels(m, tonegens);
    if (m->percussion > 0)
        m->percussion *= 0.99f;
    for (n = 0; n < 91; n++)
    {
        if (tonegens[n] > 0)
        {
            int iamp = tonegens[n];
            if (iamp > 512)
                iamp = 512 + 3 * ((iamp - 512) >> 2);
            
            iamp = (iamp * m->amp_scaling[n]) >> 10;
            
            int *table = n < 12 ? complex_table : sine_table;
            uint32_t phase = m->phase[n];
            for (i = 0; i < CBOX_BLOCK_SIZE; i++)
            {
                uint32_t pos = phase >> 21;
                int val0 = table[(pos - 1) & 2047];
                int val1 = table[pos];
                // phase & frac_mask has 21 bits of resolution, but we only have 14 bits of headroom here
                int frac_14bit = (phase & frac_mask) >> (21-14);
                int val = (val1 * frac_14bit + val0 * ((1 << 14) - frac_14bit)) >> 14;
                internal_out[i] += val * iamp;
                phase += m->frequency[n];
            }
        }
        m->phase[n] += m->frequency[n] * CBOX_BLOCK_SIZE;
    }
    
    int32_t vibrato_dphase = (int)(6.6 / 44100 * 65536 * 65536);
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        static const int v1[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8 };
        static const int v2[] = { 0, 1, 2, 4, 6, 8, 9, 10, 12 };
        static const int v3[] = { 0, 1, 3, 6, 11, 12, 15, 17, 18, 18, 18 };
        const int *dmap = v3;
        int x0 = internal_out[i];
        int delay[19];
        int64_t accum;
        uint32_t vphase = m->vibrato_phase;
        uint32_t vphint;
        if (vphase >= 0x80000000)
            vphase = ~vphase;
        delay[0] = x0;
        for (n = 0; n < 18; n++)
        {
            struct biquad *bq = &m->scanner_delay[n];
            accum = 0;
            accum += (x0 + (bq->x1 << 1) + bq->x2) * scanner_a0;
            accum -= bq->y1 * scanner_b1;
            accum -= bq->y2 * scanner_b2;
            accum = accum >> 20;
            bq->x2 = bq->x1;
            bq->x1 = x0;
            bq->y2 = bq->y1;
            bq->y1 = accum;
            
            delay[1 + n] = x0 = accum;
        }
        m->vibrato_phase += vibrato_dphase;
        
        vphint = vphase >> 28;
        accum = 0;
        accum += ((int64_t)delay[0]) << 28;
        accum += delay[dmap[vphint]] * ((1ULL << 28) - (vphase & ~0xF0000000));
        accum += delay[dmap[vphint + 1]] * (vphase & ~0xF0000000ULL);
        
        vphase = m->vibrato_phase + (1 << 27);
        if (vphase >= 0x80000000)
            vphase = ~vphase;        
        vphint = vphase >> 28;
        accum = 0;
        accum += ((int64_t)delay[0]) << 28;
        accum += delay[dmap[vphint]] * ((1ULL << 28) - (vphase & ~0xF0000000));
        accum += delay[dmap[vphint + 1]] * (vphase & ~0xF0000000ULL);
        
        internal_out[i] = accum >> 29;
    }
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        int value = (internal_out[i] >> 12);
        int sign = (value >= 0 ? 1 : -1);
        int result;
        int a, b, idx;
        
        value = abs(value);
        if (value > 8192 * 8 - 2 * 8) 
            value = 8192 * 8 - 2 * 8;
        idx = value >> 3;
        a = distortion_table[idx];
        b = distortion_table[idx + 1];
        internal_out[i] = sign * (a + ((b - a) * (value & 7) >> 3));
        //internal_out[i] = 32767 * value2;
    }
    x1 = m->lowpass_x1;
    y1 = m->lowpass_y1;
    
    /*
    This is where the coeffs came from:
    x = tan (M_PI * 60 / (2 * 44100));
    q = 1/(1+x);
    a01 = x*q;
    b1 = a01 - q;
    However, I had to adapt them for fixed point (including refactoring of b1 as 1 - value for easier multiplication)
    */
    a01 = (int)(0.5 + 0.002133 * 65536);
    b1 = (int)(0.5 + (1 - 0.995735) * 16384);
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        int x0 = internal_out[i]; // ~ -32768 to 32767
        y1 = (x0 + x1) + y1 - (b1 * y1 >> 14);
        x1 = x0;
        y1 += (y1 == -1);
        int64_t out = ((int64_t)y1) * a01 >> 16;
        if (out < -32768)
            out = -32768;
        if (out > 32767)
            out = 32767;
        internal_out[i] = out;
    }
    m->lowpass_x1 = x1;
    m->lowpass_y1 = y1;
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float value = internal_out[i]* (1.0 / 32768.0);
        outputs[1][i] = outputs[0][i] = value;
    }
}

static void biquad_init(struct biquad *bq)
{
    bq->x1 = bq->y1 = bq->x2 = bq->y2 = 0;
}

struct cbox_module *tonewheel_organ_create(void *user_data)
{
    static int inited = 0;
    int i;
    if (!inited)
    {
        for (i = 0; i < 2048; i++)
        {
            float ph = i * M_PI / 1024;
            sine_table[i] = (int)(32000 * sin(ph));
            complex_table[i] = (int)(32000 * (sin(ph) + sin(3 * ph) / 3 + sin(5 * ph) / 5 + sin(7 * ph) / 7 + sin(9 * ph) / 9 + sin(11 * ph) / 11));
        }
        for (i = 0; i < 8192; i++)
        {
            float value = atan(sqrt(i * (4.0 / 8192)));
            distortion_table[i] = ((int)(i * 4 + 32767 * value * value)) >> 1;
        }
        inited = 1;
    }
    
    struct tonewheel_organ_module *m = malloc(sizeof(struct tonewheel_organ_module));
    m->module.user_data = m;
    m->module.process_event = tonewheel_organ_process_event;
    m->module.process_block = tonewheel_organ_process_block;
    m->lowpass_x1 = 0;
    m->lowpass_y1 = 0;
    m->percussion = -1;
    m->do_filter = 0;
    m->cc91 = 0;
    m->vibrato_phase = 0;
    for (i = 0; i < 18; i++)
    {
        biquad_init(&m->scanner_delay[i]);
    }
    for (i = 0; i < 91; i++)
    {
        float freq_hz = 440 * pow(2.0, (i - 45) / 12.0);
        float scaling = freq_hz / 60.0;
        if (scaling < 1)
            scaling = 1;
        if (scaling > 32)
            scaling = 32 + ((scaling - 32) / 2.0);
        m->frequency[i] = (uint32_t)(freq_hz * 65536 * 65536 / 44100);
        m->phase[i] = 0;
        m->amp_scaling[i] = (int)(1024 * scaling);
    }
    for (i = 0; i < 4; i++)
        m->keymasks[i] = 0;
    
    return &m->module;
}

struct cbox_module_keyrange_metadata tonewheel_organ_keyranges[] = {
    { 1, 24, 35, "Pedal keyboard" },
    { 1, 36, 36 + 60, "Manual" },
};

DEFINE_MODULE(tonewheel_organ, 0, 2)

