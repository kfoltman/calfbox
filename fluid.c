/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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
#include "dspmath.h"
#include "module.h"
#include <glib.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <fluidsynth.h>

static void fluidsynth_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs);

static void fluidsynth_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len);

struct fluidsynth_module
{
    struct cbox_module module;

    fluid_settings_t *settings;
    fluid_synth_t *synth;
    int sfid;
};

struct cbox_module *fluidsynth_create(void *user_data, const char *cfg_section, int srate)
{
    int result = 0;
    int i;
    const char *bankname = cbox_config_get_string(cfg_section, "sf2");
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    if (!bankname)
    {
        g_error("No bank specified (section=%s, key=sf2)", cfg_section);
        return NULL;        
    }
    
    struct fluidsynth_module *m = malloc(sizeof(struct fluidsynth_module));
    m->module.user_data = m;
    m->module.process_event = fluidsynth_process_event;
    m->module.process_block = fluidsynth_process_block;
    m->settings = new_fluid_settings();
    fluid_settings_setnum(m->settings, "synth.sample-rate", srate);
    m->synth = new_fluid_synth(m->settings);
    g_message("Loading soundfont %s", bankname);
    result = fluid_synth_sfload(m->synth, bankname, 1);
    if (result == FLUID_FAILED)
    {
        g_error("Failed to load the default bank %s", bankname);
        return NULL;
    }
    m->sfid = result;
    fluid_synth_set_reverb_on(m->synth, cbox_config_get_int(cfg_section, "reverb", 1));
    fluid_synth_set_chorus_on(m->synth, cbox_config_get_int(cfg_section, "chorus", 1));
    for (i = 0; i < 16; i++)
    {
        gchar *key = g_strdup_printf("channel%d", i + 1);
        gchar *preset = cbox_config_get_string(cfg_section, key);
        fluid_synth_sfont_select(m->synth, i, m->sfid);
        if (preset)
        {
            int found = 0;
            
            fluid_sfont_t* sfont = fluid_synth_get_sfont(m->synth, 0);
            fluid_preset_t tmp;

            sfont->iteration_start(sfont);            
            while(sfont->iteration_next(sfont, &tmp))
            {
                // trailing spaces are common in some SF2s
                const char *pname = tmp.get_name(&tmp);
                int len = strlen(pname);
                while (len > 0 && pname[len - 1] == ' ')
                    len--;
                    
                if (!strncmp(pname, preset, len) && preset[len] == '\0')
                {
                    fluid_synth_bank_select(m->synth, i, tmp.get_banknum(&tmp));
                    fluid_synth_program_change(m->synth, i, tmp.get_num(&tmp));
                    found = 1;
                    break;
                }
            }
            
            if (!found)
                g_error("Preset not found: %s", preset);
        }
        g_free(key);
    }
    
    return &m->module;
}

void fluidsynth_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct fluidsynth_module *m = (struct fluidsynth_module *)module;
    fluid_synth_write_float(m->synth, CBOX_BLOCK_SIZE, outputs[0], 0, 1, outputs[1], 0, 1);
}

void fluidsynth_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct fluidsynth_module *m = (struct fluidsynth_module *)module;
    if (len > 0)
    {
        int cmd = data[0] >> 4;
        int chn = data[0] & 15;
        switch(cmd)
        {
            case 8:
                fluid_synth_noteoff(m->synth, chn, data[1]);
                break;

            case 9:
                fluid_synth_noteon(m->synth, chn, data[1], data[2]);
                break;
            
            case 10:
                // polyphonic pressure not handled
                break;
            
            case 11:
                fluid_synth_cc(m->synth, chn, data[1], data[2]);
                break;

            case 12:
                fluid_synth_program_change(m->synth, chn, data[1]);
                break;

            case 13:
                fluid_synth_channel_pressure(m->synth, chn, data[1]);
                break;

            case 14:
                fluid_synth_pitch_bend(m->synth, chn, data[1] + 128 * data[2]);
                break;

            }
    }
}

struct cbox_module_livecontroller_metadata fluidsynth_controllers[] = {
    { -1, cmlc_continuouscc, 1, "Modulation", NULL},
    { -1, cmlc_continuouscc, 7, "Volume", NULL},
    { -1, cmlc_continuouscc, 10, "Pan", NULL},
    { -1, cmlc_continuouscc, 91, "Reverb", NULL},
    { -1, cmlc_continuouscc, 93, "Chorus", NULL},
    { -1, cmlc_onoffcc, 64, "Hold", NULL},
    { -1, cmlc_onoffcc, 66, "Sostenuto", NULL},
};

struct cbox_module_keyrange_metadata fluidsynth_keyranges[] = {
    { 1, 0, 127, "Channel 1" },
    { 2, 0, 127, "Channel 2" },
    { 3, 0, 127, "Channel 3" },
    { 4, 0, 127, "Channel 4" },
    { 5, 0, 127, "Channel 5" },
    { 6, 0, 127, "Channel 6" },
    { 7, 0, 127, "Channel 7" },
    { 8, 0, 127, "Channel 8" },
    { 9, 0, 127, "Channel 9" },
    { 10, 0, 127, "Channel 10" },
    { 11, 0, 127, "Channel 11" },
    { 12, 0, 127, "Channel 12" },
    { 13, 0, 127, "Channel 13" },
    { 14, 0, 127, "Channel 14" },
    { 15, 0, 127, "Channel 15" },
    { 16, 0, 127, "Channel 16" },
};

DEFINE_MODULE(fluidsynth, 0, 2)

