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
    NULL
};

void cbox_module_manifest_dump(struct cbox_module_manifest *manifest)
{
    static const char *ctl_classes[] = { "Switch CC#", "Continuous CC#", "Cont. Param", "Discrete Param", "Enum" };
    int i = 0;
    printf("Module: %s\n", manifest->name);
    printf("Audio I/O: %d inputs, %d outputs\n", manifest->inputs, manifest->outputs);
    
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

struct cbox_module *cbox_module_manifest_create_module(struct cbox_module_manifest *manifest, const char *cfg_section, int srate)
{
    struct cbox_module *module = manifest->create(manifest->user_data, cfg_section, srate);
    if (!module)
        return NULL;
    
    module->input_samples = malloc(sizeof(float) * CBOX_BLOCK_SIZE * manifest->inputs);
    module->output_samples = malloc(sizeof(float) * CBOX_BLOCK_SIZE * manifest->outputs);
    cbox_midi_buffer_init(&module->midi_input);
    
    return module;
}

void cbox_module_init(struct cbox_module *module, void *user_data)
{
    module->user_data = user_data;
    module->input_samples = NULL;
    module->output_samples = NULL;
    
    module->process_event = NULL;
    module->process_block = NULL;
    module->destroy = NULL;
}

void cbox_module_do(struct cbox_module *module, const char *cmd_name, const char *args, ...)
{
    va_list av;
    int argcount = 0;
    struct cbox_osc_command cmd;
    uint8_t *extra_data;
    // XXXKF might be not good enough for weird platforms
    int unit_size = sizeof(double);
    // this must be a power of 2 to guarantee proper alignment
    assert(unit_size >= sizeof(int) && (unit_size == 4 || unit_size == 8));
    cmd.command = cmd_name;
    cmd.arg_types = args;
    for (int i = 0; args[i]; i++)
        argcount = i + 1;
    // contains pointers to all the values, plus values themselves in case of int/double
    // (casting them to pointers is ugly, and va_arg does not return a lvalue)
    cmd.arg_values = malloc(sizeof(void *) * argcount + unit_size * argcount);
    extra_data = (uint8_t *)&cmd.arg_values[argcount];
    
    va_start(av, args);
    for (int i = 0; i < argcount; i++)
    {
        int iv;
        double fv;
        void *pv = extra_data + unit_size * i;
        switch(args[i])
        {
            case 's':
                cmd.arg_values[i] = va_arg(av, char *);
                break;
            case 'i':
                iv = va_arg(av, int);
                memcpy(pv, &iv, sizeof(int));
                cmd.arg_values[i] = pv;
                break;
            case 'f': // double really
                fv = (double)va_arg(av, double);
                memcpy(pv, &fv, sizeof(double));
                cmd.arg_values[i] = pv;
                break;
            default:
                g_error("Invalid format specification '%c'", args[i]);
                assert(0);
        }
    }
    va_end(av);
    module->process_cmd(module, &cmd);
    free(cmd.arg_values);
}

void cbox_module_destroy(struct cbox_module *module)
{
    free(module->input_samples);
    free(module->output_samples);
    if (module->destroy)
        module->destroy(module);
}
