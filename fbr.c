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
#include "biquad-float.h"
#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "module.h"
#include <complex.h>
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

#define MODULE_PARAMS feedback_reducer_params

#define MAX_FBR_BANDS 16

#define ANALYSIS_BUFFER_SIZE 8192
#define ANALYSIS_BUFFER_BITS 13

// Sine table
static complex float euler_table[ANALYSIS_BUFFER_SIZE];

// Bit reversal table
static int map_table[ANALYSIS_BUFFER_SIZE];

// Bit-reversed von Hann window
static float von_hann_window_transposed[ANALYSIS_BUFFER_SIZE];

struct fbr_band
{
    gboolean active;
    float center;
    float q;
    float gain;
};

struct feedback_reducer_params
{
    struct fbr_band bands[MAX_FBR_BANDS];
};

struct feedback_reducer_module
{
    struct cbox_module module;
    
    struct feedback_reducer_params *params, *old_params;

    struct cbox_biquadf_coeffs coeffs[MAX_FBR_BANDS];
    struct cbox_biquadf_state state[2][MAX_FBR_BANDS];
    
    float analysis_buffer[ANALYSIS_BUFFER_SIZE];
    float *wrptr;

    complex float fft_buffers[2][ANALYSIS_BUFFER_SIZE];

    int srate;
};

// Do a butterfly operation:
// dst1 = src1 + e^iw_1*src2
// dst2 = src1 + e^iw_2*src2 (w = phase * 2pi / ANALYSIS_BUFFER_SIZE)
static inline void butterfly(complex float *dst1, complex float *dst2, complex float src1, complex float src2, complex float eiw1, complex float eiw2)
{
    *dst1 = src1 + eiw1 * src2;
    *dst2 = src1 + eiw2 * src2;
}

// Trivial implementation of Cooley-Tukey (+ my own mistakes) + von Hann window
static int do_fft(struct feedback_reducer_module *m)
{
    // Copy + bit reversal addressing
    for (int i = 0; i < ANALYSIS_BUFFER_SIZE; i++)
    {
        m->fft_buffers[0][i] = von_hann_window_transposed[i] * m->analysis_buffer[map_table[i]] * (2.0 / ANALYSIS_BUFFER_SIZE);
    }
    
    for (int i = 0; i < ANALYSIS_BUFFER_BITS; i++)
    {
        complex float *src = m->fft_buffers[i & 1];
        complex float *dst = m->fft_buffers[(~i) & 1];
        int invi = ANALYSIS_BUFFER_BITS - i - 1;
        int disp = 1 << i;
        int mask = disp - 1;
        
        for (int j = 0; j < ANALYSIS_BUFFER_SIZE / 2; j++)
        {
            int jj1 = (j & mask) + ((j & ~mask) << 1); // insert 0 at ith bit to get the left arm of the butterfly
            int jj2 = jj1 + disp;                      // insert 1 at ith bit to get the right arm

            // e^iw
            complex float eiw1 = euler_table[(jj1 << invi) & (ANALYSIS_BUFFER_SIZE - 1)];
            complex float eiw2 = euler_table[(jj2 << invi) & (ANALYSIS_BUFFER_SIZE - 1)];

            // printf("%d -> %d, %d\n", j, jj, jj + disp);
            butterfly(&dst[jj1], &dst[jj2], src[jj1], src[jj2], eiw1, eiw2);
        }
    }
    return ANALYSIS_BUFFER_BITS & 1;
}

static void redo_filters(struct feedback_reducer_module *m)
{
    for (int i = 0; i < MAX_FBR_BANDS; i++)
    {
        struct fbr_band *band = &m->params->bands[i];
        if (band->active)
        {
            cbox_biquadf_set_peakeq_rbj(&m->coeffs[i], band->center, band->q, band->gain, m->srate);
        }
    }
    m->old_params = m->params;
}

gboolean feedback_reducer_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct feedback_reducer_module *m = (struct feedback_reducer_module *)ct->user_data;
    
    EFFECT_PARAM_ARRAY("/active", "i", bands, active, int, , 0, 1) else
    EFFECT_PARAM_ARRAY("/center", "f", bands, center, double, , 10, 20000) else
    EFFECT_PARAM_ARRAY("/q", "f", bands, q, double, , 0.01, 100) else
    EFFECT_PARAM_ARRAY("/gain", "f", bands, gain, double, dB2gain_simple, -100, 100) else
    if (!strcmp(cmd->command, "/start") && !strcmp(cmd->arg_types, ""))
    {
        cbox_rt_swap_pointers(app.rt, (void **)&m->wrptr, m->analysis_buffer);
    }
    else if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/finished", "i", error, m->wrptr == m->analysis_buffer + ANALYSIS_BUFFER_SIZE))
            return FALSE;
        for (int i = 0; i < MAX_FBR_BANDS; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/active", "ii", error, i, (int)m->params->bands[i].active))
                return FALSE;
            if (!cbox_execute_on(fb, NULL, "/center", "if", error, i, m->params->bands[i].center))
                return FALSE;
            if (!cbox_execute_on(fb, NULL, "/q", "if", error, i, m->params->bands[i].q))
                return FALSE;
            if (!cbox_execute_on(fb, NULL, "/gain", "if", error, i, gain2dB_simple(m->params->bands[i].gain)))
                return FALSE;
        }
        // return cbox_execute_on(fb, NULL, "/wet_dry", "f", error, m->params->wet_dry);
        return TRUE;
    }
    else
        return cbox_set_command_error(error, cmd);
    return TRUE;
}

