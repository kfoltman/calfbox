/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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

#ifndef CBOX_IOENV_H
#define CBOX_IOENV_H

struct cbox_io_env
{
    int srate;
    uint32_t buffer_size;    
    uint32_t input_count, output_count;
};

static inline void cbox_io_env_clear(struct cbox_io_env *env)
{
    env->srate = 0;
    env->buffer_size = 0;
    env->input_count = 0;
    env->output_count = 0;
}

static inline void cbox_io_env_copy(struct cbox_io_env *dest, const struct cbox_io_env *src)
{
    dest->srate = src->srate;
    dest->buffer_size = src->buffer_size;
    dest->input_count = src->input_count;
    dest->output_count = src->output_count;
}

#endif

