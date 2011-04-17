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

#include "config-api.h"
#include "instr.h"
#include "io.h"
#include "layer.h"
#include "menu.h"
#include "midi.h"
#include "module.h"
#include "procmain.h"
#include "scene.h"
#include "ui.h"

#include <assert.h>
#include <glib.h>
#include <getopt.h>
#include <math.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static struct cbox_io io;
static struct cbox_rt *rt;
static gchar *current_scene_name = NULL;

static const char *short_options = "i:c:e:s:h";

static struct option long_options[] = {
    {"help", 0, 0, 'h'},
    {"instrument", 1, 0, 'i'},
    {"scene", 1, 0, 's'},
    {"effect", 1, 0, 'e'},
    {"config", 1, 0, 'c'},
    {0,0,0,0},
};

void print_help(char *progname)
{
    printf("Usage: %s [--help] [--instrument <name>] [--scene <name>] [--config <name>]\n", progname);
    exit(0);
}

int cmd_quit(struct cbox_menu_item *item, void *context)
{
    return 1;
}

int cmd_load_scene(struct cbox_menu_item *item, void *context)
{
    struct cbox_scene *scene = cbox_scene_load(item->value);
    struct cbox_scene *old = cbox_rt_set_scene(rt, scene);
    if (old)
    {
        cbox_scene_destroy(old);
        g_free(current_scene_name);
        current_scene_name = g_strdup_printf("scene:%s", (char *)item->value);
    }
    return 0;
}

int cmd_load_instrument(struct cbox_menu_item *item, void *context)
{
    struct cbox_scene *scene = cbox_scene_new();
    struct cbox_layer *layer = cbox_layer_new((char *)item->value);
    
    if (layer)
    {
        cbox_scene_add_layer(scene, layer);
        
        struct cbox_scene *old = cbox_rt_set_scene(rt, scene);
        if (old)
        {
            cbox_scene_destroy(old);
            g_free(current_scene_name);
            current_scene_name = g_strdup_printf("instrument:%s", (char *)item->value);
        }
    }
    else
        cbox_scene_destroy(scene);
    return 0;
}

gchar *scene_format_value(const struct cbox_menu_item *item, void *context)
{
    return strdup(current_scene_name);
}

gchar *transport_format_value(const struct cbox_menu_item *item, void *context)
{
    struct cbox_bbt bbt;
    cbox_master_to_bbt(rt->master, &bbt);
    if (!strcmp((const char *)item->value, "pos"))
        return g_strdup_printf("%d", (int)rt->master->song_pos_samples);
    else
        return g_strdup_printf("%d:%d:%02d", bbt.bar, bbt.beat, bbt.tick);
}

static void config_key_process(struct cbox_config_section_cb *section, const char *key)
{
    struct cbox_menu *menu = section->user_data;
    static struct cbox_menu_item_extras_command mx_cmd_load_scene = { cmd_load_scene };
    static struct cbox_menu_item_extras_command mx_cmd_load_instrument = { cmd_load_instrument };
    
    if (!strncmp(key, "scene:", 6))
    {    
        cbox_menu_add_item(menu, key, menu_item_command, &mx_cmd_load_scene, strdup(key + 6));
    }
    if (!strncmp(key, "instrument:", 11))
    {    
        cbox_menu_add_item(menu, key, menu_item_command, &mx_cmd_load_instrument, strdup(key + 11));
    }
}

