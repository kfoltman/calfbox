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

#include "wavebank.h"
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

static int64_t wavebank_bytes = 0;
static int64_t wavebank_maxbytes = 0;

GQuark cbox_waveform_error_quark()
{
    return g_quark_from_string("cbox-waveform-error-quark");
}

void cbox_wavebank_init()
{
    wavebank_bytes = 0;
    wavebank_maxbytes = 0;
}

struct cbox_waveform *cbox_wavebank_get_waveform(const char *context_name, const char *filename, GError **error)
{
    int i;
    int nshorts;
    
    if (!filename)
    {
        g_set_error(error, CBOX_WAVEFORM_ERROR, CBOX_WAVEFORM_ERROR_FAILED, "%s: no filename specified", context_name);
        return NULL;
    }
    struct cbox_waveform *waveform = malloc(sizeof(struct cbox_waveform));
    memset(&waveform->info, 0, sizeof(waveform->info));
    SNDFILE *sndfile = sf_open(filename, SFM_READ, &waveform->info);
    if (!sndfile)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: cannot open '%s'", context_name, filename);
        return NULL;
    }
    if (waveform->info.channels != 1 && waveform->info.channels != 2)
    {
        g_set_error(error, CBOX_WAVEFORM_ERROR, CBOX_WAVEFORM_ERROR_FAILED, 
            "%s: cannot open file '%s': unsupported channel count %d", context_name, filename, (int)waveform->info.channels);
        sf_close(sndfile);
        return NULL;
    }
    waveform->bytes = waveform->info.channels * 2 * (waveform->info.frames + 1);
    waveform->data = malloc(waveform->bytes);
    waveform->refcount = 1;
    nshorts = waveform->info.channels * (waveform->info.frames + 1);
    for (i = 0; i < nshorts; i++)
        waveform->data[i] = 0;
    sf_readf_short(sndfile, waveform->data, waveform->info.frames);
    sf_close(sndfile);
    wavebank_bytes += waveform->bytes;
    if (wavebank_bytes > wavebank_maxbytes)
        wavebank_maxbytes = wavebank_bytes;
    
    return waveform;
}

int64_t cbox_wavebank_get_bytes()
{
    return wavebank_bytes;
}

int64_t cbox_wavebank_get_maxbytes()
{
    return wavebank_maxbytes;
}

void cbox_wavebank_close()
{
    if (wavebank_bytes > 0)
        g_warning("Warning: %lld bytes in unfreed samples", (long long int)wavebank_bytes);
}

void cbox_waveform_ref(struct cbox_waveform *waveform)
{
    ++waveform->refcount;
}

void cbox_waveform_unref(struct cbox_waveform *waveform)
{
    if (--waveform->refcount > 0)
        return;
    
    wavebank_bytes -= waveform->bytes;

    free(waveform->data);
    free(waveform);
    
}