void feedback_reducer_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct feedback_reducer_module *m = module->user_data;
}

void feedback_reducer_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct feedback_reducer_module *m = module->user_data;
    
    if (m->params != m->old_params)
        redo_filters(m);
    
    if (m->wrptr && m->wrptr != m->analysis_buffer + ANALYSIS_BUFFER_SIZE)
    {
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
        {
            if (m->wrptr == m->analysis_buffer + ANALYSIS_BUFFER_SIZE)
                break;
            *m->wrptr++ = inputs[0][i] + inputs[1][i];
        }
    }
    for (int c = 0; c < 2; c++)
    {
        gboolean first = TRUE;
        for (int i = 0; i < MAX_FBR_BANDS; i++)
        {
            if (!m->params->bands[i].active)
                continue;
            if (first)
            {
                cbox_biquadf_process_to(&m->state[c][i], &m->coeffs[i], inputs[c], outputs[c]);
                first = FALSE;
            }
            else
            {
                cbox_biquadf_process(&m->state[c][i], &m->coeffs[i], outputs[c]);
            }
        }
        if (first)
            memcpy(outputs[c], inputs[c], sizeof(float) * CBOX_BLOCK_SIZE);
    }
}

static float get_band_param(const char *cfg_section, int band, const char *param, float defvalue)
{
    gchar *s = g_strdup_printf("band%d_%s", band + 1, param);
    float v = cbox_config_get_float(cfg_section, s, defvalue);
    g_free(s);
    
    return v;
}

static float get_band_param_db(const char *cfg_section, int band, const char *param, float defvalue)
{
    gchar *s = g_strdup_printf("band%d_%s", band + 1, param);
    float v = cbox_config_get_gain_db(cfg_section, s, defvalue);
    g_free(s);
    
    return v;
}

struct cbox_module *feedback_reducer_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    if (!inited)
    {
        for (int i = 0; i < ANALYSIS_BUFFER_SIZE; i++)
        {
            euler_table[i] = cos(i * 2 * M_PI / ANALYSIS_BUFFER_SIZE) + I * sin(i * 2 * M_PI / ANALYSIS_BUFFER_SIZE);
            int ni = 0;
            for (int j = 0; j < ANALYSIS_BUFFER_BITS; j++)
            {
                if (i & (1 << (ANALYSIS_BUFFER_BITS - 1 - j)))
                    ni = ni | (1 << j);
            }
            map_table[i] = ni;
            von_hann_window_transposed[i] = 0.5 * (1 - cos (ni * 2 * M_PI / (ANALYSIS_BUFFER_SIZE - 1)));
        }
            
        inited = 1;
    }
    
    struct feedback_reducer_module *m = malloc(sizeof(struct feedback_reducer_module));
    cbox_module_init(&m->module, m, 2, 2);
    m->module.process_event = feedback_reducer_process_event;
    m->module.process_block = feedback_reducer_process_block;
    m->module.cmd_target.process_cmd = feedback_reducer_process_cmd;
    m->srate = srate;
    struct feedback_reducer_params *p = malloc(sizeof(struct feedback_reducer_params));
    m->params = p;
    m->old_params = NULL;
    
    for (int i = 0; i < MAX_FBR_BANDS; i++)
    {
        p->bands[i].active = get_band_param(cfg_section, i, "active", 0) > 0;
        p->bands[i].center = get_band_param(cfg_section, i, "center", 50 * pow(2.0, i / 2.0));
        p->bands[i].q = get_band_param(cfg_section, i, "q", 0.707 * 2);
        p->bands[i].gain = get_band_param_db(cfg_section, i, "gain", 0);
    }
    redo_filters(m);

    /*
    for (int i = 0; i < ANALYSIS_BUFFER_SIZE; i++)
    {
        m->analysis_buffer[i] = 10000 * cos(i * 300.31 * 2 * M_PI / ANALYSIS_BUFFER_SIZE);
    }
    int idx = do_fft(m);
    for (int i = 0; i <= ANALYSIS_BUFFER_SIZE / 2; i++)
    {
        printf("[%d] = %f\n", i, (i == 0 ? 1 : 2) * cabs(m->fft_buffers[idx][i]));
    }
    */
    
    return &m->module;
}


struct cbox_module_keyrange_metadata feedback_reducer_keyranges[] = {
};

struct cbox_module_livecontroller_metadata feedback_reducer_controllers[] = {
};

DEFINE_MODULE(feedback_reducer, 2, 2)

