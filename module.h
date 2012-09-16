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

#include "dom.h"
#include "dspmath.h"
#include "errors.h"
#include "midi.h"

#include <stdint.h>

CBOX_EXTERN_CLASS(cbox_module)

#define CBOX_MAX_AUDIO_PORTS 32

struct cbox_rt;

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
    CBOX_OBJECT_HEADER()
    void *user_data;
    struct cbox_rt *rt;
    const char *engine_name;
    gchar *instance_name;
    cbox_sample_t *input_samples;
    cbox_sample_t *output_samples;
    struct cbox_midi_buffer midi_input;
    int inputs, outputs, aux_offset;
    int bypass;
    int srate;
    
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
    
    struct cbox_module *(*create)(void *user_data, const char *cfg_section, struct cbox_rt *rt, GError **error);
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
extern struct cbox_module *cbox_module_manifest_create_module(struct cbox_module_manifest *manifest, const char *cfg_section, struct cbox_rt *rt, const char *instance_name, GError **error);

extern struct cbox_module *cbox_module_new_from_fx_preset(const char *name, struct cbox_rt *rt, GError **error);

extern void cbox_module_init(struct cbox_module *module, struct cbox_rt *rt, void *user_data, int inputs, int outputs, cbox_process_cmd cmd_handler, void (*destroy)(struct cbox_module *module));
extern void cbox_module_swap_pointers_and_free(struct cbox_module *sm, void **pptr, void *value);

extern gboolean cbox_module_slot_process_cmd(struct cbox_module **psm, struct cbox_command_target *fb, struct cbox_osc_command *cmd, const char *subcmd, struct cbox_rt *rt, GError **error);

#define EFFECT_PARAM_CLONE(res) \
    struct MODULE_PARAMS *res = malloc(sizeof(struct MODULE_PARAMS)); \
    memcpy(res, m->params, sizeof(struct MODULE_PARAMS)); \

#define EFFECT_PARAM(path, type, field, ctype, expr, minv, maxv) \
    if (!strcmp(cmd->command, path) && !strcmp(cmd->arg_types, type)) \
    { \
        ctype value = *(ctype *)cmd->arg_values[0]; \
        if (value < minv || value > maxv) \
            return cbox_set_range_error(error, path, minv, maxv);\
        EFFECT_PARAM_CLONE(pp); \
        pp->field = expr(value); \
        cbox_module_swap_pointers_and_free(&m->module, (void **)&m->params, pp); \
    } \

#define EFFECT_PARAM_ARRAY(path, type, array, field, ctype, expr, minv, maxv) \
    if (!strcmp(cmd->command, path) && !strcmp(cmd->arg_types, "i" type)) \
    { \
        int pos = *(int *)cmd->arg_values[0]; \
        ctype value = *(ctype *)cmd->arg_values[1]; \
        if (value < minv || value > maxv) \
            return cbox_set_range_error(error, path, minv, maxv);\
        EFFECT_PARAM_CLONE(pp); \
        pp->array[pos].field = expr(value); \
        cbox_module_swap_pointers_and_free(&m->module, (void **)&m->params, pp); \
    } \

#define MODULE_CREATE_FUNCTION(module) \
    struct cbox_module *module##_create(void *user_data, const char *cfg_section, struct cbox_rt *rt, GError **error)

#define MODULE_PROCESSCMD_FUNCTION(module) \
    gboolean module##_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)

#define MODULE_SIMPLE_DESTROY_FUNCTION(module) \
    static void module##_destroyfunc(struct cbox_module *module_) \
    { \
        struct module##_module *m = (struct module##_module *)module_; \
        free(m->params); \
    }

#define CALL_MODULE_INIT(m, inputs, outputs, name) \
    cbox_module_init(&(m)->module, rt, (m), inputs, outputs, name##_process_cmd, name##_destroyfunc);

#define CALL_MODULE_INIT_SIMPLE(m, inputs, outputs) \
    cbox_module_init(&(m)->module, rt, (m), inputs, outputs, NULL, NULL);


#endif
