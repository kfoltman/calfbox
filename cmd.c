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

#include "blob.h"
#include "cmd.h"
#include "dom.h"
#include "errors.h"
#include <assert.h>
#include <glib.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

void cbox_command_target_init(struct cbox_command_target *ct, cbox_process_cmd cmd, void *user_data)
{
    ct->process_cmd = cmd;
    ct->user_data = user_data;
}

gboolean cbox_execute_on(struct cbox_command_target *ct, struct cbox_command_target *fb, const char *cmd_name, const char *args, GError **error, ...)
{
    va_list av;
    
    va_start(av, error);
    gboolean res = cbox_execute_on_v(ct, fb, cmd_name, args, av, error);
    va_end(av);
    return res;
}

gboolean cbox_execute_on_v(struct cbox_command_target *ct, struct cbox_command_target *fb, const char *cmd_name, const char *args, va_list av, GError **error)
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
            case 'b':
                cmd.arg_values[i] = va_arg(av, struct cbox_blob *);
                break;
            case 'o':
                cmd.arg_values[i] = va_arg(av, struct cbox_objhdr *);
                break;
            default:
                g_error("Invalid format character '%c' for command '%s'", args[i], cmd_name);
                assert(0);
        }
    }
    gboolean result = ct->process_cmd(ct, fb, &cmd, error);
    free(cmd.arg_values);
    return result;
}

gboolean cbox_osc_command_dump(const struct cbox_osc_command *cmd)
{
    g_message("Command = %s, args = %s", cmd->command, cmd->arg_types);
    for (int i = 0; cmd->arg_types[i]; i++)
    {
        switch(cmd->arg_types[i])
        {
            case 's':
                g_message("Args[%d] = '%s'", i, (const char *)cmd->arg_values[i]);
                break;
            case 'o':
            {
                struct cbox_objhdr *oh = cmd->arg_values[i];
                char buf[40];
                uuid_unparse(oh->instance_uuid.uuid, buf);
                g_message("Args[%d] = uuid:'%s'", i, buf);
                break;
            }
            case 'i':
                g_message("Args[%d] = %d", i, *(int *)cmd->arg_values[i]);
                break;
            case 'f':
                g_message("Args[%d] = %f", i, *(double *)cmd->arg_values[i]);
                break;
            case 'b':
            {
                struct cbox_blob *b = cmd->arg_values[i];
                g_message("Args[%d] = (%p, %d)", i, b->data, (int)b->size);
                break;
            }
            default:
                g_error("Invalid format character '%c' for command '%s'", cmd->arg_types[i], cmd->command);
                assert(0);
        }
    }
}

gboolean cbox_check_fb_channel(struct cbox_command_target *fb, const char *command, GError **error)
{
    if (fb)
        return TRUE;
    
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Feedback channel required for command '%s'", command);    
    return FALSE;
}


gboolean cbox_execute_sub(struct cbox_command_target *ct, struct cbox_command_target *fb, const struct cbox_osc_command *cmd, const char *new_command, GError **error)
{
    struct cbox_osc_command subcmd;
    subcmd.command = new_command;
    subcmd.arg_types = cmd->arg_types;
    subcmd.arg_values = cmd->arg_values;
    return ct->process_cmd(ct, fb, &subcmd, error);
}

gboolean cbox_parse_path_part_int(const struct cbox_osc_command *cmd, const char *path, const char **subcommand, int *index, int min_index, int max_index, GError **error)
{
    char *numcopy = NULL;
    if (!cbox_parse_path_part_str(cmd, path, subcommand, &numcopy, error))
        return FALSE;
    if (!*subcommand)
        return TRUE;
    char *endptr = NULL;
    *index = strtol(numcopy, &endptr, 10);
    if (!*numcopy && *endptr)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid index %s for command %s", numcopy, cmd->command);
        g_free(numcopy);
        *subcommand = NULL;
        return TRUE;
    }
    if (*index < min_index || *index > max_index)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Index %s out of range [%d, %d] for command %s", numcopy, min_index, max_index, cmd->command);
        g_free(numcopy);
        *subcommand = NULL;
        return TRUE;
    }
    g_free(numcopy);
    return TRUE;
}

gboolean cbox_parse_path_part_str(const struct cbox_osc_command *cmd, const char *path, const char **subcommand, char **path_element, GError **error)
{
    *path_element = NULL;
    *subcommand = NULL;
    int plen = strlen(path);
    if (!strncmp(cmd->command, path, plen))
    {
        const char *num = cmd->command + plen;
        const char *slash = strchr(num, '/');
        if (!slash)
        {
            cbox_set_command_error_with_msg(error, cmd, "needs at least one extra path element");
            return TRUE;
        }
        
        *path_element = g_strndup(num, slash-num);
        *subcommand = slash;
        return TRUE;
    }
    return FALSE;
}
