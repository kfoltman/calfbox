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
#include "onepole-float.h"
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

struct tone_control_module
{
    struct cbox_module module;

    struct cbox_onepolef_coeffs lowpass_coeffs, highpass_coeffs;
    
    struct cbox_onepolef_state lowpass_state[2], highpass_state[2];
};

void tone_control_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct tone_control_module *m = (struct tone_control_module *)module;
}

void tone_control_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct tone_control_module *m = (struct tone_control_module *)module;
    
    cbox_onepolef_process_to(&m->lowpass_state[0], &m->lowpass_coeffs, inputs[0], outputs[0]);
    cbox_onepolef_process_to(&m->lowpass_state[1], &m->lowpass_coeffs, inputs[1], outputs[1]);
    cbox_onepolef_process(&m->highpass_state[0], &m->highpass_coeffs, outputs[0]);
    cbox_onepolef_process(&m->highpass_state[1], &m->highpass_coeffs, outputs[1]);
}

struct cbox_module *tone_control_create(void *user_data, const char *cfg_section, int srate)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct tone_control_module *m = malloc(sizeof(struct tone_control_module));
    cbox_module_init(&m->module, m);
    m->module.process_event = tone_control_process_event;
    m->module.process_block = tone_control_process_block;
    
    float tpdsr = 2 * M_PI / srate;
    
    cbox_onepolef_set_lowpass(&m->lowpass_coeffs, cbox_config_get_float(cfg_section, "lowpass", 8000.f) * tpdsr);
    cbox_onepolef_set_highpass(&m->highpass_coeffs, cbox_config_get_float(cfg_section, "highpass", 75.f) * tpdsr);
    cbox_onepolef_reset(&m->lowpass_state[0]);
    cbox_onepolef_reset(&m->lowpass_state[1]);
    cbox_onepolef_reset(&m->highpass_state[0]);
    cbox_onepolef_reset(&m->highpass_state[1]);
    
    return &m->module;
}


struct cbox_module_keyrange_metadata tone_control_keyranges[] = {
};

struct cbox_module_livecontroller_metadata tone_control_controllers[] = {
};

DEFINE_MODULE(tone_control, 2, 2)

