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

#if USE_NCURSES

int cmd_quit(struct cbox_menu_item_command *item, void *context)
{
    return 1;
}

static void set_current_scene_name(gchar *name)
{
    if (app.current_scene_name)
        g_free(app.current_scene_name);
    app.current_scene_name = name;
}

int cmd_load_scene(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_scene *scene = app.engine->scenes[0];
    cbox_scene_clear(scene);
    if (!cbox_scene_load(scene, item->item.item_context, &error))
        cbox_print_error(error);
    set_current_scene_name(g_strdup_printf("scene:%s", (const char *)item->item.item_context));
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
        set_current_scene_name(g_strdup_printf("instrument:%s", (const char *)item->item.item_context));
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
        set_current_scene_name(g_strdup_printf("layer:%s", (const char *)item->item.item_context));
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
        return g_strdup(app.current_scene_name);
    else
        return g_strdup("- No scene -");
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
            cbox_menu_add_item(data->menu, cbox_menu_item_new_command(title, data->func, strdup(key + strlen(data->prefix)), mif_dup_label | mif_free_context));
        else
            cbox_menu_add_item(data->menu, cbox_menu_item_new_command(key, data->func, strdup(key + strlen(data->prefix)), mif_dup_label | mif_free_context));
    }
}

struct cbox_menu *create_scene_menu(struct cbox_menu_item_menu *item, void *menu_context)
{
    struct cbox_menu *scene_menu = cbox_menu_new();
    struct cbox_config_section_cb_data cb = { .menu = scene_menu };

    cbox_menu_add_item(scene_menu, cbox_menu_item_new_static("Scenes", NULL, NULL, 0));
    cb.prefix = "scene:";
    cb.func = cmd_load_scene;
    cbox_config_foreach_section(config_key_process, &cb);
    cbox_menu_add_item(scene_menu, cbox_menu_item_new_static("Layers", NULL, NULL, 0));
    cb.prefix = "layer:";
    cb.func = cmd_load_layer;
    cbox_config_foreach_section(config_key_process, &cb);
    cbox_menu_add_item(scene_menu, cbox_menu_item_new_static("Instruments", NULL, NULL, 0));
    cb.prefix = "instrument:";
    cb.func = cmd_load_instrument;
    cbox_config_foreach_section(config_key_process, &cb);
    
    cbox_menu_add_item(scene_menu, cbox_menu_item_new_ok());
    return scene_menu;
}

///////////////////////////////////////////////////////////////////////////////

static struct cbox_command_target *find_module_target(const struct cbox_menu_item_command *item)
{
    struct cbox_instrument *instr = item->item.item_context;
    return &instr->module->cmd_target;
    
}

int cmd_stream_rewind(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target(item);
    if (target)
        cbox_execute_on(target, NULL, "/seek", "i", &error, 0);
    cbox_print_error_if(error);
    return 0;
}

int cmd_stream_play(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target(item);
    if (target)
        cbox_execute_on(target, NULL, "/play", "", &error);
    cbox_print_error_if(error);
    return 0;
}

int cmd_stream_stop(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target(item);
    if (target)
        cbox_execute_on(target, NULL, "/stop", "", &error);
    cbox_print_error_if(error);
    return 0;
}

int cmd_stream_unload(struct cbox_menu_item_command *item, void *context)
{
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target(item);
    if (target)
        cbox_execute_on(target, NULL, "/unload", "", &error);
    cbox_print_error_if(error);
    return 0;
}

struct stream_response_data
{
    gchar *filename;
    uint32_t pos, length, sample_rate, channels;
};

gboolean result_parser_status(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct stream_response_data *res = ct->user_data;
    if (!strcmp(cmd->command, "/filename"))
        res->filename = g_strdup(cmd->arg_values[0]);
    if (!strcmp(cmd->command, "/pos"))
        res->pos = *((uint32_t **)cmd->arg_values)[0];
    if (!strcmp(cmd->command, "/length"))
        res->length = *((uint32_t **)cmd->arg_values)[0];
    if (!strcmp(cmd->command, "/sample_rate"))
        res->sample_rate = *((uint32_t **)cmd->arg_values)[0];
    if (!strcmp(cmd->command, "/channels"))
        res->channels = *((uint32_t **)cmd->arg_values)[0];
    //cbox_osc_command_dump(cmd);
    return TRUE;
}

