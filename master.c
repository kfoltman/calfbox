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
#include "seq.h"
#include "rt.h"
#include "song.h"
#include <string.h>

static gboolean master_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_master *m = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !*cmd->arg_types)
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/sample_rate", "i", error, m->srate))
            return FALSE;
        if (!m->spb)
            return TRUE;
        return cbox_execute_on(fb, NULL, "/tempo", "f", error, m->tempo) &&
            cbox_execute_on(fb, NULL, "/timesig", "ii", error, m->timesig_nom, m->timesig_denom) &&
            cbox_execute_on(fb, NULL, "/playing", "i", error, (int)m->state) &&
            cbox_execute_on(fb, NULL, "/pos", "i", error, m->spb->song_pos_samples) &&
            cbox_execute_on(fb, NULL, "/pos_ppqn", "i", error, m->spb->song_pos_ppqn);
    }
    else
    if (!strcmp(cmd->command, "/tell") && !*cmd->arg_types)
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!m->spb)
            return TRUE;
        return cbox_execute_on(fb, NULL, "/playing", "i", error, (int)m->state) &&
            cbox_execute_on(fb, NULL, "/pos", "i", error, m->spb->song_pos_samples) &&
            cbox_execute_on(fb, NULL, "/pos_ppqn", "i", error, m->spb->song_pos_ppqn);
    }
    else
    if (!strcmp(cmd->command, "/set_tempo") && !strcmp(cmd->arg_types, "f"))
    {
        cbox_master_set_tempo(m, CBOX_ARG_F(cmd, 0));
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/set_timesig") && !strcmp(cmd->arg_types, "ii"))
    {
        cbox_master_set_timesig(m, CBOX_ARG_I(cmd, 0), CBOX_ARG_I(cmd, 1));
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
    if (!strcmp(cmd->command, "/seek_samples") && !strcmp(cmd->arg_types, "i"))
    {
        if (m->spb)
            cbox_song_playback_seek_samples(m->spb, CBOX_ARG_I(cmd, 0));
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/seek_ppqn") && !strcmp(cmd->arg_types, "i"))
    {
        if (m->spb)
            cbox_song_playback_seek_ppqn(m->spb, CBOX_ARG_I(cmd, 0), FALSE);
        return TRUE;
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

static void cbox_master_init(struct cbox_master *master, struct cbox_rt *rt)
{
    master->srate = 0;
    master->tempo = 120.0;
    master->new_tempo = 120.0;
    master->timesig_nom = 4;
    master->timesig_denom = 4;
    master->state = CMTS_STOP;
    master->rt = rt;
    master->song = NULL;
    master->spb = NULL;
    cbox_command_target_init(&master->cmd_target, master_process_cmd, master);
}

struct cbox_master *cbox_master_new(struct cbox_rt *rt)
{
    struct cbox_master *master = malloc(sizeof(struct cbox_master));
    cbox_master_init(master, rt);
    return master;
}


void cbox_master_set_sample_rate(struct cbox_master *master, int srate)
{
    master->srate = srate;
}

void cbox_master_set_tempo(struct cbox_master *master, float tempo)
{
    // XXXKF not realtime-safe; won't crash, but may lose tempo
    // changes when used multiple times in rapid succession
    master->new_tempo = tempo;
}

void cbox_master_set_timesig(struct cbox_master *master, int beats, int unit)
{
    master->timesig_nom = beats;
    master->timesig_denom = unit;
}

/*
void cbox_master_to_bbt(const struct cbox_master *master, struct cbox_bbt *bbt, int time_samples)
{
    double second = ((double)time_samples) / master->srate;
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
*/

void cbox_master_play(struct cbox_master *master)
{
    master->state = CMTS_ROLLING;
}

static int stop_transport_execute(void *arg)
{
    struct cbox_master *master = arg;
    if (master->state == CMTS_ROLLING)
        master->state = CMTS_STOPPING;
    
    return master->state == CMTS_STOP;
}

void cbox_master_stop(struct cbox_master *master)
{
    static struct cbox_rt_cmd_definition cmd = { NULL, stop_transport_execute, NULL };
    cbox_rt_execute_cmd_sync(master->rt, &cmd, master);
}

int cbox_master_ppqn_to_samples(struct cbox_master *master, int time_ppqn)
{
    double tempo = master->tempo;
    int offset = 0;
    if (master->spb)
    {
        int idx = cbox_song_playback_tmi_from_ppqn(master->spb, time_ppqn);
        if (idx != -1)
        {
            const struct cbox_tempo_map_item *tmi = &master->spb->tempo_map_items[idx];
            tempo = tmi->tempo;
            time_ppqn -= tmi->time_ppqn;
            offset = tmi->time_samples;
        }
    }
    return offset + (int)(master->srate * 60.0 * time_ppqn / (tempo * PPQN));
}

int cbox_master_samples_to_ppqn(struct cbox_master *master, int time_samples)
{
    double tempo = master->tempo;
    int offset = 0;
    if (master->spb)
    {
        int idx = cbox_song_playback_tmi_from_samples(master->spb, time_samples);
        if (idx != -1)
        {
            const struct cbox_tempo_map_item *tmi = &master->spb->tempo_map_items[idx];
            tempo = tmi->tempo;
            time_samples -= tmi->time_samples;
            offset = tmi->time_ppqn;
        }
    }
    return offset + (int)(tempo * PPQN * time_samples / (master->srate * 60.0));
}

void cbox_master_destroy(struct cbox_master *master)
{
    if (master->spb)
    {
        cbox_song_playback_destroy(master->spb);
        master->spb = NULL;
    }
    free(master);
}
