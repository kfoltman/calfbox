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

#include "app.h"
#include "blob.h"
#include "config-api.h"
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

int cmd_quit(struct cbox_menu_item_command *item, void *context)
{
    return 1;
}

void switch_scene(struct cbox_menu_item_command *item, struct cbox_scene *new_scene, const char *prefix)
{
    struct cbox_scene *old = cbox_rt_set_scene(app.rt, new_scene);
    if (old)
    {
        CBOX_DELETE(old);
        g_free(app.current_scene_name);
    }
    app.current_scene_name = g_strdup_printf("%s:%s", prefix, (char *)item->item.item_context);
}

int cmd_load_scene(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_scene *scene = app.rt->scene;
    cbox_scene_clear(scene);
    if (!cbox_scene_load(scene, item->item.item_context, &error))
        cbox_print_error(error);
    return 0;
}

int cmd_load_instrument(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_scene *scene = app.rt->scene;
    cbox_scene_clear(scene);
    struct cbox_layer *layer = cbox_layer_new_with_instrument(scene, (char *)item->item.item_context, &error);
    
    if (layer)
    {
        if (!cbox_scene_add_layer(scene, layer, &error))
            cbox_print_error(error);
    }
    else
    {
        cbox_print_error(error);
    }
    return 0;
}

int cmd_load_layer(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_scene *scene = app.rt->scene;
    cbox_scene_clear(scene);
    struct cbox_layer *layer = cbox_layer_new_from_config(scene, (char *)item->item.item_context, &error);
    
    if (layer)
    {
        if (cbox_scene_add_layer(scene, layer, &error))
            switch_scene(item, scene, "layer");
        else
            cbox_print_error(error);
    }
    else
    {
        cbox_print_error(error);
        CBOX_DELETE(scene);
    }
    return 0;
}

gchar *scene_format_value(const struct cbox_menu_item_static *item, void *context)
{
    return strdup(app.current_scene_name);
}

gchar *transport_format_value(const struct cbox_menu_item_static *item, void *context)
{
    // XXXKF
    // struct cbox_bbt bbt;
    // cbox_master_to_bbt(app.rt->master, &bbt);
    if (app.rt->scene->spb == NULL)
        return g_strdup("N/A");
    if (!strcmp((const char *)item->item.item_context, "pos"))
        return g_strdup_printf("%d", (int)app.rt->scene->spb->song_pos_samples);
    else
        return g_strdup_printf("%d", (int)app.rt->scene->spb->song_pos_ppqn);
}

struct cbox_config_section_cb_data
{
    struct cbox_menu *menu;
    cbox_menu_item_execute_func func;
    const char *prefix;
};

static void config_key_process(void *user_data, const char *key)
{
    struct cbox_config_section_cb_data *data = user_data;
    
    if (!strncmp(key, data->prefix, strlen(data->prefix)))
    {
        char *title = cbox_config_get_string(key, "title");
        if (title)
            cbox_menu_add_item(data->menu, cbox_menu_item_new_command(title, data->func, strdup(key + strlen(data->prefix))));
        else
            cbox_menu_add_item(data->menu, cbox_menu_item_new_command(key, data->func, strdup(key + strlen(data->prefix))));
    }
}

struct cbox_menu *create_scene_menu(struct cbox_menu_item_menu *item, void *menu_context)
{
    struct cbox_menu *scene_menu = cbox_menu_new();
    struct cbox_config_section_cb_data cb = { .menu = scene_menu };

    cbox_menu_add_item(scene_menu, cbox_menu_item_new_static("Scenes", NULL, NULL));
    cb.prefix = "scene:";
    cb.func = cmd_load_scene;
    cbox_config_foreach_section(config_key_process, &cb);
    cbox_menu_add_item(scene_menu, cbox_menu_item_new_static("Layers", NULL, NULL));
    cb.prefix = "layer:";
    cb.func = cmd_load_layer;
    cbox_config_foreach_section(config_key_process, &cb);
    cbox_menu_add_item(scene_menu, cbox_menu_item_new_static("Instruments", NULL, NULL));
    cb.prefix = "instrument:";
    cb.func = cmd_load_instrument;
    cbox_config_foreach_section(config_key_process, &cb);
    
