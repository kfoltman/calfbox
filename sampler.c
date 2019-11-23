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

#include "config-api.h"
#include "dspmath.h"
#include "errors.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_impl.h"
#include "sfzloader.h"
#include "stm.h"
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

float sampler_sine_wave[2049];

GQuark cbox_sampler_error_quark()
{
    return g_quark_from_string("cbox-sampler-error-quark");
}

static void sampler_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs);
static void sampler_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len);
static void sampler_destroyfunc(struct cbox_module *module);

void sampler_steal_voice(struct sampler_module *m)
{
    int max_age = 0;
    struct sampler_voice *voice_found = NULL;
    for (int i = 0; i < 16; i++)
    {
        FOREACH_VOICE(m->channels[i].voices_running, v)
        {
            if (v->amp_env.cur_stage == 15)
                continue;
            int age = m->serial_no - v->serial_no;
            if (v->gen.loop_start == (uint32_t)-1)
                age += (int)((v->gen.bigpos >> 32) * 100.0 / v->gen.cur_sample_end);
            else
            if (v->released)
                age += 10;
            if (age > max_age)
            {
                max_age = age;
                voice_found = v;
            }
        }
    }
    if (voice_found)
    {
        voice_found->released = 1;
        cbox_envelope_go_to(&voice_found->amp_env, 15);
    }
}

static inline float clip01(float v)
{
    if (v < 0.f)
        return 0;
    if (v > 1.f)
        return 1;
    return v;
}

void sampler_create_voice_from_prevoice(struct sampler_module *m, struct sampler_prevoice *pv)
{
    if (!m->voices_free)
        return;
    int exgroups[MAX_RELEASED_GROUPS], exgroupcount = 0;
    sampler_voice_start(m->voices_free, pv->channel, pv->layer_data, pv->note, pv->vel, exgroups, &exgroupcount);
    if (exgroupcount)
        sampler_channel_release_groups(pv->channel, pv->note, exgroups, exgroupcount);
}

void sampler_process_block(struct cbox_module *module, cbox_sample_t **inputs, cbox_sample_t **outputs)
{
    struct sampler_module *m = (struct sampler_module *)module;
    
    int active_prevoices[16];
    uint32_t active_prevoices_mask = 0;
    FOREACH_PREVOICE(m->prevoices_running, pv)
    {
        uint32_t c = pv->channel - m->channels;
        active_prevoices[c] = (active_prevoices_mask >> c) & 1 ? 1 + active_prevoices[c] : 1;
        active_prevoices_mask |= 1 << c;
        if (sampler_prevoice_process(pv, m))
        {
            sampler_prevoice_unlink(&m->prevoices_running, pv);
            sampler_create_voice_from_prevoice(m, pv);
            sampler_prevoice_link(&m->prevoices_free, pv);
        }
    }

    for (int c = 0; c < m->output_pairs + m->aux_pairs; c++)
    {
        int oo = 2 * c;
        for (int i = 0; i < CBOX_BLOCK_SIZE; i++)
            outputs[oo][i] = outputs[oo + 1][i] = 0.f;
    }
    
    int vcount = 0, vrel = 0, pvcount = 0;
    for (int i = 0; i < 16; i++)
    {
        int cvcount = 0;
        FOREACH_VOICE(m->channels[i].voices_running, v)
        {
            sampler_voice_process(v, m, outputs);

            if (v->amp_env.cur_stage == 15)
                vrel++;
            cvcount++;
        }
        int cpvcount = (active_prevoices_mask >> i) & 1 ? active_prevoices[i] : 0;
        m->channels[i].active_voices = cvcount;
        m->channels[i].active_prevoices = cpvcount;
        vcount += cvcount;
        pvcount += cpvcount;
    }
    m->active_voices = vcount;
    m->active_prevoices = pvcount;
    if (vcount - vrel > m->max_voices)
        sampler_steal_voice(m);
    m->serial_no++;
    m->current_time += CBOX_BLOCK_SIZE;
}