char *cmd_stream_status(const struct cbox_menu_item_static *item, void *context)
{
    struct stream_response_data data = { NULL, 0, 0, 0, 0 };
    struct cbox_command_target response = { &data, result_parser_status };
    GError *error = NULL;
    struct cbox_command_target *target = find_module_target((struct cbox_menu_item_command *)item);
    if (target)
        cbox_execute_on(target, &response, "/status", "", &error);
    cbox_print_error_if(error);
    gchar *res = NULL;
    if (data.filename && data.length && data.sample_rate)
    {
        double duration = data.length * 1.0 / data.sample_rate;
        res = g_strdup_printf("%s (%um%0.2fs, %uch, %uHz) (%0.2f%%)", data.filename, (unsigned)floor(duration / 60), duration - 60 * floor(duration / 60), (unsigned)data.channels, (unsigned)data.sample_rate, data.pos * 100.0 / data.length);
    }
    else
        res = g_strdup("-");
    g_free(data.filename);
    return res;
}

struct load_waveform_context
{
    struct cbox_menu_item_context header;
    struct cbox_instrument *instrument;
    char *filename;
};

static void destroy_load_waveform_context(void *p)
{
    struct load_waveform_context *context = p;
    g_free(context->filename);
    free(context);
}

int cmd_stream_load(struct cbox_menu_item_command *item, void *context)
{
    struct load_waveform_context *ctx = item->item.item_context;
    GError *error = NULL;
    struct cbox_command_target *target = &ctx->instrument->module->cmd_target;
    if (target)
        cbox_execute_on(target, NULL, "/load", "si", &error, ctx->filename, 0);
    cbox_print_error_if(error);
    return 0;
}

struct cbox_menu *create_streamplay_menu(struct cbox_menu_item_menu *item, void *menu_context)
{
    struct cbox_menu *menu = cbox_menu_new();
    struct cbox_instrument *instr = item->item.item_context;

    assert(instr);
    cbox_menu_add_item(menu, cbox_menu_item_new_static("Current stream", NULL, NULL, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_static("File", cmd_stream_status, instr, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_static("Module commands", NULL, NULL, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Play stream", cmd_stream_play, instr, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Stop stream", cmd_stream_stop, instr, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Rewind stream", cmd_stream_rewind, instr, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Unload stream", cmd_stream_unload, instr, 0));

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Files available", NULL, NULL, 0));
    glob_t g;
    gboolean found = (glob("*.wav", GLOB_TILDE_CHECK, NULL, &g) == 0);
    found = glob("*.ogg", GLOB_TILDE_CHECK | (found ? GLOB_APPEND : 0), NULL, &g) || found;
    if (found)
    {
        for (size_t i = 0; i < g.gl_pathc; i++)
        {
            struct load_waveform_context *context = calloc(1, sizeof(struct load_waveform_context));
            context->header.destroy_func = destroy_load_waveform_context;
            context->instrument = instr;
            context->filename = g_strdup(g.gl_pathv[i]);
            cbox_menu_add_item(menu, cbox_menu_item_new_command(g_strdup_printf("Load: %s", g.gl_pathv[i]), cmd_stream_load, context, mif_free_label | mif_context_is_struct));
        }
    }
    globfree(&g);

    cbox_menu_add_item(menu, cbox_menu_item_new_ok());
    return menu;
}

///////////////////////////////////////////////////////////////////////////////

struct cbox_menu *create_module_menu(struct cbox_menu_item_menu *item, void *menu_context)
{
    struct cbox_menu *menu = cbox_menu_new();

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Scene instruments", NULL, NULL, 0));
    struct cbox_scene *scene = app.engine->scenes[0];
    for (uint32_t i = 0; i < scene->instrument_count; ++i)
    {
        struct cbox_instrument *instr = scene->instruments[i];
        create_menu_func menufunc = NULL;

        if (!strcmp(instr->module->engine_name, "stream_player"))
            menufunc = create_streamplay_menu;
        if (menufunc)
            cbox_menu_add_item(menu, cbox_menu_item_new_dynamic_menu(g_strdup_printf("%s (%s)", instr->module->instance_name, instr->module->engine_name), menufunc, instr, mif_free_label));
        else
            cbox_menu_add_item(menu, cbox_menu_item_new_static(g_strdup_printf("%s (%s)", instr->module->instance_name, instr->module->engine_name), NULL, NULL, mif_free_label));
    }
    cbox_menu_add_item(menu, cbox_menu_item_new_ok());
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
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_new_metronome(app.engine->master->song, 1, app.engine->master->ppqn_factor));
    restart_song();
    return 0;
}

int cmd_pattern_normal(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_new_metronome(app.engine->master->song, app.engine->master->timesig_num, app.engine->master->ppqn_factor));
    restart_song();
    return 0;
}

