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

#include "errors.h"

GQuark cbox_module_error_quark()
{
    return g_quark_from_string("cbox-module-error-quark");
}

void cbox_force_error(GError **error)
{
    if (error && !*error)
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "unknown error");
}

void cbox_print_error(GError *error)
{
    if (!error)
    {
        g_warning("Unspecified error");
        return;
    }
    g_warning("%s", error->message);
    g_error_free(error);
}

void cbox_print_error_if(GError *error)
{
    if (!error)
        return;
    g_warning("%s", error->message);
    g_error_free(error);
}

gboolean cbox_set_command_error(GError **error, const struct cbox_osc_command *cmd)
{
    if (error && !*error)
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_OUT_OF_RANGE, "Invalid command '%s' with args '%s'", cmd->command, cmd->arg_types);
    return FALSE;
}

gboolean cbox_set_range_error(GError **error, const char *param, double minv, double maxv)
{
    if (error && !*error)
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_OUT_OF_RANGE, "Parameter %s not within a valid range of [%f, %f]", param, minv, maxv);
    return FALSE;
}