    cbox_menu_add_item(scene_menu, cbox_menu_item_new_menu("OK", NULL, NULL));    
    
    return scene_menu;
}

///////////////////////////////////////////////////////////////////////////////

static struct cbox_command_target *find_module_target(const char *type, GError **error)
{
    struct cbox_scene *scene = app.rt->scene;
    for (int i = 0; i < scene->instrument_count; i++)
    {
        if (!strcmp(scene->instruments[i]->module->engine_name, type))
            return &scene->instruments[i]->module->cmd_target;
    }
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot find a module of type '%s'", type);
    return NULL;
    
}

int cmd_stream_rewind(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target("stream_player", &error);
    if (target)
        cbox_execute_on(target, NULL, "/seek", "i", &error, 0);
    cbox_print_error_if(error);
    return 0;
}

int cmd_stream_play(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target("stream_player", &error);
    if (target)
        cbox_execute_on(target, NULL, "/play", "", &error);
    cbox_print_error_if(error);
    return 0;
}

int cmd_stream_stop(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target("stream_player", &error);
    if (target)
        cbox_execute_on(target, NULL, "/stop", "", &error);
    cbox_print_error_if(error);
    return 0;
}

int cmd_stream_unload(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target("stream_player", &error);
    if (target)
        cbox_execute_on(target, NULL, "/unload", "", &error);
    cbox_print_error_if(error);
    return 0;
}

gboolean result_parser_status(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    cbox_osc_command_dump(cmd);
    return TRUE;
}

int cmd_stream_status(struct cbox_menu_item_command *item, void *context)
{
    struct cbox_command_target response = { NULL, result_parser_status };
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target("stream_player", &error);
    if (target)
        cbox_execute_on(target, &response, "/status", "", &error);
    cbox_print_error_if(error);
    return 0;
}

int cmd_stream_load(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target("stream_player", &error);
    if (target)
        cbox_execute_on(target, NULL, "/load", "si", &error, (gchar *)item->item.item_context, 0);
    cbox_print_error_if(error);
    return 0;
}

struct cbox_menu *create_stream_menu(struct cbox_menu_item_menu *item, void *menu_context)
{
    struct cbox_menu *menu = cbox_menu_new();
    struct cbox_config_section_cb_data cb = { .menu = menu };

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Module commands", NULL, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Play stream", cmd_stream_play, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Stop stream", cmd_stream_stop, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Rewind stream", cmd_stream_rewind, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Describe stream", cmd_stream_status, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Unload stream", cmd_stream_unload, NULL));

    glob_t g;
    if (glob("*.wav", GLOB_TILDE_CHECK, NULL, &g) == 0)
    {
        for (int i = 0; i < g.gl_pathc; i++)
        {
            cbox_menu_add_item(menu, cbox_menu_item_new_command(g_strdup_printf("Load: %s", g.gl_pathv[i]), cmd_stream_load, g_strdup(g.gl_pathv[i])));
        }
    }
    
    globfree(&g);
        
    cbox_menu_add_item(menu, cbox_menu_item_new_menu("OK", NULL, NULL));    
    
    return menu;
}

///////////////////////////////////////////////////////////////////////////////

int cmd_pattern_none(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_clear(app.rt->master->song);
    cbox_rt_update_song_playback(app.rt);
    return 0;
}

int cmd_pattern_simple(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_new_metronome(app.rt->master->song, 1));
    return 0;
}

int cmd_pattern_normal(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_new_metronome(app.rt->master->song, app.rt->master->timesig_nom));
    return 0;
}

int cmd_load_drumpattern(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_load(app.rt->master->song, item->item.item_context, 1));
    return 0;
}

int cmd_load_drumtrack(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_load_track(app.rt->master->song, item->item.item_context, 1));
    return 0;
}

int cmd_load_pattern(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_load(app.rt->master->song, item->item.item_context, 0));
    return 0;
}

int cmd_load_track(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_load_track(app.rt->master->song, item->item.item_context, 0));
    return 0;
}

