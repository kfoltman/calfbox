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

int cmd_quit(struct cbox_menu_item_command *item, void *context)
{
    return 1;
}

int cmd_load_scene(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_scene *scene = app.engine->scenes[0];
    cbox_scene_clear(scene);
    if (!cbox_scene_load(scene, item->item.item_context, &error))
        cbox_print_error(error);
    return 0;
}

int cmd_load_instrument(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_scene *scene = app.engine->scenes[0];
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
    struct cbox_scene *scene = app.engine->scenes[0];
    cbox_scene_clear(scene);
    struct cbox_layer *layer = cbox_layer_new_from_config(scene, (char *)item->item.item_context, &error);
    
    if (layer)
    {
        if (!cbox_scene_add_layer(scene, layer, &error))
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
    if (app.current_scene_name)
        return strdup(app.current_scene_name);
    else
        return strdup("- No scene -");
}

gchar *transport_format_value(const struct cbox_menu_item_static *item, void *context)
{
    // XXXKF
    // struct cbox_bbt bbt;
    // cbox_master_to_bbt(app.engine->master, &bbt);
    if (app.engine->master->spb == NULL)
        return g_strdup("N/A");
    if (!strcmp((const char *)item->item.item_context, "pos"))
        return g_strdup_printf("%d", (int)app.engine->master->spb->song_pos_samples);
    else
        return g_strdup_printf("%d", (int)app.engine->master->spb->song_pos_ppqn);
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
    struct cbox_scene *scene = app.engine->scenes[0];
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

static void restart_song()
{
    cbox_master_stop(app.engine->master);
    cbox_master_seek_ppqn(app.engine->master, 0);
    cbox_master_play(app.engine->master);
}

int cmd_pattern_none(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_clear(app.engine->master->song);
    cbox_engine_update_song_playback(app.engine);
    return 0;
}

int cmd_pattern_simple(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_new_metronome(app.engine->master->song, 1));
    restart_song();
    return 0;
}

int cmd_pattern_normal(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_new_metronome(app.engine->master->song, app.engine->master->timesig_nom));
    restart_song();
    return 0;
}

int cmd_load_drumpattern(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load(app.engine->master->song, item->item.item_context, 1));
    restart_song();
    return 0;
}

int cmd_load_drumtrack(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load_track(app.engine->master->song, item->item.item_context, 1));
    restart_song();
    return 0;
}

int cmd_load_pattern(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load(app.engine->master->song, item->item.item_context, 0));
    restart_song();
    return 0;
}

int cmd_load_track(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load_track(app.engine->master->song, item->item.item_context, 0));
    restart_song();
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

