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

#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "module.h"
#include "onepole-int.h"
#include <glib.h>
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
    uint64_t pedalmasks;
    uint64_t upper_manual, lower_manual;
    int amp_scaling[91];
    struct biquad scanner_delay[18];
    struct cbox_onepole_state filter_anticlick, filter_overdrive;
    struct cbox_onepole_coeffs filter_anticlick_coeffs, filter_overdrive_coeffs;
    float percussion;
    int enable_percussion, enable_vibrato_upper, enable_vibrato_lower, vibrato_mode, vibrato_mix, percussion_3rd;
    int do_filter;
    int cc91;
    uint32_t vibrato_phase, vibrato_dphase;
    
    int pedal_drawbar_settings[2];
    int upper_manual_drawbar_settings[9];
    int lower_manual_drawbar_settings[9];
};

static const int drawbars[9] = {0, 19, 12, 24, 24 + 7, 36, 36 + 4, 36 + 7, 48};

static void set_keymask(struct tonewheel_organ_module *m, int channel, int key, int value)
{
    uint64_t mask = 0;
    uint64_t *manual = NULL;
    if (key >= 24 && key < 36)
    {
        mask = 1 << (key - 24);
        manual = &m->pedalmasks;
    }
    else if (key >= 36 && key < 36 + 61)
    {
        manual = (channel == 0) ? &m->upper_manual : &m->lower_manual;
        mask = ((int64_t)1) << (key - 36);
    }
    else
        return;
    
    if (value)
        *manual |= mask;
    else
        *manual &= ~mask;
}

void tonewheel_organ_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct tonewheel_organ_module *m = (struct tonewheel_organ_module *)module;
    if (len > 0)
    {
        int cmd = data[0] >> 4;
        if (cmd == 9)
        {
            int channel = data[0] & 0x0F;
            int key = data[1] & 127;
            set_keymask(m, channel, key, 1);
            if (m->percussion < 0 && key >= 36 && m->enable_percussion && channel == 0)
                m->percussion = 16.0;
        }
        if (cmd == 8)
        {
            int channel = data[0] & 0x0F;
            int key = data[1] & 127;
            set_keymask(m, channel, key, 0);
            
            if (channel == 0 && !m->upper_manual)
                m->percussion = -1;
        }
        if (cmd == 11)
        {
            int *drawbars = (data[0] & 0xF0) != 0 ? m->lower_manual_drawbar_settings : m->upper_manual_drawbar_settings;
            if (data[1] >= 21 && data[1] <= 29)
                drawbars[data[1] - 21] = data[2] * 8 / 127;
            if (data[1] == 82)
                drawbars[8] = data[2] * 8 / 127;
            if (data[1] == 64)
                m->do_filter = data[2] >= 64;
            if (data[1] == 91)
                m->cc91 = data[2];
            if (data[1] == 93)
                m->vibrato_mix = data[2] > 0;
            //if (data[1] == 6)
            //    cbox_onepole_set_lowpass(&m->filter_overdrive_coeffs, hz2w(data[2] * 10000 / 127, 44100.0));
        }
    }
}

static inline int check_keymask(uint64_t keymasks, int note)
{
    if (note < 0 || note > 127)
        return 0;
    if (note >= 24 && note < 36)
        return 0 != (keymasks & (1 << (note - 24)));
    if (note >= 36 && note < 36 + 61)
        return 0 != (keymasks & (1ULL << (note - 36)));
    return 0;
}

static inline int tonegenidx_pedals(int note, int shift)
{
    if (note < 24 || note > 24 + 11)
        return 91;
    
    note -= 24;
    return note + shift;
}

static inline int tonegenidx(int note, int shift)
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

static int drawbar_amp_mapping[9] = { 0, 1, 2, 3, 4, 6, 8, 11, 16 };

static void calc_crosstalk(int *wheel1, int *wheel2)
{
    int w1 = *wheel1;
    int w2 = *wheel2;
    *wheel1 += w2 >> 9;
    *wheel2 += w1 >> 9;
}

