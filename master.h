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

#include <stdint.h>

#define PPQN 24


enum cbox_master_transport_state
{
    CMTS_STOP,
    CMTS_ROLLING,
};

struct cbox_master
{
    uint32_t song_pos_samples;
    int srate;
    float tempo;
    int timesig_nom;
    int timesig_denom; // must be 4 for now
    enum cbox_master_transport_state state;
};

struct cbox_bbt
{
    int bar;
    int beat;
    int tick;
};

extern void cbox_master_init(struct cbox_master *master, int srate);
extern void cbox_master_to_bbt(const struct cbox_master *master, struct cbox_bbt *bbt);
extern uint32_t cbox_master_song_pos_from_bbt(struct cbox_master *master, const struct cbox_bbt *bbt);
extern void cbox_master_play(struct cbox_master *master);
extern void cbox_master_stop(struct cbox_master *master);

