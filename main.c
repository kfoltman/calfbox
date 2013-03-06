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
#include "config.h"
#include "config-api.h"
#include "dom.h"
#include "instr.h"
#include "io.h"
#include "layer.h"
#include "menu.h"
#include "menuitem.h"
#include "midi.h"
#include "module.h"
#include "pattern.h"
#include "rt.h"
#include "scene.h"
#include "scripting.h"
#include "song.h"
#include "ui.h"
#include "wavebank.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <getopt.h>
#include <math.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static const char *short_options = "i:c:"
#if USE_PYTHON
"r:"
#endif
"e:s:t:b:d:D:N:o:nmhpP";

static struct option long_options[] = {
    {"help", 0, 0, 'h'},
    {"no-ui", 0, 0, 'n'},
    {"play", 0, 0, 'p'},
    {"no-io", 1, 0, 'N'},
    {"instrument", 1, 0, 'i'},
    {"scene", 1, 0, 's'},
    {"effect", 1, 0, 'e'},
    {"config", 1, 0, 'c'},
    {"metronome", 0, 0, 'm'},
    {"tempo", 1, 0, 't'},
    {"beats", 1, 0, 'b'},
    {"drum-pattern", 1, 0, 'd'},
    {"drum-track", 1, 0, 'D'},
#if USE_PYTHON
    {"run-script", 1, 0, 'r'},
#endif
    {"output", 1, 0, 'o'},
    {0,0,0,0},
};

void print_help(char *progname)
{
    printf("Usage: %s [options]\n"
        "\n"
        "Options:\n"
        " -h | --help               Show this help text\n"
        " -m | --metronome          Create a simple metronome pattern\n"
        " -n | --no-ui              Do not start the user interface\n"
        " -p | --play               Start pattern playback (default for -d/-D)\n"
        " -P | --no-play            Don't start pattern playback\n"
        " -N | --no-io <rate>       Use off-line processing instead of JACK I/O\n"
        " -d | --drum-pattern <p>   Load drum pattern with a given name\n"
        " -D | --drum-track <t>     Load drum track with a given name\n"
        " -t | --tempo <bpm>        Use given tempo (specified in beats/min)\n"
        " -b | --beats <bpb>        Use given beats/bar\n"
        " -e | --effect <e>         Override master effect with preset <e>\n"
        " -i | --instrument <i>     Load instrument <i> as a single-instrument scene\n"
        " -s | --scene <s>          Load a scene <s>\n"
        " -c | --config <c>         Use specified config file instead of default\n"
#if USE_PYTHON
        " -r | --run-script <s>     Run a Python script from a given file\n"
#endif
        " -o | --output <o>         Write the first stereo output to a WAV file\n"
        "\n",
        progname);
    exit(0);
}

static int (*old_menu_on_idle)(struct cbox_ui_page *page);

static int on_idle_with_ui_poll(struct cbox_ui_page *page)
{
    cbox_app_on_idle();
    
    if (old_menu_on_idle)
        return old_menu_on_idle(page);
    else
        return 0;
}

void run_ui()
{
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
    cbox_menu_destroy(main_menu);
}