static int compress_amp(int iamp, int scaling)
{
    if (iamp > 512)
        iamp = 512 + 3 * ((iamp - 512) >> 2);        
    return (iamp * scaling) >> 10;
}

static void set_tonewheels(struct tonewheel_organ_module *m, int tonegens[2][92])
{
    int n, i;
    int pshift = m->percussion_3rd ? 24 + 7 : 24;
    
    int upper_manual_drawbar_amp[9], lower_manual_drawbar_amp[9];
    
    for (i = 0; i < 9; i++)
    {
        upper_manual_drawbar_amp[i] = drawbar_amp_mapping[m->upper_manual_drawbar_settings[i]] * 8;
        lower_manual_drawbar_amp[i] = drawbar_amp_mapping[m->lower_manual_drawbar_settings[i]] * 8;
    }
    
    memset(tonegens, 0, 2 * 92 * sizeof(tonegens[0][0]));
    // pedalboard
    for (n = 24; n < 24 + 12; n++)
    {
        if (check_keymask(m->pedalmasks, n))
        {
            tonegens[0][tonegenidx_pedals(n, 0)] += 3 * 16 * m->pedal_drawbar_settings[0];
            tonegens[0][tonegenidx_pedals(n, 12)] += 3 * 16 * m->pedal_drawbar_settings[1];
        }
    }
    // manual
    for (n = 36; n < 36 + 61; n++)
    {
        if (check_keymask(m->upper_manual, n))
        {
            int tgf = m->enable_vibrato_upper;
            for (i = 0; i < 9; i++)
            {
                int tg = tonegenidx(n, drawbars[i]);
                tonegens[tgf][tg] += upper_manual_drawbar_amp[i];
            }
            if (m->percussion > 0)
                tonegens[0][tonegenidx(n, pshift)] += m->percussion * 10;
        }
        if (check_keymask(m->lower_manual, n))
        {
            int tgf = m->enable_vibrato_lower;
            for (i = 0; i < 9; i++)
            {
                int tg = tonegenidx(n, drawbars[i]);
                tonegens[tgf][tg] += lower_manual_drawbar_amp[i];
            }
        }
    }
    for (n = 0; n < 91; n++)
    {
        int scaling = m->amp_scaling[n];
        tonegens[0][n] = compress_amp(tonegens[0][n], scaling);
        tonegens[1][n] = compress_amp(tonegens[1][n], scaling);
    }
    for (n = 0; n < 36; n++)
    {
        calc_crosstalk(&tonegens[0][n], &tonegens[0][n + 48]);
        calc_crosstalk(&tonegens[1][n], &tonegens[1][n + 48]);
    }
}

