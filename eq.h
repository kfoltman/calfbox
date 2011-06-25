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

#include <glib.h>

struct eq_band
{
    gboolean active;
    float center;
    float q;
    float gain;
};

extern float cbox_eq_get_band_param(const char *cfg_section, int band, const char *param, float defvalue);
extern float cbox_eq_get_band_param_db(const char *cfg_section, int band, const char *param, float defvalue);
extern void cbox_eq_reset_bands(struct cbox_biquadf_state (*state)[2], int bands);