void sampler_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len)
{
    struct sampler_module *m = (struct sampler_module *)module;
    if (len > 0)
    {
        int cmd = data[0] >> 4;
        int chn = data[0] & 15;
        struct sampler_channel *c = &m->channels[chn];
        switch(cmd)
        {
            case 8:
                sampler_channel_stop_note(c, data[1], data[2], FALSE);
                break;

            case 9:
                if (data[2] > 0)
                    sampler_channel_start_note(c, data[1], data[2], FALSE);
                else
                    sampler_channel_stop_note(c, data[1], data[2], FALSE);
                break;
            
            case 10:
                c->last_polyaft = data[2];
                // Lazy clearing
                if (!(c->poly_pressure_mask & (1 << (data[1] >> 2))))
                {
                    // Clear the group of 4
                    memset(c->poly_pressure + (data[1] & ~3), 0, 4);
                    c->poly_pressure_mask |= 1 << (data[1] >> 2);
                }
                c->poly_pressure[data[1]] = data[2];
                // XXXKF add a new opcode for cymbal chokes via poly pressure
                // if (data[2] == 127)
                //    sampler_channel_stop_note(c, data[1], data[2], TRUE);
                break;
            
            case 11:
                sampler_channel_process_cc(c, data[1], data[2]);
                break;

            case 12:
                sampler_channel_program_change(c, data[1]);
                break;

            case 13:
                c->last_chanaft = data[1];
                break;

            case 14:
                c->pitchwheel = data[1] + 128 * data[2] - 8192;
                break;

            }
    }
}

static int get_first_free_program_no(struct sampler_module *m)
{
    int prog_no = -1;
    gboolean found;
    
    // XXXKF this has a N-squared complexity - but I'm not seeing
    // this being used with more than 10 programs at the same time
    // in the near future
    do {
        prog_no++;
        found = FALSE;
        for (uint32_t i = 0; i < m->program_count; i++)
        {
            if (m->programs[i]->prog_no == prog_no)
            {
                found = TRUE;
                break;
            }
        }        
    } while(found);
    
    return prog_no;
}

static int find_program(struct sampler_module *m, int prog_no)
{
    for (uint32_t i = 0; i < m->program_count; i++)
    {
        if (m->programs[i]->prog_no == prog_no)
            return i;
    }
    return -1;
}

struct release_program_voices_data
{
    struct sampler_module *module;
    
    struct sampler_program *old_pgm, *new_pgm;
    uint16_t channels_to_wait_for;
};

static int release_program_voices_execute(void *data)
{
    struct release_program_voices_data *rpv = data;
    struct sampler_module *m = rpv->module;
    int finished = 1;
    
    FOREACH_PREVOICE(m->prevoices_running, pv)
    {
        if (pv->channel->program == rpv->old_pgm)
        {
            sampler_prevoice_unlink(&m->prevoices_running, pv);
            sampler_prevoice_link(&m->prevoices_free, pv);            
        }
    }
    for (int i = 0; i < 16; i++)
    {
        uint16_t mask = 1 << i;
        struct sampler_channel *c = &m->channels[i];
        if (c->program == rpv->old_pgm || c->program == NULL)
        {
            sampler_channel_set_program_RT(c, rpv->new_pgm);
            rpv->channels_to_wait_for |= mask;
        }
        if (rpv->channels_to_wait_for & mask)
        {
            FOREACH_VOICE(c->voices_running, v)
            {
                if (m->deleting)
                {
                    sampler_voice_inactivate(v, TRUE);
                    continue;
                }
                // This is a new voice, started after program change, so it
                // should not be terminated and waited for.
                if (v->program == rpv->new_pgm)
                    continue;
                // The voice is still going, so repeat until it fades out
                finished = 0;
                // If not in final fadeout stage, force final fadeout.
                if (v->amp_env.cur_stage != 15)
                {
                    v->released = 1;
                    cbox_envelope_go_to(&v->amp_env, 15);
                }
            }
        }
    }
    
    return finished;
}

static void swap_program(struct sampler_module *m, int index, struct sampler_program *pgm, gboolean delete_old)
{
    static struct cbox_rt_cmd_definition release_program_voices = { NULL, release_program_voices_execute, NULL };
    
    struct sampler_program *old_program = NULL;
    if (pgm)
        old_program = cbox_rt_swap_pointers(m->module.rt, (void **)&m->programs[index], pgm);
    else
        old_program = cbox_rt_array_remove(m->module.rt, (void ***)&m->programs, &m->program_count, index);

    struct release_program_voices_data data = {m, old_program, pgm, 0};

    cbox_rt_execute_cmd_sync(m->module.rt, &release_program_voices, &data);
    
    if (delete_old && old_program)
        CBOX_DELETE(old_program);
}