void tonewheel_organ_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct tonewheel_organ_module *m = (struct tonewheel_organ_module *)module;
    int n, i;
    //float a01, b1, x1, y1, x, q;
    int a01, b1, x1, y1;
    
    static const uint32_t frac_mask = (1 << 21) - 1;
    
    int internal_out_for_vibrato[CBOX_BLOCK_SIZE];
    int internal_out[CBOX_BLOCK_SIZE];
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        internal_out[i] = 0;
        internal_out_for_vibrato[i] = 0;
    }
    // 91 tonewheels + 1 dummy
    int tonegens[2][92];
    set_tonewheels(m, tonegens);
    if (m->percussion > 0)
        m->percussion *= 0.99f;
    for (n = 0; n < 91; n++)
    {
        if (tonegens[0][n] > 0 || tonegens[1][n])
        {
            int iamp1, iamp2, scaling;
            
            iamp1 = tonegens[0][n];
            iamp2 = tonegens[1][n];
            
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
                internal_out[i] += val * iamp1 >> 3;
                internal_out_for_vibrato[i] += val * iamp2 >> 3;
                phase += m->frequency[n];
            }
        }
        m->phase[n] += m->frequency[n] * CBOX_BLOCK_SIZE;
    }
    
    static const int v1[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8 };
    static const int v2[] = { 0, 1, 2, 4, 6, 8, 9, 10, 12 };
    static const int v3[] = { 0, 1, 3, 6, 11, 12, 15, 17, 18, 18, 18 };
    static const int *vtypes[] = { v1, v2, v3 };
    const int *dmap = vtypes[m->vibrato_mode];
    int32_t mix = m->vibrato_mix;
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        int x0 = internal_out_for_vibrato[i] >> 1;
        int delay[19];
        int64_t accum;
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
        m->vibrato_phase += m->vibrato_dphase;
        
        uint32_t vphase = m->vibrato_phase;
        if (vphase >= 0x80000000)
            vphase = ~vphase;
        uint32_t vphint = vphase >> 28;
        
        accum = 0;
        
        accum += delay[dmap[vphint]] * ((1ULL << 28) - (vphase & ~0xF0000000));
        accum += delay[dmap[vphint + 1]] * (vphase & ~0xF0000000ULL);
        

        internal_out[i] += (accum >> 28) + mix * delay[0];
    }

    int32_t filtered[CBOX_BLOCK_SIZE];
    cbox_onepole_process_to(&m->filter_overdrive, &m->filter_overdrive_coeffs, internal_out, filtered);
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        int value = filtered[i] >> 9;
        int sign = (value >= 0 ? 1 : -1);
        int result;
        int a, b, idx;
        
        value = abs(value);
        if (value > 8192 * 8 - 2 * 8) 
            value = 8192 * 8 - 2 * 8;
        idx = value >> 3;
        a = distortion_table[idx];
        b = distortion_table[idx + 1];
        internal_out[i] = (internal_out[i] >> 11) + sign * (a + ((b - a) * (value & 7) >> 3));
        //internal_out[i] = 32767 * value2;
    }
    
    int prev = internal_out[0];
    cbox_onepole_process(&m->filter_anticlick, &m->filter_anticlick_coeffs, internal_out);
    
    for (i = 0; i < CBOX_BLOCK_SIZE; i++)
    {
        float value = internal_out[i] * (1.0 / 32768.0);
        outputs[1][i] = outputs[0][i] = value;
    }
}

static void biquad_init(struct biquad *bq)
{
    bq->x1 = bq->y1 = bq->x2 = bq->y2 = 0;
}

static void read_drawbars(int *drawbars, int count, const char *registration)
{
    int i;
    
    memset(drawbars, 0, count * sizeof(int));
    for (i = 0; i < count; i++)
    {
        if (!registration[i])
        {
            g_error("registration too short: %s (%d digits required)", registration, count);
            break;
        }
        if (registration[i] < '0' || registration[i] > '8')
        {
            g_error("registration invalid: %s (%c is not in 0..8)", registration, registration[i]);
            break;
        }
        drawbars[i] = registration[i] - '0';
    }
}

