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

#define CBOX_FLUIDSYNTH_ERROR cbox_fluidsynth_error_quark()

enum CboxFluidsynthError
{
    CBOX_FLUIDSYNTH_ERROR_FAILED,
};

GQuark cbox_fluidsynth_error_quark(void)
{
    return g_quark_from_string("cbox-fluidsynth-error-quark");
}

static void fluidsynth_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs);
static void fluidsynth_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len);
static gboolean fluidsynth_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error);
static void fluidsynth_destroyfunc(struct cbox_module *module);

struct fluidsynth_module
{
    struct cbox_module module;

    fluid_settings_t *settings;
    fluid_synth_t *synth;
    char *bank_name;
    int sfid;
    int output_pairs;
    int is_multi;
    float **left_outputs, **right_outputs;
};

static gboolean select_patch_by_name(struct fluidsynth_module *m, int channel, const gchar *preset, GError **error)
{
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
            fluid_synth_program_select(m->synth, channel, m->sfid, tmp.get_banknum(&tmp), tmp.get_num(&tmp));
            return TRUE;
        }
    }
    
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Preset not found: %s", preset);
    return FALSE;
}

MODULE_CREATE_FUNCTION(fluidsynth)
{
    int result = 0;
    int i;
    const char *bankname = cbox_config_get_string(cfg_section, "sf2");
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
    }
    
    struct fluidsynth_module *m = malloc(sizeof(struct fluidsynth_module));
    int pairs = cbox_config_get_int(cfg_section, "output_pairs", 0);
    m->output_pairs = pairs ? pairs : 1;
    m->is_multi = pairs > 0;
    if (m->output_pairs < 1 || m->output_pairs > 16)
    {
        free(m);
        g_set_error(error, CBOX_FLUIDSYNTH_ERROR, CBOX_FLUIDSYNTH_ERROR_FAILED, "Invalid number of output pairs (found %d, supported range 1-16)", m->output_pairs);
        return NULL;
    }
    if (pairs == 0)
    {
        CALL_MODULE_INIT(m, 0, 2 * m->output_pairs, fluidsynth);
        m->left_outputs = NULL;
        m->right_outputs = NULL;
    }
    else
    {
        g_message("Multichannel mode enabled, %d output pairs, 2 effects", m->output_pairs);
        CALL_MODULE_INIT(m, 0, 2 * m->output_pairs + 4, fluidsynth);
        m->left_outputs = malloc(sizeof(float *) * (m->output_pairs + 2));
        m->right_outputs = malloc(sizeof(float *) * (m->output_pairs + 2));
    }
    m->module.process_event = fluidsynth_process_event;
    m->module.process_block = fluidsynth_process_block;
    m->module.aux_offset = 2 * m->output_pairs;
    m->settings = new_fluid_settings();
    fluid_settings_setnum(m->settings, "synth.sample-rate", m->module.srate);
    fluid_settings_setint(m->settings, "synth.audio-channels", m->output_pairs);
    fluid_settings_setint(m->settings, "synth.audio-groups", m->output_pairs);
    m->synth = new_fluid_synth(m->settings);
    fluid_synth_set_reverb_on(m->synth, cbox_config_get_int(cfg_section, "reverb", 1));
    fluid_synth_set_chorus_on(m->synth, cbox_config_get_int(cfg_section, "chorus", 1));

    m->bank_name = NULL;
    m->sfid = -1;
    if (bankname)
    {
        m->bank_name = g_strdup(bankname);
        g_message("Loading soundfont %s", bankname);
        result = fluid_synth_sfload(m->synth, bankname, 1);
        if (result == FLUID_FAILED)
        {
            g_set_error(error, CBOX_FLUIDSYNTH_ERROR, CBOX_FLUIDSYNTH_ERROR_FAILED, "Failed to load the default bank %s: %s", bankname, fluid_synth_error(m->synth));
            return NULL;
        }
        m->sfid = result;
        g_message("Soundfont %s loaded", bankname);
    }
    if (bankname)
    {
        for (i = 0; i < 16; i++)
        {
            gchar *key = g_strdup_printf("channel%d", i + 1);
            gchar *preset = cbox_config_get_string(cfg_section, key);
            fluid_synth_sfont_select(m->synth, i, m->sfid);
            if (preset)
            {
                if (!select_patch_by_name(m, i, preset, error))
                {
                    CBOX_DELETE(&m->module);
                    return NULL;
                }
            }
            g_free(key);
        }
    }
    
    return &m->module;
}

void fluidsynth_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct fluidsynth_module *m = (struct fluidsynth_module *)module;
    if (!m->is_multi)
        fluid_synth_write_float(m->synth, CBOX_BLOCK_SIZE, outputs[0], 0, 1, outputs[1], 0, 1);
    else
    {
        for (int i = 0; i < 2 + m->output_pairs; i++)
        {
            m->left_outputs[i] = outputs[2 * i];
            m->right_outputs[i] = outputs[2 * i + 1];
        }
        
        fluid_synth_nwrite_float(m->synth, CBOX_BLOCK_SIZE, m->left_outputs, m->right_outputs, m->left_outputs + m->output_pairs, m->right_outputs + m->output_pairs);
    }
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

