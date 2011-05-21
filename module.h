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

#ifndef CBOX_MODULE_H
#define CBOX_MODULE_H

#include "cmd.h"
#include "cmd.h"
#include "dspmath.h"
#include "midi.h"

#include <glib.h>
#include <stdint.h>

#define CBOX_MAX_AUDIO_PORTS 32

struct cbox_module_keyrange_metadata
{
    uint8_t channel; // 0 = omni
    uint8_t low_key;
    uint8_t high_key;
    const char *name;
};

enum cbox_module_livecontroller_class
{
    cmlc_onoffcc,
    cmlc_continuouscc,
    cmlc_continuous,
    cmlc_discrete,
    cmlc_enum
};

struct cbox_module_livecontroller_metadata
{
    uint8_t channel;
    enum cbox_module_livecontroller_class controller_class:8;
    uint16_t controller;
    const char *name;
    void *extra_info;
};

struct cbox_module_voicingparam_metadata
{
};

struct cbox_module
{
    void *user_data;
    cbox_sample_t *input_samples;
    cbox_sample_t *output_samples;
    struct cbox_midi_buffer midi_input;
    int inputs, outputs;
    
    struct cbox_command_target cmd_target;
        
    void (*process_event)(struct cbox_module *module, const uint8_t *data, uint32_t len);
    void (*process_block)(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs);
    void (*destroy)(struct cbox_module *module);
};

struct cbox_module_manifest
{
    void *user_data;
    const char *name;
    int min_inputs;
    int min_outputs;
    
    struct cbox_module_keyrange_metadata *keyranges;
    int num_keyranges;

    struct cbox_module_livecontroller_metadata *live_controllers;
    int num_live_controllers;

    struct cbox_module_voicingparam_metadata *voicing_params;
    int num_voicing_params;
    
    struct cbox_module *(*create)(void *user_data, const char *cfg_section, int srate, GError **error);
};

#define DEFINE_MODULE(modname, ninputs, noutputs) \
    struct cbox_module_manifest modname##_module = { \
        NULL, \
        .name = #modname, \
        .min_inputs = ninputs, \
        .min_outputs = noutputs, \
        .keyranges = modname##_keyranges, \
        .num_keyranges = sizeof(modname##_keyranges)/sizeof(modname##_keyranges[0]), \
        .live_controllers = modname##_controllers, \
        .num_live_controllers = sizeof(modname##_controllers)/sizeof(modname##_controllers[0]), \
        .create = modname##_create \
    };

extern struct cbox_module_manifest *cbox_module_list[];

extern void cbox_module_manifest_dump(struct cbox_module_manifest *manifest);
extern struct cbox_module_manifest *cbox_module_manifest_get_by_name(const char *name);
extern struct cbox_module *cbox_module_manifest_create_module(struct cbox_module_manifest *manifest, const char *cfg_section, int srate, GError **error);

extern void cbox_module_init(struct cbox_module *module, void *user_data, int inputs, int outputs);
extern void cbox_module_destroy(struct cbox_module *module);

#endif
