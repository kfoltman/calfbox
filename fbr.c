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

#include "biquad-float.h"
#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "eq.h"
#include "module.h"
#include "rt.h"
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

struct feedback_reducer_params
{
    struct eq_band bands[MAX_FBR_BANDS];
};

struct feedback_reducer_module
{
    struct cbox_module module;
    
    struct feedback_reducer_params *params, *old_params;

    struct cbox_biquadf_coeffs coeffs[MAX_FBR_BANDS];
    struct cbox_biquadf_state state[MAX_FBR_BANDS][2];
    
    float analysis_buffer[ANALYSIS_BUFFER_SIZE];
    float *wrptr;
    int analysed;

    complex float fft_buffers[2][ANALYSIS_BUFFER_SIZE];
};

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

#define PEAK_REGION_RADIUS 3

struct potential_peak_info
{
    int bin;
    float avg;
    float centre;
    float peak;
    float dist;
    float points;
};

static int peak_compare(const void *peak1, const void *peak2)
{
    const struct potential_peak_info *pi1 = peak1;
    const struct potential_peak_info *pi2 = peak2;
    
    if (pi1->points < pi2->points)
        return +1;
    if (pi1->points > pi2->points)
        return -1;
    return 0;
}

static int find_peaks(complex float *spectrum, float srate, float peak_freqs[16])
{
    struct potential_peak_info pki[ANALYSIS_BUFFER_SIZE / 2 + 1];
    for (int i = 0; i <= ANALYSIS_BUFFER_SIZE / 2; i++)
    {
        pki[i].bin = i;
        pki[i].points = 0.f;
    }
    float gmax = 0;
    for (int i = PEAK_REGION_RADIUS; i <= ANALYSIS_BUFFER_SIZE / 2 - PEAK_REGION_RADIUS; i++)
    {
        struct potential_peak_info *pi = &pki[i];
        float sum = 0;
        float sumf = 0;
        float peak = 0;
        for (int j = -PEAK_REGION_RADIUS; j <= PEAK_REGION_RADIUS; j++)
        {
            float f = (i + j);
            float bin = cabs(spectrum[i + j]);
            if (bin > peak)
                peak = bin;
            sum += bin;
            sumf += f * bin;
        }
        pi->avg = sum / (2 * PEAK_REGION_RADIUS + 1);
        pi->peak = peak;
        pi->centre = sumf / sum;
        pi->dist = (sumf / sum - i);
        if (peak > gmax)
            gmax = peak;
        // printf("Bin %d sumf/sum %f avg %f peak %f p/a %f dist %f val %f\n", i, sumf / sum, pki[i].avg, peak, peak / pki[i].avg, sumf/sum - i, cabs(spectrum[i]));
    }
    for (int i = PEAK_REGION_RADIUS; i <= ANALYSIS_BUFFER_SIZE / 2 - PEAK_REGION_RADIUS; i++)
    {
        struct potential_peak_info *tpi = &pki[i];
        // ignore peaks below -40dB of the max bin
        if (pki[(int)tpi->centre].peak < gmax * 0.01)
            continue;
        pki[(int)tpi->centre].points += 1;
    }
    for (int i = 0; i <= ANALYSIS_BUFFER_SIZE / 2; i++)
    {
        float freq = i * srate / ANALYSIS_BUFFER_SIZE;
        // printf("Bin %d freq %f points %f\n", i, freq, pki[i].points);
    }
    qsort(pki, ANALYSIS_BUFFER_SIZE / 2 + 1, sizeof(struct potential_peak_info), peak_compare);
    
    float peaks[16];
    int peak_count = 0;
    for (int i = 0; i <= ANALYSIS_BUFFER_SIZE / 2; i++)
    {
        if (pki[i].points <= 1)
            break;
        if (pki[i].peak <= 0.0001)
            break;
        gboolean dupe = FALSE;
        for (int j = 0; j < peak_count; j++)
        {
            if (fabs(peaks[j] - pki[i].centre) < PEAK_REGION_RADIUS)
            {
                dupe = TRUE;
                break;
            }
        }
        if (dupe)
            continue;
        peak_freqs[peak_count] = pki[i].centre * srate / ANALYSIS_BUFFER_SIZE;
        peaks[peak_count++] = pki[i].centre;
        printf("Mul %f freq %f points %f peak %f\n", pki[i].centre, pki[i].centre * srate / ANALYSIS_BUFFER_SIZE, pki[i].points, pki[i].peak);
        if (peak_count == 4)
            break;
    }
    return peak_count;
}