struct cbox_module *tonewheel_organ_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    int i;
    const char *vibrato_mode;
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
            distortion_table[i] = ((int)(i * 2 + 32767 * value * value)) >> 1;
        }
        inited = 1;
    }
    
    struct tonewheel_organ_module *m = malloc(sizeof(struct tonewheel_organ_module));
    cbox_module_init(&m->module, m);
    m->module.process_event = tonewheel_organ_process_event;
    m->module.process_block = tonewheel_organ_process_block;
    cbox_onepole_reset(&m->filter_anticlick);
    cbox_onepole_reset(&m->filter_overdrive);
    cbox_onepole_set_lowpass(&m->filter_anticlick_coeffs, hz2w(180.0, srate));
    cbox_onepole_set_lowpass(&m->filter_overdrive_coeffs, hz2w(2500.0, srate));
    m->percussion = -1;
    m->do_filter = 0;
    m->cc91 = 0;
    m->vibrato_phase = 0;
    read_drawbars(m->upper_manual_drawbar_settings, 9, cbox_config_get_string_with_default(cfg_section, "upper_drawbars", "888000000"));
    read_drawbars(m->lower_manual_drawbar_settings, 9, cbox_config_get_string_with_default(cfg_section, "lower_drawbars", "888800000"));
    read_drawbars(m->pedal_drawbar_settings, 2, cbox_config_get_string_with_default(cfg_section, "pedal_drawbars", "82"));
    m->enable_percussion = cbox_config_get_int(cfg_section, "percussion", 1);
    m->enable_vibrato_upper = cbox_config_get_int(cfg_section, "vibrato_upper", 1);
    m->enable_vibrato_lower = cbox_config_get_int(cfg_section, "vibrato_lower", 0);
    m->percussion_3rd = cbox_config_get_int(cfg_section, "percussion_3rd", 1);
    m->vibrato_dphase = (int)(6.6 / srate * 65536 * 65536);
    
    vibrato_mode = cbox_config_get_string_with_default(cfg_section, "vibrato_mode", "c3");
    if (vibrato_mode[0] == 'c')
        m->vibrato_mix = 1;
    else if (vibrato_mode[0] == 'v')
        m->vibrato_mix = 0;
    else
    {
        g_error("Unknown vibrato mode: %s (allowed: v1, v2, v3, c1, c2, c3)", vibrato_mode);
        m->vibrato_mix = 0;
    }
    if (vibrato_mode[1] >= '1' && vibrato_mode[1] <= '3')
        m->vibrato_mode = vibrato_mode[1] - '1';
    else
    {
        g_error("Unknown vibrato mode: %s (allowed: v1, v2, v3, c1, c2, c3)", vibrato_mode);
        m->vibrato_mode = 2;
    }
    
    for (i = 0; i < 18; i++)
    {
        biquad_init(&m->scanner_delay[i]);
    }
    for (i = 0; i < 91; i++)
    {
        float freq_hz = 440 * pow(2.0, (i - 45) / 12.0);
        float scaling = freq_hz / 120.0;
        if (scaling < 1)
            scaling = 1;
        if (scaling > 24)
            scaling = 24 + ((scaling - 24) / 2.5);
        m->frequency[i] = (uint32_t)(freq_hz * 65536 * 65536 / srate);
        m->phase[i] = 0;
        m->amp_scaling[i] = (int)(1024 * scaling);
    }
    m->upper_manual = 0;
    m->lower_manual = 0;
    m->pedalmasks = 0;
    
    return &m->module;
}

struct cbox_module_keyrange_metadata tonewheel_organ_keyranges[] = {
    { 0, 24, 35, "Pedal keyboard" },
    { 1, 36, 36 + 60, "Upper Manual" },
    { 2, 36, 36 + 60, "Lower Manual" },
};

struct cbox_module_livecontroller_metadata tonewheel_organ_controllers[] = {
    { 0, cmlc_onoffcc, 93, "Vib/Chr", NULL},
    
    { 1, cmlc_continuouscc, 21, "Upper Drawbar 1", NULL},
    { 1, cmlc_continuouscc, 22, "Upper Drawbar 2", NULL},
    { 1, cmlc_continuouscc, 23, "Upper Drawbar 3", NULL},
    { 1, cmlc_continuouscc, 24, "Upper Drawbar 4", NULL},
    { 1, cmlc_continuouscc, 25, "Upper Drawbar 5", NULL},
    { 1, cmlc_continuouscc, 26, "Upper Drawbar 6", NULL},
    { 1, cmlc_continuouscc, 27, "Upper Drawbar 7", NULL},
    { 1, cmlc_continuouscc, 28, "Upper Drawbar 8", NULL},
    { 1, cmlc_continuouscc, 29, "Upper Drawbar 9", NULL},
    
    { 2, cmlc_continuouscc, 21, "Lower Drawbar 1", NULL},
    { 2, cmlc_continuouscc, 22, "Lower Drawbar 2", NULL},
    { 2, cmlc_continuouscc, 23, "Lower Drawbar 3", NULL},
    { 2, cmlc_continuouscc, 24, "Lower Drawbar 4", NULL},
    { 2, cmlc_continuouscc, 25, "Lower Drawbar 5", NULL},
    { 2, cmlc_continuouscc, 26, "Lower Drawbar 6", NULL},
    { 2, cmlc_continuouscc, 27, "Lower Drawbar 7", NULL},
    { 2, cmlc_continuouscc, 28, "Lower Drawbar 8", NULL},
    { 2, cmlc_continuouscc, 29, "Lower Drawbar 9", NULL},
};

DEFINE_MODULE(tonewheel_organ, 0, 2)

