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

struct cbox_module
{
    void *user_data;
    
    void (*process_event)(void *user_data, const uint8_t *data, uint32_t len);
    void (*process_block)(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs);
};

struct cbox_module_manifest
{
    void *user_data;
    int inputs;
    int outputs;
    
    struct cbox_module *(*create)(void *user_data);
};

extern struct cbox_module_manifest tonewheel_organ_module;
