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

#ifndef CBOX_PROCMAIN_H
#define CBOX_PROCMAIN_H

#include <stdint.h>

struct cbox_scene;
struct cbox_io;

struct cbox_rt_cmd
{
    void *user_data;
    void (*prepare)(struct cbox_rt_cmd *cmd);
    void (*execute)(struct cbox_rt_cmd *cmd);
    void (*cleanup)(struct cbox_rt_cmd *cmd);
};

struct cbox_rt
{
    struct cbox_scene *scene;
    struct cbox_module *effect;
};

extern struct cbox_rt *cbox_rt_new();
extern void cbox_rt_process(void *user_data, struct cbox_io *io, uint32_t nframes);

extern void cbox_rt_cmd_execute_sync(struct cbox_rt_cmd *cmd);
extern void cbox_rt_cmd_execute_async(struct cbox_rt_cmd *cmd);
extern void cbox_rt_destroy(struct cbox_rt *);

#endif
