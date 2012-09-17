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

#ifndef CBOX_METER_H
#define CBOX_METER_H

#include "recsrc.h"

struct cbox_meter
{
    struct cbox_recorder recorder;
    
    float volume[2]; // lowpassed squared
    float peak[2];
    float last_peak[2];
    int srate;
    int channels;
    int smpcounter;
};

extern struct cbox_meter *cbox_meter_new(struct cbox_document *document, int srate);

#endif
