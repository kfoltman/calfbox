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

#ifndef CBOX_WAVEBANK_H
#define CBOX_WAVEBANK_H

#include <glib.h>
#include <sndfile.h>

#define CBOX_WAVEFORM_ERROR cbox_waveform_error_quark()

enum CboxWaveformError
{
    CBOX_WAVEFORM_ERROR_FAILED,
};

struct cbox_waveform
{
    int16_t *data;
    SF_INFO info;
    int refcount;
    size_t bytes;
    gchar *canonical_name;
    gchar *display_name;
};

extern void cbox_wavebank_init();
extern struct cbox_waveform *cbox_wavebank_get_waveform(const char *context_name, const char *filename, GError **error);
extern int64_t cbox_wavebank_get_bytes();
extern int64_t cbox_wavebank_get_maxbytes();
extern void cbox_wavebank_close();

extern void cbox_waveform_ref(struct cbox_waveform *waveform);
extern void cbox_waveform_unref(struct cbox_waveform *waveform);

#endif
