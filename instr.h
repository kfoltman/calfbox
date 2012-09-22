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

#ifndef CBOX_INSTR_H
#define CBOX_INSTR_H

#include "recsrc.h"

CBOX_EXTERN_CLASS(cbox_instrument)

struct cbox_module;
struct cbox_rt;
struct cbox_scene;
struct cbox_instruments;

struct cbox_instrument_output
{
    struct cbox_module *insert;
    int output_bus;
    float gain;
    struct cbox_recording_source rec_dry, rec_wet;
};

struct cbox_instrument
{
    CBOX_OBJECT_HEADER()
    struct cbox_command_target cmd_target;
    struct cbox_module *module;
    struct cbox_instrument_output *outputs;
    struct cbox_scene *scene;
    int refcount;
    gchar **aux_output_names;
    struct cbox_aux_bus **aux_outputs;
    int aux_output_count;
};

extern void cbox_instrument_unref_aux_buses(struct cbox_instrument *instrument);
extern void cbox_instrument_disconnect_aux_bus(struct cbox_instrument *instrument, struct cbox_aux_bus *bus);
extern void cbox_instrument_destroy_if_unused(struct cbox_instrument *instrument);
extern gboolean cbox_instrument_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

extern void cbox_instrument_output_init(struct cbox_instrument_output *output, struct cbox_scene *scene, uint32_t max_numsamples);
extern void cbox_instrument_output_uninit(struct cbox_instrument_output *output);

#endif
