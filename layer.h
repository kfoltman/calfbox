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

#ifndef CBOX_LAYER_H
#define CBOX_LAYER_H

#include <glib.h>
#include <stdint.h>

struct cbox_module;

struct cbox_layer
{
    struct cbox_instrument *instrument;
    int8_t in_channel; // -1 for Omni
    int8_t out_channel; // -1 for Omni
    uint8_t low_note;
    uint8_t high_note;
    int8_t transpose;
    int8_t fixed_note;
    int disable_aftertouch;
    int invert_sustain;
};

extern struct cbox_layer *cbox_layer_new(const char *name, GError **error);
extern struct cbox_layer *cbox_layer_load(const char *module_name, GError **error);

#endif
