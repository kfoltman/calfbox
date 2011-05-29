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

#include "errors.h"
#include "master.h"
#include <string.h>

static gboolean master_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_master *m = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !*cmd->arg_types)
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/sample_rate", "i", error, m->srate) &&
            cbox_execute_on(fb, NULL, "/tempo", "f", error, m->tempo) &&
            cbox_execute_on(fb, NULL, "/timesig", "ii", error, m->timesig_nom, m->timesig_denom) &&
            cbox_execute_on(fb, NULL, "/playing", "i", error, (int)m->state) &&
            cbox_execute_on(fb, NULL, "/pos", "i", error, m->song_pos_samples);
    }
    else
    if (!strcmp(cmd->command, "/tell") && !*cmd->arg_types)
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/playing", "i", error, (int)m->state) &&
            cbox_execute_on(fb, NULL, "/pos", "i", error, m->song_pos_samples);
    }
    else
    if (!strcmp(cmd->command, "/set_tempo") && !strcmp(cmd->arg_types, "f"))
    {
        cbox_master_set_tempo(m, *(double *)cmd->arg_values[0]);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/set_timesig") && !strcmp(cmd->arg_types, "ii"))
    {
        cbox_master_set_timesig(m, *(int *)cmd->arg_values[0], *(int *)cmd->arg_values[1]);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/play") && !strcmp(cmd->arg_types, ""))
    {
        cbox_master_play(m);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/stop") && !strcmp(cmd->arg_types, ""))
    {
        cbox_master_stop(m);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/seek") && !strcmp(cmd->arg_types, "i"))
    {
        cbox_master_seek(m, *(int *)cmd->arg_values[0]);
        return TRUE;
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

void cbox_master_init(struct cbox_master *master)
{
    master->song_pos_samples = 0;
    master->srate = 0;
    master->tempo = 120.0;
    master->timesig_nom = 4;
    master->timesig_denom = 4;
    master->state = CMTS_STOP;
    master->cmd_target.user_data = master;
    master->cmd_target.process_cmd = master_process_cmd;
}

void cbox_master_set_sample_rate(struct cbox_master *master, int srate)
{
    master->srate = srate;
}

void cbox_master_set_tempo(struct cbox_master *master, float tempo)
{
    master->tempo = tempo;
}

void cbox_master_set_timesig(struct cbox_master *master, int beats, int unit)
{
    master->timesig_nom = beats;
    master->timesig_denom = unit;
}

void cbox_master_to_bbt(const struct cbox_master *master, struct cbox_bbt *bbt)
{
    double second = ((double)master->song_pos_samples) / master->srate;
    double beat = master->tempo * second / 60;
    int beat_int = (int)beat;
    bbt->bar = beat_int / master->timesig_nom;
    bbt->beat = beat_int % master->timesig_nom;
    bbt->tick = (beat - beat_int) * PPQN; // XXXKF what if timesig_denom is not 4?
    
}

uint32_t cbox_master_song_pos_from_bbt(struct cbox_master *master, const struct cbox_bbt *bbt)
{
    double beat = bbt->bar * master->timesig_nom + bbt->beat + bbt->tick * 1.0 / PPQN;
    return (uint32_t)(master->srate * 60 / master->tempo);
}

void cbox_master_play(struct cbox_master *master)
{
    master->state = CMTS_ROLLING;
}

void cbox_master_stop(struct cbox_master *master)
{
    master->state = CMTS_STOP;
}

void cbox_master_seek(struct cbox_master *master, uint32_t song_pos_samples)
{
    master->song_pos_samples = song_pos_samples;
}

