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

#ifndef CBOX_PATTERN_MAKER_H
#define CBOX_PATTERN_MAKER_H

#include <glib.h>

struct cbox_midi_pattern;
struct cbox_midi_pattern_maker;

extern struct cbox_midi_pattern_maker *cbox_midi_pattern_maker_new();

extern gboolean cbox_midi_pattern_maker_load_smf(struct cbox_midi_pattern_maker *maker, const char *filename, int *length, GError **error);

extern void cbox_midi_pattern_maker_add(struct cbox_midi_pattern_maker *maker, uint32_t time, uint8_t cmd, uint8_t val1, uint8_t val2);
extern void cbox_midi_pattern_maker_add_mem(struct cbox_midi_pattern_maker *maker, uint32_t time, const uint8_t *src, uint32_t len);

extern struct cbox_midi_pattern *cbox_midi_pattern_maker_create_pattern(struct cbox_midi_pattern_maker *maker);

extern void cbox_midi_pattern_maker_destroy(struct cbox_midi_pattern_maker *maker);

#endif