int cmd_load_drumpattern(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load(app.engine->master->song, item->item.item_context, 1, app.engine->master->ppqn_factor));
    restart_song();
    return 0;
}

int cmd_load_drumtrack(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load_track(app.engine->master->song, item->item.item_context, 1, app.engine->master->ppqn_factor));
    restart_song();
    return 0;
}

int cmd_load_pattern(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load(app.engine->master->song, item->item.item_context, 0, app.engine->master->ppqn_factor));
    restart_song();
    return 0;
}

int cmd_load_track(struct cbox_menu_item_command *item, void *context)
{
    cbox_song_use_looped_pattern(app.engine->master->song, cbox_midi_pattern_load_track(app.engine->master->song, item->item.item_context, 0, app.engine->master->ppqn_factor));
    restart_song();
    return 0;
}

struct cbox_menu *create_pattern_menu(struct cbox_menu_item_menu *item, void *menu_context)
{
    struct cbox_menu *menu = cbox_menu_new();
    struct cbox_config_section_cb_data cb = { .menu = menu };

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Pattern commands", NULL, NULL, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("No pattern", cmd_pattern_none, NULL, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Simple metronome", cmd_pattern_simple, NULL, 0));
    cbox_menu_add_item(menu, cbox_menu_item_new_command("Normal metronome", cmd_pattern_normal, NULL, 0));

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Drum tracks", NULL, NULL, 0));
    cb.prefix = "drumtrack:";
    cb.func = cmd_load_drumtrack;
    cbox_config_foreach_section(config_key_process, &cb);
    
    cbox_menu_add_item(menu, cbox_menu_item_new_static("Melodic tracks", NULL, NULL, 0));
    cb.prefix = "track:";
    cb.func = cmd_load_track;
    cbox_config_foreach_section(config_key_process, &cb);
    
    cbox_menu_add_item(menu, cbox_menu_item_new_static("Drum patterns", NULL, NULL, 0));
    cb.prefix = "drumpattern:";
    cb.func = cmd_load_drumpattern;
    cbox_config_foreach_section(config_key_process, &cb);

    cbox_menu_add_item(menu, cbox_menu_item_new_static("Melodic patterns", NULL, NULL, 0));
    cb.prefix = "pattern:";
    cb.func = cmd_load_pattern;
    cbox_config_foreach_section(config_key_process, &cb);

    cbox_menu_add_item(menu, cbox_menu_item_new_ok());
    return menu;
}

struct cbox_menu *create_main_menu()
{
    struct cbox_menu *main_menu = cbox_menu_new();
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("Current scene:", scene_format_value, NULL, 0));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_dynamic_menu("Set scene", create_scene_menu, NULL, 0));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_dynamic_menu("Module control", create_module_menu, NULL, 0));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_dynamic_menu("Pattern control", create_pattern_menu, NULL, 0));
    
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("Variables", NULL, NULL, 0));
    // cbox_menu_add_item(main_menu, cbox_menu_item_new_int("foo:", &var1, 0, 127, NULL));
    // cbox_menu_add_item(main_menu, "bar:", menu_item_value_double, &mx_double_var2, &var2);
    //cbox_menu_add_item(main_menu, cbox_menu_item_new_static("pos:", transport_format_value, "pos", 0));
    //cbox_menu_add_item(main_menu, cbox_menu_item_new_static("bbt:", transport_format_value, "bbt", 0));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_static("Commands", NULL, NULL, 0));
    cbox_menu_add_item(main_menu, cbox_menu_item_new_command("Quit", cmd_quit, NULL, 0));
    return main_menu;
}

#endif
