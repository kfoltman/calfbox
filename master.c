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

#include "master.h"

void cbox_master_init(struct cbox_master *master, int srate)
{
    master->song_pos_samples = 0;
    master->srate = srate;
    master->tempo = 120.0;
    master->timesig_nom = 4;
    master->timesig_denom = 4;
    master->state = CMTS_STOP;
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