static void redo_filters(struct feedback_reducer_module *m)
{
    for (int i = 0; i < MAX_FBR_BANDS; i++)
    {
        struct eq_band *band = &m->params->bands[i];
        if (band->active)
        {
            cbox_biquadf_set_peakeq_rbj(&m->coeffs[i], band->center, band->q, band->gain, m->module.srate);
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
        m->analysed = 0;
        cbox_rt_swap_pointers(m->module.rt, (void **)&m->wrptr, m->analysis_buffer);
    }
    else if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        if (m->wrptr == m->analysis_buffer + ANALYSIS_BUFFER_SIZE && m->analysed == 0)
        {
            float freqs[16];
            int count = find_peaks(m->fft_buffers[do_fft(m)], m->module.srate, freqs);
            struct feedback_reducer_params *p = malloc(sizeof(struct feedback_reducer_params));
            memcpy(p->bands + count, &m->params->bands[0], sizeof(struct eq_band) * (MAX_FBR_BANDS - count));
            for (int i = 0; i < count; i++)
            {
                p->bands[i].active = TRUE;
                p->bands[i].center = freqs[i];
                p->bands[i].q = freqs[i] / 50; // each band ~100 Hz (not really sure about filter Q vs bandwidth)
                p->bands[i].gain = 0.125;
            }
            free(cbox_rt_swap_pointers(m->module.rt, (void **)&m->params, p)); \
            m->analysed = 1;
            if (!cbox_execute_on(fb, NULL, "/refresh", "i", error, 1))
                return FALSE;
        }
        if (!cbox_execute_on(fb, NULL, "/finished", "i", error, m->analysed))
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
        return CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error);
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
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
                cbox_biquadf_process_to(&m->state[i][c], &m->coeffs[i], inputs[c], outputs[c]);
                first = FALSE;
            }
            else
            {
                cbox_biquadf_process(&m->state[i][c], &m->coeffs[i], outputs[c]);
            }
        }
        if (first)
            memcpy(outputs[c], inputs[c], sizeof(float) * CBOX_BLOCK_SIZE);
    }
}

MODULE_SIMPLE_DESTROY_FUNCTION(feedback_reducer)

MODULE_CREATE_FUNCTION(feedback_reducer)
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
    CALL_MODULE_INIT(m, 2, 2, feedback_reducer);
    m->module.process_event = feedback_reducer_process_event;
    m->module.process_block = feedback_reducer_process_block;
    struct feedback_reducer_params *p = malloc(sizeof(struct feedback_reducer_params));
    m->params = p;
    m->old_params = NULL;
    m->analysed = 0;
    m->wrptr = NULL;
    
    for (int b = 0; b < MAX_FBR_BANDS; b++)
    {
        p->bands[b].active = cbox_eq_get_band_param(cfg_section, b, "active", 0) > 0;
        p->bands[b].center = cbox_eq_get_band_param(cfg_section, b, "center", 50 * pow(2.0, b / 2.0));
        p->bands[b].q = cbox_eq_get_band_param(cfg_section, b, "q", 0.707 * 2);
        p->bands[b].gain = cbox_eq_get_band_param_db(cfg_section, b, "gain", 0);
    }
    redo_filters(m);
    cbox_eq_reset_bands(m->state, MAX_FBR_BANDS);
    
    return &m->module;
}


struct cbox_module_keyrange_metadata feedback_reducer_keyranges[] = {
};

struct cbox_module_livecontroller_metadata feedback_reducer_controllers[] = {
};

DEFINE_MODULE(feedback_reducer, 2, 2)

