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

#ifndef CBOX_ERRORS_H
#define CBOX_ERRORS_H

#include <glib.h>
#include "cmd.h"

#define CBOX_MODULE_ERROR cbox_module_error_quark()

enum CboxModuleError
{
    CBOX_MODULE_ERROR_FAILED,
    CBOX_MODULE_ERROR_INVALID_COMMAND,
    CBOX_MODULE_ERROR_OUT_OF_RANGE,
};

struct cbox_osc_command;

extern GQuark cbox_module_error_quark(void);
extern void cbox_force_error(GError **error);
extern void cbox_print_error(GError *error);
extern void cbox_print_error_if(GError *error);
extern gboolean cbox_set_command_error(GError **error, const struct cbox_osc_command *cmd);
extern gboolean cbox_set_command_error_with_msg(GError **error, const struct cbox_osc_command *cmd, const char *extra_msg);
extern gboolean cbox_set_range_error(GError **error, const char *param, double minv, double maxv);

#endif
