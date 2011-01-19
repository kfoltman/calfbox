// Copyright (C) 2010 Krzysztof Foltman. All rights reserved.

#include "dspmath.h"
#include "module.h"
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>

// a0 a1 a2 b1 b2 for scanner vibrato filter @4kHz with sr=44.1:    0.057198 0.114396 0.057198 -1.218829 0.447620

static int sine_table[2048];
static int complex_table[2048];

struct mono_sine_module
{
    struct cbox_module module;

    uint32_t frequency[91];
    uint32_t phase[91];
    uint32_t keymasks[4];
    int amp_scaling[91];
    int lowpass_x1, lowpass_y1;
    float percussion;
};

static const int drawbars[9] = {0, 19, 12, 24, 24 + 7, 36, 36 + 4, 36 + 7, 48};
static int pedal_drawbar_settings[2] = {8, 2};
static int manual_drawbar_settings[9] = {8, 3, 8, 0, 0, 0, 0, 0, 3};

void mono_sine_process_event(void *user_data, const uint8_t *data, uint32_t len)
{
    struct mono_sine_module *m = user_data;
    if (len > 0)
    {
        if (data[0] == 0x90)
        {
            int key = data[1] & 127;
            m->keymasks[key >> 5] |= 1 << (key & 31);
            if (m->percussion < 0)
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
        return note + shift;
    
    while (note + shift > 90)
        note -= 12;
    
    return note + shift;
}

void mono_sine_process_block(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct mono_sine_module *m = user_data;
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
    memset(tonegens, 0, sizeof(tonegens));
    // pedalboard
    for (n = 24; n < 24 + 12; n++)
    {
        if (check_keymask(m->keymasks, n))
        {
            tonegens[tonegenidx_pedals(n, 0)] += 4 * 16 * pedal_drawbar_settings[0];
            tonegens[tonegenidx_pedals(n, 12)] += 4 * 16 * pedal_drawbar_settings[1];
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
                int tgalt = tg >= 48 ? tg - 48 : tg + 48;
                if (tgalt < 91)
                    tonegens[tgalt] += manual_drawbar_settings[i] * 1;
            }
            if (m->percussion > 0)
                tonegens[tonegenidx(n, 24+7)] += m->percussion * 32;
        }
    }
    if (m->percussion > 0)
        m->percussion *= 0.99f;
    for (n = 0; n < 91; n++)
    {
        if (tonegens[n] > 0)
        {
            int iamp = tonegens[n] * m->amp_scaling[n] >> 10;
            if (iamp > 512)
                iamp = 512 + ((iamp - 512) >> 1);
            
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
        int x0 = internal_out[i] >> 12; // ~ -32768 to 32767
        y1 = (x0 + x1) + y1 - (b1 * y1 >> 14);
        x1 = x0;
        y1 += (y1 == -1);
        int64_t out = ((int64_t)y1) * a01 >> (16 - 2);
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
        outputs[1][i] = outputs[0][i] = internal_out[i] * (1.0 / 32768.0);
    }
}

struct cbox_module *mono_sine_create(void *user_data)
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
        inited = 1;
    }
    
    struct mono_sine_module *m = malloc(sizeof(struct mono_sine_module));
    m->module.user_data = m;
    m->module.process_event = mono_sine_process_event;
    m->module.process_block = mono_sine_process_block;
    m->lowpass_x1 = 0;
    m->lowpass_y1 = 0;
    m->percussion = -1;
    for (i = 0; i < 91; i++)
    {
        float freq_hz = 440 * pow(2.0, (i - 45) / 12.0);
        m->frequency[i] = (uint32_t)(freq_hz * 65536 * 65536 / 44100);
        m->phase[i] = 0;
        m->amp_scaling[i] = (int)(1024 * sqrt(m->frequency[i] / m->frequency[0]));
    }
    for (i = 0; i < 4; i++)
        m->keymasks[i] = 0;
    
    return &m->module;
}

struct cbox_module_manifest mono_sine_module = { NULL, 0, 2, mono_sine_create };
