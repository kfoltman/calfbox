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

#include <stdint.h>

#define CBOX_BLOCK_SIZE 16

typedef float cbox_sample_t;

struct cbox_module_keyrange_metadata
{
    uint8_t channel; // 0 = omni
    uint8_t low_key;
    uint8_t high_key;
    const char *name;
};

enum cbox_module_livecontroller_class
{
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

struct cbox_module_metadata
{
    struct cbox_module_keyrange_metadata *keyranges;
    int num_keyranges;

    struct cbox_module_livecontroller_metadata *live_controllers;
    int num_live_controllers;

    struct cbox_module_voicingparam_metadata *voicing_params;
    int num_voicing_params;
};

struct cbox_module
{
    void *user_data;
    
    void (*process_event)(void *user_data, const uint8_t *data, uint32_t len);
    void (*process_block)(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs);
};

struct cbox_module_manifest
{
    void *user_data;
    const char *name;
    int inputs;
    int outputs;
    struct cbox_module_metadata *metadata;
    
    struct cbox_module *(*create)(void *user_data, const char *cfg_section);
};

#define DEFINE_MODULE(name, ninputs, noutputs) \
    struct cbox_module_metadata name##_metadata = { name##_keyranges, sizeof(name##_keyranges)/sizeof(name##_keyranges[0]), NULL, 0, NULL, 0 }; \
    struct cbox_module_manifest name##_module = { NULL, #name, .inputs = ninputs, .outputs = noutputs, .metadata = &name##_metadata, .create = name##_create };

extern struct cbox_module_manifest fluidsynth_module;
extern struct cbox_module_manifest tonewheel_organ_module;
extern struct cbox_module_manifest *cbox_module_list[];
