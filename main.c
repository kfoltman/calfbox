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
#include "io.h"
#include "menu.h"
#include "midi.h"
#include "module.h"
#include "procmain.h"
#include "ui.h"

#include <glib.h>
#include <getopt.h>
#include <math.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char *short_options = "i:c:e:h";

static struct option long_options[] = {
    {"help", 0, 0, 'h'},
    {"instrument", 1, 0, 'i'},
    {"effect", 1, 0, 'e'},
    {"config", 1, 0, 'c'},
    {0,0,0,0},
};

void print_help(char *progname)
{
    printf("Usage: %s [--help] [--instrument <name>] [--config <name>]\n", progname);
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
    box(stdscr, 0, 0);
    mvwprintw(stdscr, 3, 3, "%d", time(NULL));
    return 0;
}

void run_ui()
{
    int var1 = 42;
    double var2 = 1.5;
    static struct cbox_menu_item_extras_int mx_int_var1 = { 0, 127, "%d" };
    static struct cbox_menu_item_extras_double mx_double_var2 = { 0, 127, "%f", NULL, 0 };
    static struct cbox_menu_item_extras_command mx_cmd_quit = { cmd_quit };
    struct cbox_menu_item menu_items_main[] = {
        { "foo", menu_item_value_int, &mx_int_var1, &var1 },
        { "bar", menu_item_value_double, &mx_double_var2, &var2 },
        { "Quit", menu_item_command, &mx_cmd_quit, NULL },
    };
    struct cbox_menu_state *st = NULL;
    struct cbox_ui_page *page = NULL;
    struct cbox_ui_page page2;
    FIXED_MENU(main);
    page = cbox_menu_init(&st, &menu_main, NULL);

    page2.on_key = main_on_key;
    page2.on_idle = main_on_idle;
    page2.draw = main_draw;
    
    cbox_ui_start();
    cbox_ui_run(&page2);
    cbox_ui_stop();
}

int main(int argc, char *argv[])
{
    struct cbox_io io;
    struct cbox_open_params params;
    struct cbox_process_struct process = { .module = NULL, .effect = NULL };
    struct cbox_io_callbacks cbs = { &process, main_process};
    const char *module = NULL;
    struct cbox_module_manifest *mptr;
    const char *config_name = NULL;
    const char *instrument_name = "default";
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

    instr_section = g_strdup_printf("instrument:%s", instrument_name);
    module = cbox_config_get_string_with_default(instr_section, "engine", "tonewheel_organ");
    
    mptr = cbox_module_get_by_name(module);
    if (!mptr)
    {
        fprintf(stderr, "Cannot find module %s\n", module);
        return 1;
    }
    cbox_module_manifest_dump(mptr);
    process.module = (*mptr->create)(mptr->user_data, instr_section);
    if (!process.module)
    {
        fprintf(stderr, "Cannot find module %s\n", module);
        return 1;
    }
    if (effect_module_name && *effect_module_name)
    {
        mptr = cbox_module_get_by_name(effect_module_name);
        if (!mptr)
        {
            fprintf(stderr, "Cannot create module %s\n", effect_module_name);
            return 1;
        }
        cbox_module_manifest_dump(mptr);
        process.effect = (*mptr->create)(mptr->user_data, instr_section);
        if (!process.effect)
        {
            fprintf(stderr, "Cannot create effect %s\n", effect_module_name);
            return 1;
        }
    }

    if (!cbox_io_init(&io, &params))
    {
        fprintf(stderr, "Cannot initialise sound I/O\n");
        return 1;
    }
    cbox_io_start(&io, &cbs);
    run_ui();
    cbox_io_stop(&io);
    cbox_io_close(&io);
    cbox_config_close();
    g_free(instr_section);
    
    return 0;
}
