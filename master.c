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

#include "engine.h"
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
            cbox_execute_on(fb, NULL, "/timesig", "ii", error, m->timesig_num, m->timesig_denom) &&
            cbox_execute_on(fb, NULL, "/playing", "i", error, (int)m->state) &&
            cbox_execute_on(fb, NULL, "/pos", "i", error, m->spb->song_pos_samples) &&
            cbox_execute_on(fb, NULL, "/pos_ppqn", "i", error, m->spb->song_pos_ppqn) &&
            cbox_execute_on(fb, NULL, "/ppqn_factor", "i", error, (int)m->ppqn_factor);
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
    if (!strcmp(cmd->command, "/set_ppqn_factor") && !strcmp(cmd->arg_types, "i"))
    {
        m->ppqn_factor = CBOX_ARG_I(cmd, 0);
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
    if (!strcmp(cmd->command, "/panic") && !strcmp(cmd->arg_types, ""))
    {
        cbox_master_panic(m);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/seek_samples") && !strcmp(cmd->arg_types, "i"))
    {
        cbox_master_seek_samples(m, CBOX_ARG_I(cmd, 0));
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/seek_ppqn") && !strcmp(cmd->arg_types, "i"))
    {
        cbox_master_seek_ppqn(m, CBOX_ARG_I(cmd, 0));
        return TRUE;
    }
    else
    if ((!strcmp(cmd->command, "/samples_to_ppqn") || !strcmp(cmd->command, "/ppqn_to_samples")) && 
        !strcmp(cmd->arg_types, "i"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (m->spb)
        {
            if (cmd->command[1] == 's')
                return cbox_execute_on(fb, NULL, "/value", "i", error, cbox_master_samples_to_ppqn(m, CBOX_ARG_I(cmd, 0)));
            else
                return cbox_execute_on(fb, NULL, "/value", "i", error, cbox_master_ppqn_to_samples(m, CBOX_ARG_I(cmd, 0)));
        }
        else
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Song playback not initialised.");
            return FALSE;
        }
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

static void cbox_master_init(struct cbox_master *master, struct cbox_engine *engine)
{
    master->srate = engine->io_env.srate;
    master->tempo = 120.0;
    master->new_tempo = 120.0;
    master->timesig_num = 4;
    master->timesig_denom = 4;
    master->state = CMTS_STOP;
    master->engine = engine;
    master->song = NULL;
    master->spb = NULL;
    master->ppqn_factor = 48;
    cbox_command_target_init(&master->cmd_target, master_process_cmd, master);
}

struct cbox_master *cbox_master_new(struct cbox_engine *engine)
{
    struct cbox_master *master = malloc(sizeof(struct cbox_master));
    cbox_master_init(master, engine);
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
    master->timesig_num = beats;
    master->timesig_denom = unit;
}

#define cbox_master_play_args(ARG)

DEFINE_RT_VOID_FUNC(cbox_master, master, cbox_master_play)
{
    struct cbox_rt *rt = master->engine->rt;
    if (rt && rt->io && rt->io->impl->controltransportfunc)
    {
        rt->io->impl->controltransportfunc(rt->io->impl, FALSE, master->spb ? master->spb->song_pos_samples : (uint32_t)-1);
        if (!rt->io->impl->getsynccompletedfunc(rt->io->impl))
            RT_CALL_AGAIN_LATER();
        rt->io->impl->controltransportfunc(rt->io->impl, TRUE, master->spb ? master->spb->song_pos_samples : (uint32_t)-1);
        return;
    }
    // wait for the notes to be released
    if (master->state == CMTS_STOPPING)
    {
        RT_CALL_AGAIN_LATER();
        return;
    }
    
    master->state = CMTS_ROLLING;
}

#define cbox_master_stop_args(ARG)

DEFINE_RT_VOID_FUNC(cbox_master, master, cbox_master_stop)
{
    struct cbox_rt *rt = master->engine->rt;
    if (rt && rt->io && rt->io->impl->controltransportfunc)
    {
        rt->io->impl->controltransportfunc(rt->io->impl, FALSE, -1);
        return;
    }
    if (master->state == CMTS_ROLLING)
        master->state = CMTS_STOPPING;
    
    if (master->state != CMTS_STOP)
        RT_CALL_AGAIN_LATER();
}

struct seek_command_arg
{
    struct cbox_master *master;
    gboolean is_ppqn;
    uint32_t target_pos;
    gboolean was_rolling;
    gboolean status_known;
    gboolean seek_in_progress;
};

static int seek_transport_execute(void *arg_)
{
    struct seek_command_arg *arg = arg_;

    struct cbox_rt *rt = arg->master->engine->rt;
    if (rt && rt->io && rt->io->impl->controltransportfunc)
    {
        if (!arg->seek_in_progress)
        {
            arg->seek_in_progress = TRUE;
            uint32_t pos = arg->target_pos;
            if (arg->is_ppqn)
                arg->target_pos = pos = cbox_master_ppqn_to_samples(arg->master, pos);

            rt->io->impl->controltransportfunc(rt->io->impl, arg->master->state == CMTS_ROLLING, pos);
            // JACK slow-sync won't be performed if unless transport is rolling
            if (!arg->was_rolling)
            {
                if (arg->master->spb)
                    cbox_song_playback_seek_samples(arg->master->spb, arg->target_pos);
                return 5;
            }
        }
        if (rt->io->impl->getsynccompletedfunc(rt->io->impl))
        {
            if (arg->master->spb)
                cbox_song_playback_seek_samples(arg->master->spb, arg->target_pos);
            return 5;
        }
        return 0;
    }

    // On first pass, check if transport is rolling from the DSP thread
    if (!arg->status_known)
    {
        arg->status_known = TRUE;
        arg->was_rolling = arg->master->state == CMTS_ROLLING;
    }
    // If transport was rolling, stop, release notes, seek, then restart
    if (arg->master->state == CMTS_ROLLING)
        arg->master->state = CMTS_STOPPING;
    
    // wait until transport stopped
    if (arg->master->state != CMTS_STOP)
        return 0;
    
    if (arg->master->spb)
    {
        if (arg->is_ppqn)
            cbox_song_playback_seek_ppqn(arg->master->spb, arg->target_pos, FALSE);
        else
            cbox_song_playback_seek_samples(arg->master->spb, arg->target_pos);
    }
    if (arg->was_rolling)
        arg->master->state = CMTS_ROLLING;
    return 1;
}

void cbox_master_seek_ppqn(struct cbox_master *master, uint32_t pos_ppqn)
{
    static struct cbox_rt_cmd_definition cmd = { NULL, seek_transport_execute, NULL };
    struct seek_command_arg arg = { master, TRUE, pos_ppqn, FALSE, FALSE, FALSE };
    cbox_rt_execute_cmd_sync(master->engine->rt, &cmd, &arg);
}

void cbox_master_seek_samples(struct cbox_master *master, uint32_t pos_samples)
{
    static struct cbox_rt_cmd_definition cmd = { NULL, seek_transport_execute, NULL };
    struct seek_command_arg arg = { master, FALSE, pos_samples, FALSE, FALSE, FALSE };
    cbox_rt_execute_cmd_sync(master->engine->rt, &cmd, &arg);
}

void cbox_master_panic(struct cbox_master *master)
{
    cbox_master_stop(master);
    struct cbox_midi_buffer buf;
    cbox_midi_buffer_init(&buf);
    for (int ch = 0; ch < 16; ch++)
    {
        cbox_midi_buffer_write_inline(&buf, ch, 0xB0 + ch, 120, 0);
        cbox_midi_buffer_write_inline(&buf, ch, 0xB0 + ch, 123, 0);
        cbox_midi_buffer_write_inline(&buf, ch, 0xB0 + ch, 121, 0);
    }
    // Send to all outputs
    cbox_engine_send_events_to(master->engine, NULL, &buf);
}

uint32_t cbox_master_ppqn_to_samples(struct cbox_master *master, uint32_t time_ppqn)
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
    return offset + (int)(master->srate * 60.0 * time_ppqn / (tempo * master->ppqn_factor));
}

uint32_t cbox_master_samples_to_ppqn(struct cbox_master *master, uint32_t time_samples)
{
    double tempo = master->tempo;
    uint32_t offset = 0;
    if (master->spb)
    {
        int idx = cbox_song_playback_tmi_from_samples(master->spb, time_samples);
        if (idx != -1 && idx < master->spb->tempo_map_item_count)
        {
            const struct cbox_tempo_map_item *tmi = &master->spb->tempo_map_items[idx];
            tempo = tmi->tempo;
            time_samples -= tmi->time_samples;
            offset = tmi->time_ppqn;
        }
    }
    return offset + (uint32_t)(tempo * master->ppqn_factor * time_samples / (master->srate * 60.0));
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