int main(int argc, char *argv[])
{
    struct cbox_open_params params;
    struct cbox_layer *layer;
    const char *config_name = NULL;
    const char *instrument_name = NULL;
    const char *scene_name = NULL;
    const char *effect_preset_name = NULL;
    const char *drum_pattern_name = NULL;
    const char *drum_track_name = NULL;
#if USE_PYTHON
    const char *script_name = NULL;
#endif
    const char *output_name = NULL;
    char *instr_section = NULL;
    struct cbox_scene *scene = NULL;
    int metronome = 0;
    int bpb = 0;
    float tempo = 0;
    GError *error = NULL;
    gboolean no_ui = FALSE;
    gboolean no_io = FALSE;
    int play_immediately = 0;
    int no_io_srate = 0;
    
    cbox_dom_init();
    
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
            case 'o':
                output_name = optarg;
                break;
            case 's':
                scene_name = optarg;
                break;
            case 'e':
                effect_preset_name = optarg;
                break;
            case 'd':
                drum_pattern_name = optarg;
                break;
            case 'D':
                drum_track_name = optarg;
                break;
#if USE_PYTHON
            case 'r':
                script_name = optarg;
                break;
#endif
            case 'm':
                metronome = 1;
                break;
            case 'n':
                no_ui = TRUE;
                break;
            case 'N':
                no_io = TRUE;
                no_io_srate = atoi(optarg);
                break;
            case 'p':
                play_immediately = 1;
                break;
            case 'P':
                play_immediately = -1;
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

    app.document = cbox_document_new();
    app.rt = cbox_rt_new(app.document);
    
    cbox_config_init(config_name);
    if (tempo < 1)
        tempo = cbox_config_get_float("master", "tempo", 120);
    if (bpb < 1)
        bpb = cbox_config_get_int("master", "beats_per_bar", 4);

    if (no_io)
    {
        cbox_rt_set_offline(app.rt, no_io_srate, 1024);
    }
    else
    {
        GError *error = NULL;
        if (!cbox_io_init(&app.io, &params, &error))
        {
            fprintf(stderr, "Cannot initialise sound I/O: %s\n", (error && error->message) ? error->message : "Unknown error");
            return 1;
        }
        cbox_rt_set_io(app.rt, &app.io);
    }
    cbox_wavebank_init();
    
    if (!scene_name && !instrument_name)
    {
        scene_name = cbox_config_get_string("init", "scene");
        instrument_name = cbox_config_get_string("init", "instrument");
        if (!scene_name && !instrument_name)
        {
            if (cbox_config_has_section("scene:default"))
                scene_name = "default";
            else
            if (cbox_config_has_section("instrument:default"))
                instrument_name = "default";
        }
    }
    
    scene = cbox_scene_new(app.document, app.rt, FALSE);
    if (!scene)
        goto fail;
    if (scene_name)
    {
        app.current_scene_name = g_strdup_printf("scene:%s", scene_name);
        if (!cbox_scene_load(scene, scene_name, &error))
            goto fail;
    }
    else
    if (instrument_name)
    {
        app.current_scene_name = g_strdup_printf("instrument:%s", instrument_name);
        layer = cbox_layer_new_with_instrument(scene, instrument_name, &error);
        if (!layer)
            goto fail;

        if (!cbox_scene_add_layer(scene, layer, &error))
            goto fail;
    }

    if (!effect_preset_name)
        effect_preset_name = cbox_config_get_string("master", "effect");
    
    if (effect_preset_name && *effect_preset_name)
    {
        app.rt->effect = cbox_module_new_from_fx_preset(effect_preset_name, app.document, app.rt, &error);
        if (!app.rt->effect)
            goto fail;
    }
    cbox_master_set_tempo(app.rt->master, tempo);
    cbox_master_set_timesig(app.rt->master, bpb, 4);

    if (output_name && scene)
    {
        GError *error = NULL;
        if (!cbox_recording_source_attach(&scene->rec_stereo_outputs[0], cbox_recorder_new_stream(app.rt, output_name), &error))
            cbox_print_error(error);
    }
    cbox_rt_start(app.rt);
    if (drum_pattern_name)
        cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_load(app.rt->master->song, drum_pattern_name, 1));
    else if (drum_track_name)
        cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_load_track(app.rt->master->song, drum_track_name, 1));
    else if (metronome)
        cbox_song_use_looped_pattern(app.rt->master->song, cbox_midi_pattern_new_metronome(app.rt->master->song, app.rt->master->timesig_nom));
    
    gboolean has_song = drum_pattern_name || drum_track_name || metronome;
    if (play_immediately == 1 || (play_immediately != -1 && has_song))
        cbox_master_play(app.rt->master);
    cbox_rt_set_scene(app.rt, scene);
#if USE_PYTHON
    if (script_name)
        cbox_script_run(script_name);
    else
#endif
    if (!no_ui)
        run_ui();
    else
    {
        printf("Ready. Press ENTER to exit.\n");
        fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);
        do {
            int ch = getchar();
            if (ch == 10 || (ch == -1 && errno != EWOULDBLOCK))
                break;
            usleep(100000);
            cbox_app_on_idle();
        } while(1);
        fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) &~ O_NONBLOCK);
    }
    scene = cbox_rt_set_scene(app.rt, NULL);
    cbox_rt_stop(app.rt);
    if (!no_io)
        cbox_io_close(&app.io);
    goto ok;

fail:
    fprintf(stderr, "Cannot start: %s\n", error ? error->message : "unknown error");
ok:
    if (error)
        g_error_free(error);
    if (app.rt->effect)
    {
        CBOX_DELETE(app.rt->effect);
        app.rt->effect = NULL;
    }
    if (scene)
        CBOX_DELETE(scene);
    
    cbox_rt_destroy(app.rt);
    
    if (cbox_wavebank_get_maxbytes() > 0)
        g_message("Max waveform usage: %f MB", (float)(cbox_wavebank_get_maxbytes() / 1048576.0));
    cbox_wavebank_close();
    cbox_config_close();
    
    g_free(instr_section);

    cbox_dom_close();
    
    return 0;
}