void run_ui()
{
    int var1 = 42;
    double var2 = 1.5;
    static struct cbox_menu_item_extras_int mx_int_var1 = { 0, 127, "%d" };
    static struct cbox_menu_item_extras_double mx_double_var2 = { 0, 127, "%f", NULL, 0 };
    static struct cbox_menu_item_extras_command mx_cmd_quit = { cmd_quit };
    static struct cbox_menu_item_extras_static mx_format_transport = { transport_format_value };
    static struct cbox_menu_item_extras_static mx_format_scene = { scene_format_value };
    struct cbox_menu_state *st = NULL;
    struct cbox_ui_page *page = NULL;
    struct cbox_menu *main_menu = cbox_menu_new();
    struct cbox_config_section_cb cb = { .process = config_key_process, .user_data = main_menu };
    cbox_ui_start();
    
    cbox_menu_add_item(main_menu, "Scenes and layers", menu_item_static, NULL, NULL);
    cbox_menu_add_item(main_menu, "Current:", menu_item_static, &mx_format_scene, NULL);
    cbox_config_foreach_section(&cb);
    cbox_menu_add_item(main_menu, "Variables", menu_item_static, NULL, NULL);
    cbox_menu_add_item(main_menu, "foo:", menu_item_value_int, &mx_int_var1, &var1);
    cbox_menu_add_item(main_menu, "bar:", menu_item_value_double, &mx_double_var2, &var2);
    cbox_menu_add_item(main_menu, "pos:", menu_item_static, &mx_format_transport, "pos");
    cbox_menu_add_item(main_menu, "bbt:", menu_item_static, &mx_format_transport, "bbt");
    cbox_menu_add_item(main_menu, "Commands", menu_item_static, NULL, NULL);
    cbox_menu_add_item(main_menu, "Quit", menu_item_command, &mx_cmd_quit, NULL);

    st = cbox_menu_state_new(main_menu, stdscr, NULL);
    page = cbox_menu_state_get_page(st);

    cbox_ui_run(page);
    cbox_ui_stop();
    cbox_menu_state_destroy(st);
}

int main(int argc, char *argv[])
{
    struct cbox_open_params params;
    const char *module = NULL;
    struct cbox_module_manifest *mptr;
    struct cbox_layer *layer;
    const char *config_name = NULL;
    const char *instrument_name = "default";
    const char *scene_name = NULL;
    const char *effect_module_name = NULL;
    char *instr_section = NULL;
    struct cbox_scene *scene = NULL;
    
    rt = cbox_rt_new();

    while(1)
    {
        int option_index;
        int c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;
        switch (c)
        {
            case 'c':
                config_name = optarg;
                break;
            case 'i':
                instrument_name = optarg;
                break;
            case 's':
                scene_name = optarg;
                break;
            case 'e':
                effect_module_name = optarg;
                break;
            case 'h':
            case '?':
                print_help(argv[0]);
                return 0;
        }
    }

    cbox_config_init(config_name);

    if (!cbox_io_init(&io, &params))
    {
        fprintf(stderr, "Cannot initialise sound I/O\n");
        return 1;
    }
    
    cbox_instruments_init(&io);
    
    if (scene_name)
    {
        current_scene_name = g_strdup_printf("scene:%s", scene_name);
        scene = cbox_scene_load(scene_name);        
        if (!scene)
            goto fail;
    }
    else
    {
        current_scene_name = g_strdup_printf("instrument:%s", instrument_name);
        scene = cbox_scene_new();
        layer = cbox_layer_new(instrument_name);
        if (!layer)
            goto fail;

        cbox_scene_add_layer(scene, layer);
    }

    if (effect_module_name && *effect_module_name)
    {
        mptr = cbox_module_manifest_get_by_name(effect_module_name);
        if (!mptr)
        {
            fprintf(stderr, "Cannot find effect %s\n", effect_module_name);
            goto fail;
        }
        cbox_module_manifest_dump(mptr);
        rt->effect = cbox_module_manifest_create_module(mptr, instr_section, cbox_io_get_sample_rate(&io));
        if (!rt->effect)
        {
            fprintf(stderr, "Cannot create effect %s\n", effect_module_name);
            goto fail;
        }
    }

    cbox_rt_start(rt, &io);
    cbox_master_play(rt->master);
    cbox_rt_set_scene(rt, scene);
    run_ui();
    scene = cbox_rt_set_scene(rt, NULL);
    cbox_rt_stop(rt);
    cbox_io_close(&io);

fail:
    if (rt->effect)
        cbox_module_destroy(rt->effect);
    if (rt->scene)
        cbox_scene_destroy(rt->scene);
    
    cbox_rt_destroy(rt);
    
    cbox_instruments_close();
    cbox_config_close();
    
    g_free(instr_section);
    
    return 0;
}