static void select_initial_program(struct sampler_module *m)
{
    static struct cbox_rt_cmd_definition release_program_voices = { NULL, release_program_voices_execute, NULL };
    struct release_program_voices_data data = {m, NULL, m->programs[0], 0};
    cbox_rt_execute_cmd_sync(m->module.rt, &release_program_voices, &data);
}

void sampler_register_program(struct sampler_module *m, struct sampler_program *pgm)
{
    struct sampler_program **programs = malloc(sizeof(struct sampler_program *) * (m->program_count + 1));
    memcpy(programs, m->programs, sizeof(struct sampler_program *) * m->program_count);
    programs[m->program_count] = pgm;
    free(cbox_rt_swap_pointers_and_update_count(m->module.rt, (void **)&m->programs, programs, &m->program_count, m->program_count + 1));
    if (m->program_count == 1U)
        select_initial_program(m);
}

static gboolean load_program_at(struct sampler_module *m, const char *cfg_section, const char *name, int prog_no, struct sampler_program **ppgm, GError **error)
{
    struct sampler_program *pgm = NULL;
    int index = find_program(m, prog_no);
    pgm = sampler_program_new_from_cfg(m, cfg_section, name, prog_no, error);
    if (!pgm)
        return FALSE;
    
    if (index != -1)
    {
        swap_program(m, index, pgm, TRUE);
        return TRUE;
    }

    sampler_register_program(m, pgm);
    if (ppgm)
        *ppgm = pgm;
    return TRUE;
}

void sampler_unselect_program(struct sampler_module *m, struct sampler_program *prg)
{
    // Ensure no new notes are played on that program
    prg->deleting = TRUE;
    // Remove from the list of available programs, so that it cannot be selected again
    for (uint32_t i = 0; i < m->program_count; i++)
    {
        if (m->programs[i] == prg)
            swap_program(m, i, NULL, FALSE);
    }
}

static gboolean load_from_string(struct sampler_module *m, const char *sample_dir, const char *sfz_data, const char *name, int prog_no, struct sampler_program **ppgm, GError **error)
{
    int index = find_program(m, prog_no);
    struct sampler_program *pgm = sampler_program_new(m, prog_no, name, NULL, sample_dir, error);
    if (!pgm)
        return FALSE;
    pgm->source_file = g_strdup("string");
    if (!sampler_module_load_program_sfz(m, pgm, sfz_data, TRUE, error))
    {
        free(pgm);
        return FALSE;
    }

    if (index != -1)
    {
        swap_program(m, index, pgm, TRUE);
        if (ppgm)
            *ppgm = pgm;
        return TRUE;
    }
    
    struct sampler_program **programs = calloc((m->program_count + 1), sizeof(struct sampler_program *));
    memcpy(programs, m->programs, sizeof(struct sampler_program *) * m->program_count);
    programs[m->program_count] = pgm;
    if (ppgm)
        *ppgm = pgm;
    free(cbox_rt_swap_pointers_and_update_count(m->module.rt, (void **)&m->programs, programs, &m->program_count, m->program_count + 1));    
    if (m->program_count == 1)
        select_initial_program(m);
    return TRUE;
}

