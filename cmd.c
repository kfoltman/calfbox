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

#include "cmd.h"
#include <assert.h>
#include <glib.h>
#include <malloc.h>
#include <string.h>

void cbox_execute_on(struct cbox_command_target *ct, const char *cmd_name, const char *args, ...)
{
    va_list av;
    
    va_start(av, args);
    cbox_execute_on_v(ct, cmd_name, args, av);
    va_end(av);
}

void cbox_execute_on_v(struct cbox_command_target *ct, const char *cmd_name, const char *args, va_list av)
{
    int argcount = 0;
    struct cbox_osc_command cmd;
    uint8_t *extra_data;
    // XXXKF might be not good enough for weird platforms
    int unit_size = sizeof(double);
    // this must be a power of 2 to guarantee proper alignment
    assert(unit_size >= sizeof(int) && (unit_size == 4 || unit_size == 8));
    cmd.command = cmd_name;
    cmd.arg_types = args;
    for (int i = 0; args[i]; i++)
        argcount = i + 1;
    // contains pointers to all the values, plus values themselves in case of int/double
    // (casting them to pointers is ugly, and va_arg does not return a lvalue)
    cmd.arg_values = malloc(sizeof(void *) * argcount + unit_size * argcount);
    extra_data = (uint8_t *)&cmd.arg_values[argcount];
    
    for (int i = 0; i < argcount; i++)
    {
        int iv;
        double fv;
        void *pv = extra_data + unit_size * i;
        switch(args[i])
        {
            case 's':
                cmd.arg_values[i] = va_arg(av, char *);
                break;
            case 'i':
                iv = va_arg(av, int);
                memcpy(pv, &iv, sizeof(int));
                cmd.arg_values[i] = pv;
                break;
            case 'f': // double really
                fv = (double)va_arg(av, double);
                memcpy(pv, &fv, sizeof(double));
                cmd.arg_values[i] = pv;
                break;
            default:
                g_error("Invalid format specification '%c'", args[i]);
                assert(0);
        }
    }
    ct->process_cmd(ct, &cmd);
    free(cmd.arg_values);
}

