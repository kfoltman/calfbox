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

struct wave_bank
{
    int64_t bytes, maxbytes, serial_no;
    GHashTable *waveforms_by_name, *waveforms_by_id;
};

static struct wave_bank bank;

GQuark cbox_waveform_error_quark()
{
    return g_quark_from_string("cbox-waveform-error-quark");
}

void cbox_wavebank_init()
{
    bank.bytes = 0;
    bank.maxbytes = 0;
    bank.serial_no = 0;
    bank.waveforms_by_name = g_hash_table_new(g_str_hash, g_str_equal);
    bank.waveforms_by_id = g_hash_table_new(g_int_hash, g_int_equal);
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
    
    char *canonical = realpath(filename, NULL);
    if (!canonical)
    {
        g_set_error(error, CBOX_WAVEFORM_ERROR, CBOX_WAVEFORM_ERROR_FAILED, "%s: cannot find a real path for '%s'", context_name, filename);
        return NULL;
    }
    gpointer value = g_hash_table_lookup(bank.waveforms_by_name, canonical);
    if (value)
    {
        free(canonical);
        
        struct cbox_waveform *waveform = value;
        cbox_waveform_ref(waveform);
        return waveform;
    }
    
    struct cbox_waveform *waveform = malloc(sizeof(struct cbox_waveform));
    memset(&waveform->info, 0, sizeof(waveform->info));
    SNDFILE *sndfile = sf_open(filename, SFM_READ, &waveform->info);
    if (!sndfile)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: cannot open '%s'", context_name, filename);
        free(canonical);
        return NULL;
    }
    if (waveform->info.channels != 1 && waveform->info.channels != 2)
    {
        g_set_error(error, CBOX_WAVEFORM_ERROR, CBOX_WAVEFORM_ERROR_FAILED, 
            "%s: cannot open file '%s': unsupported channel count %d", context_name, filename, (int)waveform->info.channels);
        sf_close(sndfile);
        free(canonical);
        return NULL;
    }
    waveform->id = ++bank.serial_no;
    waveform->bytes = waveform->info.channels * 2 * (waveform->info.frames + 1);
    waveform->data = malloc(waveform->bytes);
    waveform->refcount = 1;
    waveform->canonical_name = canonical;
    waveform->display_name = g_filename_display_name(canonical);
    nshorts = waveform->info.channels * (waveform->info.frames + 1);
    for (i = 0; i < nshorts; i++)
        waveform->data[i] = 0;
    sf_readf_short(sndfile, waveform->data, waveform->info.frames);
    sf_close(sndfile);
    bank.bytes += waveform->bytes;
    if (bank.bytes > bank.maxbytes)
        bank.maxbytes = bank.bytes;
    g_hash_table_insert(bank.waveforms_by_name, waveform->canonical_name, waveform);
    g_hash_table_insert(bank.waveforms_by_id, &waveform->id, waveform);
    
    return waveform;
}

int64_t cbox_wavebank_get_bytes()
{
    return bank.bytes;
}

int64_t cbox_wavebank_get_maxbytes()
{
    return bank.maxbytes;
}

int cbox_wavebank_get_count()
{
    return g_hash_table_size(bank.waveforms_by_id);
}

struct cbox_waveform *cbox_wavebank_peek_waveform_by_id(int id)
{
    return g_hash_table_lookup(bank.waveforms_by_id, &id);
}

void cbox_wavebank_foreach(void (*cb)(void *, struct cbox_waveform *), void *user_data)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, bank.waveforms_by_id);
    while (g_hash_table_iter_next (&iter, &key, &value)) 
    {
        (*cb)(user_data, value);
    }    
}

void cbox_wavebank_close()
{
    if (bank.bytes > 0)
        g_warning("Warning: %lld bytes in unfreed samples", (long long int)bank.bytes);
    g_hash_table_destroy(bank.waveforms_by_id);
    g_hash_table_destroy(bank.waveforms_by_name);
    bank.waveforms_by_id = NULL;
    bank.waveforms_by_name = NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void cbox_waveform_ref(struct cbox_waveform *waveform)
{
    ++waveform->refcount;
}

void cbox_waveform_unref(struct cbox_waveform *waveform)
{
    if (--waveform->refcount > 0)
        return;
    
    g_hash_table_remove(bank.waveforms_by_name, waveform->canonical_name);
    g_hash_table_remove(bank.waveforms_by_id, &waveform->id);
    bank.bytes -= waveform->bytes;

    g_free(waveform->display_name);
    free(waveform->canonical_name);
    free(waveform->data);
    free(waveform);
    
}

