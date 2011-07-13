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
#include <glib.h>
#include <malloc.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

#define MODULE_PARAMS {name}_params

struct {name}_params
{
};

struct {name}_module
{
    struct cbox_module module;

    struct {name}_params *params;
};

gboolean {name}_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct {name}_module *m = (struct {name}_module *)ct->user_data;
    
    // EFFECT_PARAM("/wet_dry", "f", wet_dry, double, , 0, 1) else
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        // return cbox_execute_on(fb, NULL, "/wet_dry", "f", error, m->params->wet_dry);
        return TRUE;
    }
    else
        return cbox_set_command_error(error, cmd);
    return TRUE;
}

void {name}_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct {name}_module *m = module->user_data;
}

void {name}_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct {name}_module *m = module->user_data;
}

struct cbox_module *{name}_create(void *user_data, const char *cfg_section, int srate, GError **error)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct {name}_module *m = malloc(sizeof(struct {name}_module));
    cbox_module_init(&m->module, m, 0, 2, {name}_process_cmd);
    m->module.process_event = {name}_process_event;
    m->module.process_block = {name}_process_block;
    struct {name}_params *p = malloc(sizeof(struct {name}_params));
    
    return &m->module;
}


struct cbox_module_keyrange_metadata {name}_keyranges[] = {
};

struct cbox_module_livecontroller_metadata {name}_controllers[] = {
};

DEFINE_MODULE({name}, 0, 2)