gboolean sampler_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct sampler_module *m = (struct sampler_module *)ct->user_data;
    
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (int i = 0; i < 16; i++)
        {
            struct sampler_channel *channel = &m->channels[i];
            gboolean result;
            if (channel->program)
                result = cbox_execute_on(fb, NULL, "/patch", "iis", error, i + 1, channel->program->prog_no, channel->program->name);
            else
                result = cbox_execute_on(fb, NULL, "/patch", "iis", error, i + 1, -1, "");
            if (!result)
                return FALSE;
            if (!(cbox_execute_on(fb, NULL, "/channel_voices", "ii", error, i + 1, channel->active_voices) &&
                cbox_execute_on(fb, NULL, "/channel_prevoices", "ii", error, i + 1, channel->active_prevoices) &&
                cbox_execute_on(fb, NULL, "/output", "ii", error, i + 1, channel->output_shift) &&
                cbox_execute_on(fb, NULL, "/volume", "ii", error, i + 1, sampler_channel_addcc(channel, 7)) &&
                cbox_execute_on(fb, NULL, "/pan", "ii", error, i + 1, sampler_channel_addcc(channel, 10))))
                return FALSE;
        }
        
        return cbox_execute_on(fb, NULL, "/active_voices", "i", error, m->active_voices) &&
            cbox_execute_on(fb, NULL, "/active_prevoices", "i", error, m->active_prevoices) &&
            cbox_execute_on(fb, NULL, "/active_pipes", "i", error, cbox_prefetch_stack_get_active_pipe_count(m->pipe_stack)) &&
            cbox_execute_on(fb, NULL, "/polyphony", "i", error, m->max_voices) && 
            CBOX_OBJECT_DEFAULT_STATUS(&m->module, fb, error);
    }
    else
    if (!strcmp(cmd->command, "/patches") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (uint32_t i = 0; i < m->program_count; i++)
        {
            struct sampler_program *prog = m->programs[i];
            if (!cbox_execute_on(fb, NULL, "/patch", "isoi", error, prog->prog_no, prog->name, prog, prog->in_use))
                return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/polyphony") && !strcmp(cmd->arg_types, "i"))
    {
        int polyphony = CBOX_ARG_I(cmd, 0);
        if (polyphony < 1 || polyphony > MAX_SAMPLER_VOICES)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid polyphony %d (must be between 1 and %d)", polyphony, (int)MAX_SAMPLER_VOICES);
            return FALSE;
        }
        m->max_voices = polyphony;
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/set_patch") && !strcmp(cmd->arg_types, "ii"))
    {
        int channel = CBOX_ARG_I(cmd, 0);
        if (channel < 1 || channel > 16)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid channel %d", channel);
            return FALSE;
        }
        int value = CBOX_ARG_I(cmd, 1);
        struct sampler_program *pgm = NULL;
        for (uint32_t i = 0; i < m->program_count; i++)
        {
            if (m->programs[i]->prog_no == value)
            {
                pgm = m->programs[i];
                break;
            }
        }
        sampler_channel_set_program(&m->channels[channel - 1], pgm);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/set_output") && !strcmp(cmd->arg_types, "ii"))
    {
        int channel = CBOX_ARG_I(cmd, 0);
        int output = CBOX_ARG_I(cmd, 1);
        if (channel < 1 || channel > 16)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid channel %d", channel);
            return FALSE;
        }
        if (output < 0 || output >= m->output_pairs)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid output %d", output);
            return FALSE;
        }
        m->channels[channel - 1].output_shift = output;
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load_patch") && !strcmp(cmd->arg_types, "iss"))
    {
        struct sampler_program *pgm = NULL;
        if (!load_program_at(m, CBOX_ARG_S(cmd, 1), CBOX_ARG_S(cmd, 2), CBOX_ARG_I(cmd, 0), &pgm, error))
            return FALSE;
        if (fb)
            return cbox_execute_on(fb, NULL, "/uuid", "o", error, pgm);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load_patch_from_file") && !strcmp(cmd->arg_types, "iss"))
    {
        struct sampler_program *pgm = NULL;
        char *cfg_section = g_strdup_printf("spgm:!%s", CBOX_ARG_S(cmd, 1));
        gboolean res = load_program_at(m, cfg_section, CBOX_ARG_S(cmd, 2), CBOX_ARG_I(cmd, 0), &pgm, error);
        g_free(cfg_section);
        if (res && pgm && fb)
            return cbox_execute_on(fb, NULL, "/uuid", "o", error, pgm);
        return res;
    }
    else if (!strcmp(cmd->command, "/load_patch_from_string") && !strcmp(cmd->arg_types, "isss"))
    {
        struct sampler_program *pgm = NULL; 
        if (!load_from_string(m, CBOX_ARG_S(cmd, 1), CBOX_ARG_S(cmd, 2), CBOX_ARG_S(cmd, 3), CBOX_ARG_I(cmd, 0), &pgm, error))
            return FALSE;
        if (fb && pgm)
            return cbox_execute_on(fb, NULL, "/uuid", "o", error, pgm);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/get_unused_program") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/program_no", "i", error, get_first_free_program_no(m));
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
    return TRUE;
}

gboolean sampler_select_program(struct sampler_module *m, int channel, const gchar *preset, GError **error)
{
    for (uint32_t i = 0; i < m->program_count; i++)
    {
        if (!strcmp(m->programs[i]->name, preset))
        {
            sampler_channel_set_program(&m->channels[channel], m->programs[i]);
            return TRUE;
        }
    }
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Preset not found: %s", preset);
    return FALSE;
}

MODULE_CREATE_FUNCTION(sampler)
{
    int i;
    static int inited = 0;
    if (!inited)
    {
        for (int i = 0; i < 2049; i++)
            sampler_sine_wave[i] = sin(i * M_PI / 1024.0);
        inited = 1;
    }
    
    int max_voices = cbox_config_get_int(cfg_section, "polyphony", MAX_SAMPLER_VOICES);
    if (max_voices < 1 || max_voices > MAX_SAMPLER_VOICES)
    {
        g_set_error(error, CBOX_SAMPLER_ERROR, CBOX_SAMPLER_ERROR_INVALID_LAYER, "%s: invalid polyphony value", cfg_section);
        return NULL;
    }
    int output_pairs = cbox_config_get_int(cfg_section, "output_pairs", 1);
    if (output_pairs < 1 || output_pairs > 16)
    {
        g_set_error(error, CBOX_SAMPLER_ERROR, CBOX_SAMPLER_ERROR_INVALID_LAYER, "%s: invalid output pairs value", cfg_section);
        return NULL;
    }
    int aux_pairs = cbox_config_get_int(cfg_section, "aux_pairs", 0);
    if (aux_pairs < 0 || aux_pairs > 4)
    {
        g_set_error(error, CBOX_SAMPLER_ERROR, CBOX_SAMPLER_ERROR_INVALID_LAYER, "%s: invalid aux pairs value", cfg_section);
        return NULL;
    }
    
    struct sampler_module *m = calloc(1, sizeof(struct sampler_module));
    CALL_MODULE_INIT(m, 0, (output_pairs + aux_pairs) * 2, sampler);
    m->output_pairs = output_pairs;
    m->aux_pairs = aux_pairs;
    m->module.aux_offset = m->output_pairs * 2;
    m->module.process_event = sampler_process_event;
    m->module.process_block = sampler_process_block;
    m->programs = NULL;
    m->max_voices = max_voices;
    m->serial_no = 0;
    m->deleting = FALSE;
    // XXXKF read defaults from some better place, like config
    // XXXKF allow dynamic change of the number of the pipes
    m->pipe_stack = cbox_prefetch_stack_new(MAX_SAMPLER_VOICES, cbox_config_get_int("streaming", "streambuf_size", 65536), cbox_config_get_int("streaming", "min_buf_frames", PIPE_MIN_PREFETCH_SIZE_FRAMES));
    m->disable_mixer_controls = cbox_config_get_int("sampler", "disable_mixer_controls", 0);

    float srate = m->module.srate;
    for (i = 0; i < 12800; i++)
    {
        float freq = 440 * pow(2, (i - 5700) / 1200.0);
        if (freq < 20.0)
            freq = 20.0;
        if (freq > srate * 0.45)
            freq = srate * 0.45;
        float omega=(float)(2*M_PI*freq/srate);
        m->sincos[i].sine = sinf(omega);
        m->sincos[i].cosine = cosf(omega);
        m->sincos[i].prewarp = 2.0 * tan(hz2w(freq, srate) * 0.5f);
        m->sincos[i].prewarp2 = 1.0 / (1.0 + m->sincos[i].prewarp);

    }
    for (i = 0; ; i++)
    {
        gchar *s = g_strdup_printf("program%d", i);
        char *p = cbox_config_get_string(cfg_section, s);
        g_free(s);
        
        if (!p)
        {
            m->program_count = i;
            break;
        }
    }
    m->programs = calloc(m->program_count, sizeof(struct sampler_program *));
    int success = 1;
    for (i = 0; i < (int)m->program_count; i++)
    {
        gchar *s = g_strdup_printf("program%d", i);
        char *pgm_section = NULL;
        int pgm_id = -1;
        const char *pgm_name = cbox_config_get_string(cfg_section, s);
        g_free(s);
        char *at = strchr(pgm_name, '@');
        if (at)
        {
            pgm_id = atoi(at + 1);
            s = g_strndup(pgm_name, at - pgm_name);
            pgm_section = g_strdup_printf("spgm:%s", s);
            g_free(s);
        }
        else
        {
            pgm_id = i;
            pgm_section = g_strdup_printf("spgm:%s", pgm_name);
        }
        
        m->programs[i] = sampler_program_new_from_cfg(m, pgm_section, pgm_section + 5, pgm_id, error);
        g_free(pgm_section);
        if (!m->programs[i])
        {
            success = 0;
            break;
        }
    }
    if (!success)
    {
        // XXXKF free programs/layers, first ensuring that they're fully initialised
        free(m);
        return NULL;
    }
    m->voices_free = NULL;
    memset(m->voices_all, 0, sizeof(m->voices_all));
    for (i = 0; i < MAX_SAMPLER_VOICES; i++)
    {
        struct sampler_voice *v = &m->voices_all[i];
        v->gen.mode = spt_inactive;
        sampler_voice_link(&m->voices_free, v);
    }
    m->active_voices = 0;
    m->active_prevoices = 0;

    m->prevoices_free = NULL;
    memset(m->prevoices_all, 0, sizeof(m->prevoices_all));
    for (i = 0; i < MAX_SAMPLER_PREVOICES; i++)
    {
        struct sampler_prevoice *v = &m->prevoices_all[i];
        sampler_prevoice_link(&m->prevoices_free, v);
    }
    
    for (i = 0; i < 16; i++)
        sampler_channel_init(&m->channels[i], m);

    for (i = 0; i < 16; i++)
    {
        gchar *key = g_strdup_printf("channel%d", i + 1);
        gchar *preset = cbox_config_get_string(cfg_section, key);
        if (preset)
        {
            if (!sampler_select_program(m, i, preset, error))
            {
                CBOX_DELETE(&m->module);
                return NULL;
            }
        }
        g_free(key);
        key = g_strdup_printf("channel%d_output", i + 1);
        m->channels[i].output_shift = cbox_config_get_int(cfg_section, key, 1) - 1;
        g_free(key);
    }
    

    return &m->module;
}

void sampler_destroyfunc(struct cbox_module *module)
{
    struct sampler_module *m = (struct sampler_module *)module;
    uint32_t i;
    m->deleting = TRUE;
    
    for (i = 0; i < m->program_count;)
    {
        if (m->programs[i])
            CBOX_DELETE(m->programs[i]);
        else
            i++;
    }
    for (i = 0; i < 16U; i++)
    {
        assert (m->channels[i].voices_running == NULL);
    }
    cbox_prefetch_stack_destroy(m->pipe_stack);
    free(m->programs);
}

#define MAKE_TO_STRING_CONTENT(name, v) \
    case v: return name;

#define MAKE_FROM_STRING_CONTENT(n, v) \
    if (!strcmp(name, n)) { *value = v; return TRUE; }

#define MAKE_FROM_TO_STRING(enumtype) \
    const char *enumtype##_to_string(enum enumtype value) \
    { \
        switch(value) { \
            ENUM_VALUES_##enumtype(MAKE_TO_STRING_CONTENT) \
            default: return NULL; \
        } \
    } \
    \
    gboolean enumtype##_from_string(const char *name, enum enumtype *value) \
    { \
        ENUM_VALUES_##enumtype(MAKE_FROM_STRING_CONTENT) \
        return FALSE; \
    }

ENUM_LIST(MAKE_FROM_TO_STRING)

//////////////////////////////////////////////////////////////////////////

struct cbox_module_livecontroller_metadata sampler_controllers[] = {
};

struct cbox_module_keyrange_metadata sampler_keyranges[] = {
};

DEFINE_MODULE(sampler, 0, 2)