gboolean fluidsynth_process_load_patch(struct fluidsynth_module *m, const char *bank_name, GError **error)
{
    if (bank_name && !*bank_name)
        bank_name = NULL;
    int old_sfid = m->sfid;
    char *old_bank_name = m->bank_name;
    if (bank_name)
    {
        int result = fluid_synth_sfload(m->synth, bank_name, 1);
        if (result == FLUID_FAILED)
        {
            g_set_error(error, CBOX_FLUIDSYNTH_ERROR, CBOX_FLUIDSYNTH_ERROR_FAILED, "Failed to load the bank %s: %s", bank_name, fluid_synth_error(m->synth));
            return FALSE;
        }
        g_message("Soundfont %s loaded at ID %d", bank_name, result);
        m->sfid = result;
    }
    else
        m->sfid = -1;
    if (old_sfid != -1)
    {
        free(old_bank_name);
        fluid_synth_sfunload(m->synth, old_sfid, 1);
    }
    if (m->sfid != -1)
    {
        for (int i = 0; i < 16; i++)
            fluid_synth_sfont_select(m->synth, i, m->sfid);
    }
    m->bank_name = bank_name ? g_strdup(bank_name) : NULL;
    return TRUE;
}

gboolean fluidsynth_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct fluidsynth_module *m = (struct fluidsynth_module *)ct->user_data;
    
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/polyphony", "i", error, fluid_synth_get_polyphony(m->synth)))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/soundfont", "s", error, m->bank_name ? m->bank_name : ""))
            return FALSE;
        for (int i = 0; i < 16; i++)
        {
            fluid_synth_channel_info_t ci;
            fluid_synth_get_channel_info(m->synth, i, &ci);
            if (!cbox_execute_on(fb, NULL, "/patch", "iis", error, 1 + i, ci.program + 128 * ci.bank, ci.name))
                return FALSE;
        }
        return CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error);
    }
    else if (!strcmp(cmd->command, "/patches") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (m->sfid == -1)
            return TRUE;
        fluid_sfont_t* sfont = fluid_synth_get_sfont(m->synth, 0);
        fluid_preset_t tmp;

        sfont->iteration_start(sfont);            
        while(sfont->iteration_next(sfont, &tmp))
        {
            const char *pname = tmp.get_name(&tmp);
            if (!cbox_execute_on(fb, NULL, "/patch", "is", error, (int)(tmp.get_num(&tmp) + 128 * tmp.get_banknum(&tmp)), pname))
                return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/set_patch") && !strcmp(cmd->arg_types, "ii"))
    {
        if (m->sfid == -1)
        {
            g_set_error(error, CBOX_FLUIDSYNTH_ERROR, CBOX_FLUIDSYNTH_ERROR_FAILED, "No soundfont loaded");
            return FALSE;
        }
        int channel = CBOX_ARG_I(cmd, 0);
        if (channel < 1 || channel > 16)
        {
            g_set_error(error, CBOX_FLUIDSYNTH_ERROR, CBOX_FLUIDSYNTH_ERROR_FAILED, "Invalid channel %d", channel);
            return FALSE;
        }
        int value = CBOX_ARG_I(cmd, 1);
        return fluid_synth_program_select(m->synth, channel - 1, m->sfid, value >> 7, value & 127) == FLUID_OK;
    }
    else if (!strcmp(cmd->command, "/polyphony") && !strcmp(cmd->arg_types, "i"))
    {
        int polyphony = CBOX_ARG_I(cmd, 0);
        if (polyphony < 2 || polyphony > 256)
        {
            g_set_error(error, CBOX_FLUIDSYNTH_ERROR, CBOX_FLUIDSYNTH_ERROR_FAILED, "Invalid polyphony %d (must be between 2 and 256)", polyphony);
            return FALSE;
        }
        return fluid_synth_set_polyphony(m->synth, polyphony) == FLUID_OK;
    }
    else if (!strcmp(cmd->command, "/load_soundfont") && !strcmp(cmd->arg_types, "s"))
    {
        return fluidsynth_process_load_patch(m, CBOX_ARG_S(cmd, 0), error);
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

void fluidsynth_destroyfunc(struct cbox_module *module)
{
    struct fluidsynth_module *m = (struct fluidsynth_module *)module;
    
    if (m->output_pairs)
    {
        free(m->left_outputs);
        free(m->right_outputs);
    }
    free(m->bank_name);
    
    delete_fluid_settings(m->settings);
    delete_fluid_synth(m->synth);
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

