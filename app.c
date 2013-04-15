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

#include "app.h"
#include "blob.h"
#include "config-api.h"
#include "engine.h"
#include "instr.h"
#include "io.h"
#include "layer.h"
#include "menu.h"
#include "menuitem.h"
#include "meter.h"
#include "midi.h"
#include "module.h"
#include "scene.h"
#include "seq.h"
#include "song.h"
#include "track.h"
#include "ui.h"
#include "wavebank.h"

#include <assert.h>
#include <glib.h>
#include <glob.h>
#include <getopt.h>
#include <math.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static gboolean app_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    if (!cmd->command)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "NULL command");
        return FALSE;
    }
    if (cmd->command[0] != '/')
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid global command path '%s'", cmd->command);
        return FALSE;
    }
    const char *obj = &cmd->command[1];
    const char *pos = strchr(obj, '/');
    if (pos)
    {
        if (!strncmp(obj, "master/", 7))
            return cbox_execute_sub(&app.engine->master->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "config/", 7))
            return cbox_execute_sub(&app.config_cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "scene/", 6))
            return cbox_execute_sub(&app.engine->scenes[0]->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "engine/", 7))
            return cbox_execute_sub(&app.engine->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "rt/", 3))
            return cbox_execute_sub(&app.rt->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "io/", 3))
            return cbox_execute_sub(&app.io.cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "song/", 5) && app.engine->master->song)
            return cbox_execute_sub(&app.engine->master->song->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "waves/", 6))
            return cbox_execute_sub(&cbox_waves_cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "doc/", 4))
            return cbox_execute_sub(cbox_document_get_cmd_target(app.document), fb, cmd, pos, error);
        else
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
            return FALSE;
        }
    }
    else
    if (!strcmp(obj, "on_idle") && !strcmp(cmd->arg_types, ""))
    {
        return cbox_app_on_idle(fb, error);
    }
    else
    if (!strcmp(obj, "send_event_to") && (!strcmp(cmd->arg_types, "siii") || !strcmp(cmd->arg_types, "sii") || !strcmp(cmd->arg_types, "si")))
    {
        const char *output = CBOX_ARG_S(cmd, 0);
        struct cbox_midi_merger *merger = NULL;
        if (*output)
        {
            struct cbox_uuid uuid;
            if (!cbox_uuid_fromstring(&uuid, output, error))
                return FALSE;
            
            merger = cbox_rt_get_midi_output(app.rt, &uuid);
            if (!merger)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown MIDI output UUID: '%s'", output);
                return FALSE;
            }
        }
        else
        {
            if (!app.engine->scene_count)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Scene not set");
                return FALSE;
            }
            merger = &app.engine->scenes[0]->scene_input_merger;
        }
        int mcmd = CBOX_ARG_I(cmd, 1);
        int arg1 = 0, arg2 = 0;
        if (cmd->arg_types[2] == 'i')
        {
            arg1 = CBOX_ARG_I(cmd, 2);
            if (cmd->arg_types[3] == 'i')
                arg2 = CBOX_ARG_I(cmd, 3);
        }
        struct cbox_midi_buffer buf;
        cbox_midi_buffer_init(&buf);
        cbox_midi_buffer_write_inline(&buf, 0, mcmd, arg1, arg2);
        cbox_engine_send_events_to(app.engine, merger, &buf);
        return TRUE;
    }
    else
    if (!strcmp(obj, "update_playback") && !strcmp(cmd->arg_types, ""))
    {
        cbox_engine_update_song_playback(app.engine);
        return TRUE;
    }
    else
    if (!strcmp(obj, "get_pattern") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        if (app.engine->master->song && app.engine->master->song->tracks)
        {
            struct cbox_track *track = app.engine->master->song->tracks->data;
            if (track)
            {
                struct cbox_track_item *item = track->items->data;
                struct cbox_midi_pattern *pattern = item->pattern;
                int length = 0;
                struct cbox_blob *blob = cbox_midi_pattern_to_blob(pattern, &length);
                gboolean res = cbox_execute_on(fb, NULL, "/pattern", "bi", error, blob, length);
                cbox_blob_destroy(blob);
                if (!res)
                    return FALSE;
            }
        }
        return TRUE;
    }
    else
    if (!strcmp(obj, "new_meter") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct cbox_meter *meter = cbox_meter_new(app.document, app.rt->io_env.srate);

        return cbox_execute_on(fb, NULL, "/uuid", "o", error, meter);
    }
    else
    if (!strcmp(obj, "new_engine") && !strcmp(cmd->arg_types, "ii"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct cbox_engine *e = cbox_engine_new(app.document, NULL);
        e->io_env.srate = CBOX_ARG_I(cmd, 0);
        e->io_env.buffer_size = CBOX_ARG_I(cmd, 1);

        return e ? cbox_execute_on(fb, NULL, "/uuid", "o", error, e) : FALSE;
    }
    else
    if (!strcmp(obj, "print_s") && !strcmp(cmd->arg_types, "s"))
    {
        g_message("Print: %s", CBOX_ARG_S(cmd, 0));
        return TRUE;
    }
    else
    if (!strcmp(obj, "print_i") && !strcmp(cmd->arg_types, "i"))
    {
        g_message("Print: %d", CBOX_ARG_I(cmd, 0));
        return TRUE;
    }
    else
    if (!strcmp(obj, "print_f") && !strcmp(cmd->arg_types, "f"))
    {
        g_message("Print: %f", CBOX_ARG_F(cmd, 0));
        return TRUE;
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

struct config_foreach_data
{
    const char *prefix;
    const char *command;
    struct cbox_command_target *fb;
    GError **error;
    gboolean success;
};

void api_config_cb(void *user_data, const char *key)
{
    struct config_foreach_data *cfd = user_data;
    if (!cfd->success)
        return;
    if (cfd->prefix && strncmp(cfd->prefix, key, strlen(cfd->prefix)))
        return;
    
    if (!cbox_execute_on(cfd->fb, NULL, cfd->command, "s", cfd->error, key))
    {
        cfd->success = FALSE;
        return;
    }
}

static gboolean config_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    if (!strcmp(cmd->command, "/sections") && (!strcmp(cmd->arg_types, "") || !strcmp(cmd->arg_types, "s")))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct config_foreach_data cfd = {cmd->arg_types[0] == 's' ? CBOX_ARG_S(cmd, 0) : NULL, "/section", fb, error, TRUE};
        cbox_config_foreach_section(api_config_cb, &cfd);
        return cfd.success;
    }
    else if (!strcmp(cmd->command, "/keys") && (!strcmp(cmd->arg_types, "s") || !strcmp(cmd->arg_types, "ss")))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct config_foreach_data cfd = {cmd->arg_types[1] == 's' ? CBOX_ARG_S(cmd, 1) : NULL, "/key", fb, error, TRUE};
        cbox_config_foreach_key(api_config_cb, CBOX_ARG_S(cmd, 0), &cfd);
        return cfd.success;
    }
    else if (!strcmp(cmd->command, "/get") && !strcmp(cmd->arg_types, "ss"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        const char *value = cbox_config_get_string(CBOX_ARG_S(cmd, 0), CBOX_ARG_S(cmd, 1));
        if (!value)
            return TRUE;
        return cbox_execute_on(fb, NULL, "/value", "s", error, value);
    }
    else if (!strcmp(cmd->command, "/set") && !strcmp(cmd->arg_types, "sss"))
    {
        cbox_config_set_string(CBOX_ARG_S(cmd, 0), CBOX_ARG_S(cmd, 1), CBOX_ARG_S(cmd, 2));
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/delete") && !strcmp(cmd->arg_types, "ss"))
    {
        cbox_config_remove_key(CBOX_ARG_S(cmd, 0), CBOX_ARG_S(cmd, 1));
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/delete_section") && !strcmp(cmd->arg_types, "s"))
    {
        cbox_config_remove_section(CBOX_ARG_S(cmd, 0));
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/save") && !strcmp(cmd->arg_types, ""))
    {
        return cbox_config_save(NULL, error);
    }
    else if (!strcmp(cmd->command, "/save") && !strcmp(cmd->arg_types, "s"))
    {
        return cbox_config_save(CBOX_ARG_S(cmd, 0), error);
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

gboolean cbox_app_on_idle(struct cbox_command_target *fb, GError **error)
{
    if (app.rt->io)
    {
        GError *error2 = NULL;
        if (cbox_io_get_disconnect_status(&app.io, &error2))
            cbox_io_poll_ports(&app.io, fb);
        else
        {
            if (error2)
                g_error_free(error2);
            int auto_reconnect = cbox_config_get_int("io", "auto_reconnect", 0);
            if (auto_reconnect > 0)
            {
                sleep(auto_reconnect);
                GError *error2 = NULL;
                if (!cbox_io_cycle(&app.io, fb, &error2))
                {
                    gboolean suppress = FALSE;
                    if (fb)
                        suppress = cbox_execute_on(fb, NULL, "/io/cycle_failed", "s", NULL, error2->message);
                    if (!suppress)
                        g_warning("Cannot cycle the I/O: %s", (error2 && error2->message) ? error2->message : "Unknown error");
                    g_error_free(error2);
                }
                else
                {
                    if (fb)
                        cbox_execute_on(fb, NULL, "/io/cycled", "", NULL);
                }
            }
        }
    }
    if (app.rt)
    {
        // Process results of asynchronous commands
        cbox_rt_handle_cmd_queue(app.rt);

        if (!cbox_midi_appsink_send_to(&app.engine->appsink, fb, error))
            return FALSE;
    }
    return TRUE;
}

struct cbox_app app =
{
    .rt = NULL,
    .current_scene_name = NULL,
    .cmd_target =
    {
        .process_cmd = app_process_cmd,
        .user_data = &app
    },
    .config_cmd_target =
    {
        .process_cmd = config_process_cmd,
        .user_data = &app
    },
};

