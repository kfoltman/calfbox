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
#include "config-api.h"
#include "module.h"

#include <assert.h>
#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct cbox_module_manifest sampler_module;
extern struct cbox_module_manifest fluidsynth_module;
extern struct cbox_module_manifest tonewheel_organ_module;
extern struct cbox_module_manifest stream_player_module;
extern struct cbox_module_manifest tone_control_module;
extern struct cbox_module_manifest delay_module;
extern struct cbox_module_manifest reverb_module;
extern struct cbox_module_manifest parametric_eq_module;
extern struct cbox_module_manifest phaser_module;
extern struct cbox_module_manifest chorus_module;
extern struct cbox_module_manifest fxchain_module;
extern struct cbox_module_manifest jack_input_module;
extern struct cbox_module_manifest feedback_reducer_module;

struct cbox_module_manifest *cbox_module_list[] = {
    &tonewheel_organ_module,
    &fluidsynth_module,
    &stream_player_module,
    &tone_control_module,
    &delay_module,
    &reverb_module,
    &parametric_eq_module,
    &phaser_module,
    &chorus_module,
    &sampler_module,
    &fxchain_module,
    &jack_input_module,
    &feedback_reducer_module,
    NULL
};

void cbox_module_manifest_dump(struct cbox_module_manifest *manifest)
{
    static const char *ctl_classes[] = { "Switch CC#", "Continuous CC#", "Cont. Param", "Discrete Param", "Enum" };
    int i = 0;
    printf("Module: %s\n", manifest->name);
    printf("Audio I/O: min %d inputs, min %d outputs\n", manifest->min_inputs, manifest->min_outputs);
    
    printf("Live controllers:\n");
    printf("Ch#             Type Number Name                          \n");
    printf("---- --------------- ------ ------------------------------\n");
    for (i = 0; i < manifest->num_live_controllers; i++)
    {
        struct cbox_module_livecontroller_metadata *lc = &manifest->live_controllers[i];
        if (lc->channel == 255)
            printf("ALL  ");
        else
        if (!lc->channel)
            printf("ANY  ");
        else
            printf("%-4d ", lc->channel);
        printf("%15s %-6d %-30s\n", ctl_classes[lc->controller_class], lc->controller, lc->name);
    }
}

struct cbox_module_manifest *cbox_module_manifest_get_by_name(const char *name)
{
    struct cbox_module_manifest **mptr;
    
    for (mptr = cbox_module_list; *mptr; mptr++)
    {
        if (!strcmp((*mptr)->name, name))
            return *mptr;
    }
    return NULL;
}

struct cbox_module *cbox_module_manifest_create_module(struct cbox_module_manifest *manifest, const char *cfg_section, int srate, const char *instance_name, GError **error)
{
    g_clear_error(error);
    struct cbox_module *module = manifest->create(manifest->user_data, cfg_section, srate, error);
    if (!module)
        return NULL;

    module->instance_name = g_strdup(instance_name);
    module->input_samples = malloc(sizeof(float) * CBOX_BLOCK_SIZE * module->inputs);
    module->output_samples = malloc(sizeof(float) * CBOX_BLOCK_SIZE * module->outputs);
    module->engine_name = manifest->name;
    cbox_midi_buffer_init(&module->midi_input);
    
    return module;
}

void cbox_module_init(struct cbox_module *module, void *user_data, int inputs, int outputs)
{
    module->user_data = user_data;
    module->instance_name = NULL;
    module->input_samples = NULL;
    module->output_samples = NULL;
    module->inputs = inputs;
    module->outputs = outputs;
    module->aux_offset = outputs;
    
    module->cmd_target.process_cmd = NULL;
    module->cmd_target.user_data = module;
    module->process_event = NULL;
    module->process_block = NULL;
    module->destroy = NULL;
}

struct cbox_module *cbox_module_new_from_fx_preset(const char *name, GError **error)
{
    gchar *section = g_strdup_printf("fxpreset:%s", name);
    const char *engine;
    struct cbox_module_manifest *mptr;
    struct cbox_module *effect;
    
    if (!cbox_config_has_section(section))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No FX preset called '%s'", name);
        goto fxpreset_error;
    }
    engine = cbox_config_get_string(section, "engine");
    if (!engine)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "FX engine not specified for preset '%s'", name);
        goto fxpreset_error;
    }
    mptr = cbox_module_manifest_get_by_name(engine);
    if (!mptr)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "FX preset '%s' refers to non-existing engine '%s'", name, engine);
        goto fxpreset_error;
    }
    effect = cbox_module_manifest_create_module(mptr, section, cbox_io_get_sample_rate(&app.io), section, error);
    if (!effect)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Could not instantiate FX preset '%s': ", name);
        goto fxpreset_error;
    }
    g_free(section);
    return effect;
    
fxpreset_error:
    g_free(section);
    return NULL;
}

void cbox_module_destroy(struct cbox_module *module)
{
    g_free(module->instance_name);
    free(module->input_samples);
    free(module->output_samples);
    if (module->destroy)
        module->destroy(module);
}
