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

#ifndef CBOX_CMD_H
#define CBOX_CMD_H

#include <glib.h>
#include <stdarg.h>
#include <stdint.h>

struct cbox_command_target;

struct cbox_blob
{
    void *data;
    size_t size;
};

struct cbox_osc_command
{
    const char *command;
    const char *arg_types;
    void **arg_values;
};

typedef gboolean (*cbox_process_cmd)(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);

struct cbox_command_target
{
    void *user_data;
    cbox_process_cmd process_cmd;
};

void cbox_command_target_init(struct cbox_command_target *ct, cbox_process_cmd cmd, void *user_data);

extern gboolean cbox_check_fb_channel(struct cbox_command_target *fb, const char *command, GError **error);

extern gboolean cbox_execute_sub(struct cbox_command_target *ct, struct cbox_command_target *fb, const struct cbox_osc_command *cmd, const char *new_command, GError **error);
extern gboolean cbox_execute_on(struct cbox_command_target *ct, struct cbox_command_target *fb, const char *cmd, const char *args, GError **error, ...);
extern gboolean cbox_execute_on_v(struct cbox_command_target *ct, struct cbox_command_target *fb, const char *cmd, const char *args, va_list va, GError **error);

extern gboolean cbox_osc_command_dump(const struct cbox_osc_command *cmd);

#endif
