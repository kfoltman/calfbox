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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct cbox_module_manifest fluidsynth_module;
extern struct cbox_module_manifest tonewheel_organ_module;
extern struct cbox_module_manifest stream_player_module;
extern struct cbox_module_manifest tone_control_module;
extern struct cbox_module_manifest delay_module;
extern struct cbox_module_manifest parametric_eq_module;
extern struct cbox_module_manifest phaser_module;
extern struct cbox_module_manifest chorus_module;

struct cbox_module_manifest *cbox_module_list[] = {
    &tonewheel_organ_module,
    &fluidsynth_module,
    &stream_player_module,
    &tone_control_module,
    &delay_module,
    &parametric_eq_module,
    &phaser_module,
    &chorus_module,
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

struct cbox_module_manifest *cbox_module_get_by_name(const char *name)
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
    
    return module;
}

void cbox_module_destroy(struct cbox_module *module)
{
    free(module->input_samples);
    free(module->output_samples);
}
