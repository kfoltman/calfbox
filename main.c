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
#include "config-api.h"
#include "instr.h"
#include "io.h"
#include "layer.h"
#include "menu.h"
#include "menuitem.h"
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

static const char *short_options = "i:c:e:s:t:b:d:D:mh";

static struct option long_options[] = {
    {"help", 0, 0, 'h'},
    {"instrument", 1, 0, 'i'},
    {"scene", 1, 0, 's'},
    {"effect", 1, 0, 'e'},
    {"config", 1, 0, 'c'},
    {"metronome", 0, 0, 'm'},
    {"tempo", 1, 0, 't'},
    {"beats", 1, 0, 'b'},
    {"drum-pattern", 1, 0, 'd'},
    {"drum-track", 1, 0, 'D'},
    {0,0,0,0},
};

void print_help(char *progname)
{
    printf("Usage: %s [--help] [--metronome] [--drum-pattern <pattern>] [--drum-track <track>] [--tempo <bpm>] [--beats <beatsperbar>] [--instrument <name>] [--scene <name>] [--config <name>]\n", progname);
    exit(0);
}

static int (*old_menu_on_idle)(struct cbox_ui_page *page);

static int on_idle_with_ui_poll(struct cbox_ui_page *page)
{
    cbox_io_poll_ports(&app.io);
    if (app.rt)
        cbox_rt_cmd_handle_queue(app.rt);
    
    if (old_menu_on_idle)
        return old_menu_on_idle(page);
    else
        return 0;
}

void run_ui()
{
    int var1 = 42;
    struct cbox_menu_state *st = NULL;
    struct cbox_menu_page *page = cbox_menu_page_new();
    cbox_ui_start();
    old_menu_on_idle = page->page.on_idle;
    page->page.on_idle = on_idle_with_ui_poll;
    
    struct cbox_menu *main_menu = create_main_menu();

    st = cbox_menu_state_new(page, main_menu, stdscr, NULL);
    page->state = st;

    cbox_ui_run(&page->page);
    cbox_ui_stop();
    cbox_menu_state_destroy(st);
    cbox_menu_page_destroy(page);
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
    const char *drum_pattern_name = NULL;
    const char *drum_track_name = NULL;
    char *instr_section = NULL;
    struct cbox_scene *scene = NULL;
    int metronome = 0;
    int bpb = 0;
    float tempo = 0;
    
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
            case 'd':
                drum_pattern_name = optarg;
                break;
            case 'D':
                drum_track_name = optarg;
                break;
            case 'm':
                metronome = 1;
                break;
            case 'b':
                bpb = atoi(optarg);
                break;
            case 't':
                tempo = atof(optarg);
                break;
            case 'h':
            case '?':
                print_help(argv[0]);
                return 0;
        }
    }

    app.rt = cbox_rt_new();
    cbox_config_init(config_name);
    if (tempo < 1)
        tempo = cbox_config_get_float("master", "tempo", 120);
    if (bpb < 1)
        bpb = cbox_config_get_int("master", "beats_per_bar", 4);

    if (!cbox_io_init(&app.io, &params))
    {
        fprintf(stderr, "Cannot initialise sound I/O\n");
        return 1;
    }
    
    cbox_instruments_init(&app.io);
    
    if (scene_name)
    {
        app.current_scene_name = g_strdup_printf("scene:%s", scene_name);
        scene = cbox_scene_load(scene_name);        
        if (!scene)
            goto fail;
    }
    else
    {
        app.current_scene_name = g_strdup_printf("instrument:%s", instrument_name);
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
        app.rt->effect = cbox_module_manifest_create_module(mptr, instr_section, cbox_io_get_sample_rate(&app.io));
        if (!app.rt->effect)
        {
            fprintf(stderr, "Cannot create effect %s\n", effect_module_name);
            goto fail;
        }
    }
    cbox_master_set_tempo(app.rt->master, tempo);
    cbox_master_set_timesig(app.rt->master, bpb, 4);
    if (drum_pattern_name)
        cbox_rt_set_pattern(app.rt, cbox_midi_pattern_load_drum(drum_pattern_name), 0);
    else if (drum_track_name)
        cbox_rt_set_pattern(app.rt, cbox_midi_pattern_load_drum_track(drum_track_name), 0);
    else if (metronome)
        cbox_rt_set_pattern(app.rt, cbox_midi_pattern_new_metronome(app.rt->master->timesig_nom), 0);

    cbox_rt_start(app.rt, &app.io);
    cbox_master_play(app.rt->master);
    cbox_rt_set_scene(app.rt, scene);
    run_ui();
    scene = cbox_rt_set_scene(app.rt, NULL);
    cbox_rt_stop(app.rt);
    cbox_io_close(&app.io);

fail:
    if (app.rt->effect)
        cbox_module_destroy(app.rt->effect);
    if (app.rt->scene)
        cbox_scene_destroy(app.rt->scene);
    
    cbox_rt_destroy(app.rt);
    
    cbox_instruments_close();
    cbox_config_close();
    
    g_free(instr_section);
    
    return 0;
}