struct cbox_menu *create_pattern_menu(struct cbox_menu_item_menu *item, void *menu_context)
{
    struct cbox_menu *menu = cbox_menu_new();
    struct cbox_config_section_cb_data cb = { .menu = menu };

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Pattern commands", NULL, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("No pattern", cmd_pattern_none, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Simple metronome", cmd_pattern_simple, NULL));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Normal metronome", cmd_pattern_normal, NULL));

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Drum tracks", NULL, NULL));
    cb.prefix = "drumtrack:";
    cb.func = cmd_load_drumtrack;
    cbox_config_foreach_section(config_key_process, &cb);
    
    cbox_menu_add_item(menu, cbox_menu_item_new_static("Melodic tracks", NULL, NULL));
    cb.prefix = "track:";
    cb.func = cmd_load_track;
    cbox_config_foreach_section(config_key_process, &cb);
    
    cbox_menu_add_item(menu, cbox_menu_item_new_static("Drum patterns", NULL, NULL));
    cb.prefix = "drumpattern:";
    cb.func = cmd_load_drumpattern;
    cbox_config_foreach_section(config_key_process, &cb);

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Melodic patterns", NULL, NULL));
    cb.prefix = "pattern:";
    cb.func = cmd_load_pattern;
    cbox_config_foreach_section(config_key_process, &cb);

    cbox_menu_add_item(menu, cbox_menu_item_new_menu("OK", NULL, NULL));    
    
    return menu;
}

