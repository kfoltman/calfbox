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

int main_on_key(struct cbox_ui_page *page, int ch)
{
    if (ch == 27)
        return 27;
    return 0;
}

void main_draw(struct cbox_ui_page *page)
{
    box(stdscr, 0, 0);
}

int main_on_idle(struct cbox_ui_page *page)
{
    struct cbox_bbt bbt;
    cbox_master_to_bbt(&io.master, &bbt);
    box(stdscr, 0, 0);
    mvwprintw(stdscr, 3, 3, "%d", (int)io.master.song_pos_samples);
    mvwprintw(stdscr, 5, 3, "%d:%d:%d", bbt.bar, bbt.beat, bbt.tick);
    return 0;
}

static void config_key_process(struct cbox_config_section_cb *section, const char *key)
{
    struct cbox_menu *menu = section->user_data;
    static struct cbox_menu_item_extras_command mx_cmd_quit = { cmd_quit };
    
    cbox_menu_add_item(menu, key, menu_item_command, &mx_cmd_quit, NULL);
}

void run_ui()
{
    int var1 = 42;
    double var2 = 1.5;
    static struct cbox_menu_item_extras_int mx_int_var1 = { 0, 127, "%d" };
    static struct cbox_menu_item_extras_double mx_double_var2 = { 0, 127, "%f", NULL, 0 };
    static struct cbox_menu_item_extras_command mx_cmd_quit = { cmd_quit };
    struct cbox_menu_state *st = NULL;
    struct cbox_ui_page *page = NULL;
    struct cbox_ui_page page2;
    struct cbox_menu *main_menu = cbox_menu_new();
    struct cbox_config_section_cb cb = { .process = config_key_process, .user_data = main_menu };
    cbox_ui_start();
    
    cbox_menu_add_item(main_menu, "foo", menu_item_value_int, &mx_int_var1, &var1);
    cbox_menu_add_item(main_menu, "bar", menu_item_value_double, &mx_double_var2, &var2);
    cbox_menu_add_item(main_menu, "Quit", menu_item_command, &mx_cmd_quit, NULL);
    cbox_config_foreach_section(&cb);

    st = cbox_menu_state_new(main_menu, stdscr, NULL);
    page = cbox_menu_state_get_page(st);

    page2.on_key = main_on_key;
    page2.on_idle = main_on_idle;
    page2.draw = main_draw;
    
    cbox_ui_run(page);
    cbox_ui_stop();
    cbox_menu_state_destroy(st);
}

int main(int argc, char *argv[])
{
    struct cbox_open_params params;
    struct cbox_process_struct process = { .scene = NULL, .effect = NULL };
    struct cbox_io_callbacks cbs = { &process, main_process};
    const char *module = NULL;
    struct cbox_module_manifest *mptr;
    struct cbox_layer *layer;
    const char *config_name = NULL;
    const char *instrument_name = "default";
    const char *scene_name = NULL;
    const char *effect_module_name = NULL;
    char *instr_section;

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
        process.scene = cbox_scene_load(scene_name);        
        if (!process.scene)
            goto fail;
    }
    else
    {
        process.scene = cbox_scene_new();
        layer = cbox_layer_new(instrument_name);
        if (!layer)
            goto fail;

        cbox_scene_add_layer(process.scene, layer);
    }

    if (effect_module_name && *effect_module_name)
    {
        mptr = cbox_module_manifest_get_by_name(effect_module_name);
        if (!mptr)
        {
            fprintf(stderr, "Cannot find effect %s\n", effect_module_name);
            return 1;
        }
        cbox_module_manifest_dump(mptr);
        process.effect = cbox_module_manifest_create_module(mptr, instr_section, cbox_io_get_sample_rate(&io));
        if (!process.effect)
        {
            fprintf(stderr, "Cannot create effect %s\n", effect_module_name);
            return 1;
        }
    }

    cbox_io_start(&io, &cbs);
    cbox_master_play(&io.master);
    run_ui();
    cbox_io_stop(&io);
    cbox_io_close(&io);

fail:
    if (process.effect)
        cbox_module_destroy(process.effect);
    if (process.scene)
        cbox_scene_destroy(process.scene);
    
    cbox_instruments_close();
    cbox_config_close();
    
    g_free(instr_section);
    
    return 0;
}
