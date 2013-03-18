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

#ifndef CBOX_SAMPLER_IMPL_H
#define CBOX_SAMPLER_IMPL_H

extern void sampler_gen_reset(struct sampler_gen *v);
extern uint32_t sampler_gen_sample_playback(struct sampler_gen *v, float **tmp_outputs);
extern void sampler_program_change_byidx(struct sampler_module *m, struct sampler_channel *c, int program_idx);
extern void sampler_program_change(struct sampler_module *m, struct sampler_channel *c, int program);

#endif