struct cbox_menu *create_main_menu()
{
    struct cbox_menu *main_menu = cbox_menu_new();
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("Current scene:", scene_format_value, NULL));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_dynamic_menu("Set scene", create_scene_menu, NULL));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_dynamic_menu("Module control", create_stream_menu, NULL));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_dynamic_menu("Pattern control", create_pattern_menu, NULL));
    
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("Variables", NULL, NULL));
    // cbox_menu_add_item(main_menu, cbox_menu_item_new_int("foo:", &var1, 0, 127, NULL));
    // cbox_menu_add_item(main_menu, "bar:", menu_item_value_double, &mx_double_var2, &var2);
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("pos:", transport_format_value, "pos"));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("bbt:", transport_format_value, "bbt"));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("Commands", NULL, NULL));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_command("Quit", cmd_quit, NULL));
    return main_menu;
}

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
        int len = pos - obj;
        if (!strncmp(obj, "master/", 7))
            return cbox_execute_sub(&app.rt->master->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "config/", 7))
            return cbox_execute_sub(&app.config_cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "scene/", 6))
            return cbox_execute_sub(&app.rt->scene->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "rt/", 3))
            return cbox_execute_sub(&app.rt->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "song/", 5))
            return cbox_execute_sub(&app.rt->master->song->cmd_target, fb, cmd, pos, error);
        else
        if (!strncmp(obj, "waves/", 6))
            return cbox_execute_sub(&app.waves_cmd_target, fb, cmd, pos, error);
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
        cbox_app_on_idle();
        return TRUE;
    }
    else
    if (!strcmp(obj, "send_event") && (!strcmp(cmd->arg_types, "iii") || !strcmp(cmd->arg_types, "ii") || !strcmp(cmd->arg_types, "i")))
    {
        int mcmd = CBOX_ARG_I(cmd, 0);
        int arg1 = 0, arg2 = 0;
        if (cmd->arg_types[1] == 'i')
        {
            arg1 = CBOX_ARG_I(cmd, 1);
            if (cmd->arg_types[2] == 'i')
                arg2 = CBOX_ARG_I(cmd, 2);
        }
        struct cbox_midi_buffer buf;
        cbox_midi_buffer_init(&buf);
        cbox_midi_buffer_write_inline(&buf, 0, mcmd, arg1, arg2);
        cbox_rt_send_events(app.rt, &buf);
        return TRUE;
    }
    else
    if (!strcmp(obj, "play_note") && !strcmp(cmd->arg_types, "iii"))
    {
        int channel = CBOX_ARG_I(cmd, 0);
        int note = CBOX_ARG_I(cmd, 1);
        int velocity = CBOX_ARG_I(cmd, 2);
        struct cbox_midi_buffer buf;
        cbox_midi_buffer_init(&buf);
        cbox_midi_buffer_write_inline(&buf, 0, 0x90 + ((channel - 1) & 15), note & 127, velocity & 127);
        cbox_midi_buffer_write_inline(&buf, 1, 0x80 + ((channel - 1) & 15), note & 127, velocity & 127);
        cbox_rt_send_events(app.rt, &buf);
        return TRUE;
    }
    else
    if (!strcmp(obj, "update_playback") && !strcmp(cmd->arg_types, ""))
    {
        cbox_rt_update_song_playback(app.rt);
        return TRUE;
    }
    else
    if (!strcmp(obj, "get_pattern") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        if (app.rt->master->song && app.rt->master->song->tracks)
        {
            struct cbox_track *track = app.rt->master->song->tracks->data;
            if (track)
            {
                struct cbox_track_item *item = track->items->data;
                struct cbox_midi_pattern *pattern = item->pattern;
                int length = 0;
                struct cbox_blob *blob = cbox_midi_pattern_to_blob(pattern, &length);
                gboolean res = cbox_execute_on(fb, NULL, "/pattern", "bi", error, blob, length);
                cbox_blob_destroy(blob);
            }
        }
        return TRUE;
    }
    else
    if (!strcmp(obj, "new_meter") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct cbox_meter *meter = cbox_meter_new(app.document, cbox_rt_get_sample_rate(app.rt));

        return cbox_execute_on(fb, NULL, "/uuid", "o", error, meter);
    }
    else
    if (!strcmp(obj, "new_recorder") && !strcmp(cmd->arg_types, "s"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct cbox_recorder *rec = cbox_recorder_new_stream(app.rt, CBOX_ARG_S(cmd, 0));

        return cbox_execute_on(fb, NULL, "/uuid", "o", error, rec);
    }
    else
    if (!strcmp(obj, "new_scene") && !strcmp(cmd->arg_types, "ii"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct cbox_rt *rt = cbox_rt_new(app.document);
        cbox_rt_set_offline(rt, CBOX_ARG_I(cmd, 0), CBOX_ARG_I(cmd, 1));
        struct cbox_scene *rec = cbox_scene_new(app.document, rt, TRUE);

        return cbox_execute_on(fb, NULL, "/uuid", "o", error, rec);
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

struct waves_foreach_data
{
    struct cbox_command_target *fb;
    GError **error;
    gboolean success;
};

void wave_list_cb(void *user_data, struct cbox_waveform *waveform)
{
    struct waves_foreach_data *wfd = user_data;
    
    wfd->success = wfd->success && cbox_execute_on(wfd->fb, NULL, "/waveform", "i", wfd->error, (int)waveform->id);
}

static gboolean waves_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        // XXXKF this only supports 4GB - not a big deal for now yet?
        return cbox_execute_on(fb, NULL, "/bytes", "i", error, (int)cbox_wavebank_get_bytes()) &&
            cbox_execute_on(fb, NULL, "/max_bytes", "i", error, (int)cbox_wavebank_get_maxbytes()) &&
            cbox_execute_on(fb, NULL, "/count", "i", error, (int)cbox_wavebank_get_count())
            ;
    }
    else if (!strcmp(cmd->command, "/list") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct waves_foreach_data wfd = { fb, error, TRUE };
        cbox_wavebank_foreach(wave_list_cb, &wfd);
        return wfd.success;
    }
    else if (!strcmp(cmd->command, "/info") && !strcmp(cmd->arg_types, "i"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        int id = CBOX_ARG_I(cmd, 0);
        struct cbox_waveform *waveform = cbox_wavebank_peek_waveform_by_id(id);
        if (waveform == NULL)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Waveform %d not found", id);
            return FALSE;
        }
        assert(id == waveform->id);
        if (!cbox_execute_on(fb, NULL, "/filename", "s", error, waveform->canonical_name)) // XXXKF convert to utf8
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/name", "s", error, waveform->display_name))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/bytes", "i", error, (int)waveform->bytes))
            return FALSE;
        return TRUE;
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void cbox_app_on_idle()
{
    if (app.rt->io)
        cbox_io_poll_ports(&app.io);
    if (app.rt)
        cbox_rt_handle_cmd_queue(app.rt);    
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
    .waves_cmd_target =
    {
        .process_cmd = waves_process_cmd,
        .user_data = &app
    }
};

