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

#include <stdarg.h>
#include <stdint.h>

struct cbox_osc_command
{
    const char *command;
    const char *arg_types;
    void **arg_values;
};

struct cbox_command_target
{
    void *user_data;
    
    void (*process_cmd)(struct cbox_command_target *ct, struct cbox_osc_command *cmd);
};

extern void cbox_execute_on(struct cbox_command_target *module, const char *cmd, const char *args, ...);

#endif
