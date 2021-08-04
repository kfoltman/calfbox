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

#include "cmd.h"
#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "errors.h"
#include "tarfile.h"
#include "wavebank.h"
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STD_WAVEFORM_FRAMES 1024
#define STD_WAVEFORM_BITS 10

///////////////////////////////////////////////////////////////////////////////

// Sine table
static complex float euler_table[STD_WAVEFORM_FRAMES];

// Bit reversal table
static int map_table[STD_WAVEFORM_FRAMES];

// Initialise tables using for FFT
static void init_tables(void)
{
    int rev = 1 << (STD_WAVEFORM_BITS - 1);
    for (int i = 0; i < STD_WAVEFORM_FRAMES; i++)
    {
        euler_table[i] = cos(i * 2 * M_PI / STD_WAVEFORM_FRAMES) + I * sin(i * 2 * M_PI / STD_WAVEFORM_FRAMES);
        int ni = 0;
        for (int j = 0; j < STD_WAVEFORM_BITS; j++)
        {
            if (i & (rev >> j))
                ni = ni | (1 << j);
        }
        map_table[i] = ni;
    }
}

// Trivial implementation of Cooley-Tukey, only works for even values of ANALYSIS_BUFFER_BITS
static void my_fft_main(complex float output[STD_WAVEFORM_FRAMES])
{
    complex float temp[STD_WAVEFORM_FRAMES];

    for (int i = 0; i < STD_WAVEFORM_BITS; i++)
    {
        complex float *src = (i & 1) ? temp : output;
        complex float *dst = (i & 1) ? output : temp;
        int invi = STD_WAVEFORM_BITS - i - 1;
        int disp = 1 << i;
        int mask = disp - 1;

        for (int j = 0; j < STD_WAVEFORM_FRAMES / 2; j++)
        {
            int jj1 = (j & mask) + ((j & ~mask) << 1); // insert 0 at i'th bit to get the left arm of the butterfly
            int jj2 = jj1 + disp;                      // insert 1 at i'th bit to get the right arm
            assert((jj1 + disp) == (jj1 | disp));

            // e^iw
            complex float eiw1 = euler_table[(jj1 << invi) & (STD_WAVEFORM_FRAMES - 1)];
            complex float eiw2 = euler_table[(jj2 << invi) & (STD_WAVEFORM_FRAMES - 1)];

            // printf("%d -> %d, %d\n", j, jj, jj + disp);
            butterfly(&dst[jj1], &dst[jj2], src[jj1], src[jj2], eiw1, eiw2);
        }
    }
}

static void my_fft_r2c(complex float output[STD_WAVEFORM_FRAMES], int16_t input[STD_WAVEFORM_FRAMES])
{
    assert(!(STD_WAVEFORM_BITS&1));
    // Copy + bit reversal addressing
    for (int i = 0; i < STD_WAVEFORM_FRAMES; i++)
        output[i] = input[map_table[i]] * (1.0 / STD_WAVEFORM_FRAMES);

    my_fft_main(output);

}

static void my_ifft_c2r(int16_t output[STD_WAVEFORM_FRAMES], complex float input[STD_WAVEFORM_FRAMES])
{
    complex float temp2[STD_WAVEFORM_FRAMES];
    for (int i = 0; i < STD_WAVEFORM_FRAMES; i++)
        temp2[i] = input[map_table[i]];
    assert(!(STD_WAVEFORM_BITS&1));

    my_fft_main(temp2);
    // Copy + bit reversal addressing
    float maxv = 0;
    for (int i = 0; i < STD_WAVEFORM_FRAMES; i++)
    {
        float value = creal(temp2[i]);
        if (value < -32768) value = -32768;
        if (value > 32767) value = 32767;
        if (fabs(value) > maxv)
            maxv = fabs(value);
        output[i] = (int16_t)value;
    }
}

struct wave_bank
{
    int64_t bytes, maxbytes, serial_no;
    GHashTable *waveforms_by_name, *waveforms_by_id;
    GSList *std_waveforms;
    uint32_t streaming_prefetch_size;
};

static struct wave_bank bank;

GQuark cbox_waveform_error_quark(void)
{
    return g_quark_from_string("cbox-waveform-error-quark");
}

float func_sine(float v, void *user_data)
{
    return sin(2 * M_PI * v);
}

float func_silence(float v, void *user_data)
{
    return 0.f;
}

float func_sqr(float v, void *user_data)
{
    return v < 0.5 ? -1 : 1;
}

float func_saw(float v, void *user_data)
{
    return 2 * v - 1;
}

float func_tri(float v, void *user_data)
{
    if (v <= 0.25f)
        return v * 4;
    if (v <= 0.75f)
        return 1 - (v - 0.25f) * 4;
    return -1 + 4 * (v - 0.75f);
}

void cbox_waveform_generate_levels(struct cbox_waveform *waveform, int levels, double ratio)
{
    complex float output[STD_WAVEFORM_FRAMES], bandlimited[STD_WAVEFORM_FRAMES];
    my_fft_r2c(output, waveform->data);
    int N = STD_WAVEFORM_FRAMES;

    waveform->levels = calloc(levels, sizeof(struct cbox_waveform_level));
    double rate = 65536.0 * 65536.0; // / waveform->info.frames;
    double orig_rate = 65536.0 * 65536.0; // / waveform->info.frames;
    for (int i = 0; i < levels; i++)
    {
        int harmonics = N / 2 / (rate / orig_rate);
        bandlimited[0] = 0;

        if (harmonics > 0)
        {
            for (int j = 1; j <= harmonics; j++)
            {
                bandlimited[j] = output[j];
                bandlimited[N - j] = output[N - j];
            }
            for (int j = harmonics; j <= N / 2; j++)
                bandlimited[j] = bandlimited [N - j] = 0;
        }

        waveform->levels[i].data = calloc(N + MAX_INTERPOLATION_ORDER, sizeof(int16_t));
        my_ifft_c2r(waveform->levels[i].data, bandlimited);
        memcpy(waveform->levels[i].data + N, waveform->levels[i].data, MAX_INTERPOLATION_ORDER * sizeof(int16_t));
        waveform->levels[i].max_rate = (uint64_t)(rate);
        rate *= ratio;
    }
    waveform->level_count = levels;
}

void cbox_wavebank_add_std_waveform(const char *name, float (*getfunc)(float v, void *user_data), void *user_data, int levels)
{
    int nsize = STD_WAVEFORM_FRAMES;
    int16_t *wave = calloc(nsize, sizeof(int16_t));
    for (int i = 0; i < nsize; i++)
    {
        float v = getfunc(i * 1.0 / nsize, user_data);
        if (fabs(v) > 1)
            v = (v < 0) ? -1 : 1;
        // cannot use full scale here, because bandlimiting will introduce
        // some degree of overshoot
        wave[i] = (int16_t)(25000 * v);
    }
    struct cbox_waveform *waveform = calloc(1, sizeof(struct cbox_waveform));
    waveform->data = wave;
    waveform->info.channels = 1;
    waveform->preloaded_frames = waveform->info.frames = nsize;
    waveform->info.samplerate = (int)(nsize * 261.6255);
    waveform->id = ++bank.serial_no;
    waveform->bytes = waveform->info.channels * 2 * (waveform->info.frames + 1);
    waveform->refcount = 1;
    waveform->canonical_name = g_strdup(name);
    waveform->display_name = g_strdup(name);
    waveform->has_loop = TRUE;
    waveform->loop_start = 0;
    waveform->loop_end = nsize;
    waveform->levels = NULL;
    waveform->level_count = 0;

    if (levels)
        cbox_waveform_generate_levels(waveform, levels, 2);

    g_hash_table_insert(bank.waveforms_by_name, waveform->canonical_name, waveform);
    g_hash_table_insert(bank.waveforms_by_id, &waveform->id, waveform);
    bank.std_waveforms = g_slist_prepend(bank.std_waveforms, waveform);
    // These waveforms are not included in the bank size, I don't think it has
    // much value for the user.
}

void cbox_wavebank_init()
{
    init_tables();

    bank.bytes = 0;
    bank.maxbytes = 0;
    bank.serial_no = 0;
    bank.waveforms_by_name = g_hash_table_new(g_str_hash, g_str_equal);
    bank.waveforms_by_id = g_hash_table_new(g_int_hash, g_int_equal);
    bank.std_waveforms = NULL;
    bank.streaming_prefetch_size = cbox_config_get_int("streaming", "prefetch_size", 65536);

    cbox_wavebank_add_std_waveform("*sine", func_sine, NULL, 0);
    // XXXKF this should not be a real waveform
    cbox_wavebank_add_std_waveform("*silence", func_silence, NULL, 0);
    cbox_wavebank_add_std_waveform("*saw", func_saw, NULL, 11);
    cbox_wavebank_add_std_waveform("*sqr", func_sqr, NULL, 11);
    cbox_wavebank_add_std_waveform("*tri", func_tri, NULL, 11);
}

struct cbox_waveform *cbox_wavebank_get_waveform(const char *context_name, struct cbox_tarfile *tarfile, const char *sample_dir, const char *filename, GError **error)
{
    if (!filename)
    {
        g_set_error(error, CBOX_WAVEFORM_ERROR, CBOX_WAVEFORM_ERROR_FAILED, "%s: no filename specified", context_name);
        return NULL;
    }

    // Built in waveforms don't go through path canonicalization
    if (filename[0] == '*')
    {
        gpointer value = g_hash_table_lookup(bank.waveforms_by_name, filename);
        if (value)
        {
            struct cbox_waveform *waveform = value;
            cbox_waveform_ref(waveform);
            return waveform;
        }
    }

    gchar *value_copy = g_strdup(filename);
    for (int i = 0; value_copy[i]; i++)
    {
        if (value_copy[i] == '\\')
            value_copy[i] = '/';
    }
    gchar *pathname = value_copy[0] == '/' ? g_strdup(value_copy) : g_build_filename(sample_dir, value_copy, NULL);
    g_free(value_copy);

    char *canonical = NULL;
    if (tarfile)
        canonical = g_strdup_printf("sbtar:%s;%s", tarfile->file_pathname, pathname);
    else
    {
        // make sure canonical is always allocated on the same (glib) heap
        char *p = realpath(pathname, NULL);
        if (p)
        {
            canonical = g_strdup(p);
            free(p);
        }
    }
    if (!canonical)
    {
        g_set_error(error, CBOX_WAVEFORM_ERROR, CBOX_WAVEFORM_ERROR_FAILED, "%s: cannot find a real path for '%s': %s", context_name, pathname, strerror(errno));
        g_free(pathname);
        return NULL;
    }
    gpointer value = g_hash_table_lookup(bank.waveforms_by_name, canonical);
    if (value)
    {
        g_free(pathname);
        g_free(canonical);

        struct cbox_waveform *waveform = value;
        cbox_waveform_ref(waveform);
        return waveform;
    }

    struct cbox_waveform *waveform = calloc(1, sizeof(struct cbox_waveform));
    SNDFILE *sndfile = NULL;
    struct cbox_taritem *taritem = NULL;
    if (tarfile)
    {
        if (strcmp(sample_dir, ".") == 0)
        {
            //Potential path lookup problem:

            //This is a sample without sfz default_path opcode inside a tar.
            //We need to set the sample dir to the position of the .sfz file within the .tar
            //because we also assume that the sample paths in regions are relative to the .sfz path.

            //If the sfz is in the tar root this is a redundant action, but if the sfz is itself
            //in a subdirectoy we need to adjust the path now.
            // XXXNH sample_dir will not be updated in the struct itself and thus reported as "." in python etc.

            //context_name is the sfz file, filename the sample file without leading ./  and sample_dir just a dot.

            gchar *sfz_dir = g_path_get_dirname(context_name); //take the path of the sfz file...
            pathname = g_build_filename(sfz_dir, filename, NULL); //... and prefix the sample filename with it.
            g_free(sfz_dir);
        }
        taritem = cbox_tarfile_get_item_by_name(tarfile, pathname, TRUE);
        if (taritem)
            sndfile = cbox_tarfile_opensndfile(tarfile, taritem, &waveform->sndstream, &waveform->info);
    }
    else
        sndfile = sf_open(pathname, SFM_READ, &waveform->info);
    if (!sndfile)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: cannot open '%s'", context_name, pathname);
        g_free(pathname);
        g_free(canonical);
        free(waveform);
        return NULL;
    }

    SF_INSTRUMENT instrument;
    uint32_t nshorts;
    if (waveform->info.channels != 1 && waveform->info.channels != 2)
    {
        g_set_error(error, CBOX_WAVEFORM_ERROR, CBOX_WAVEFORM_ERROR_FAILED,
            "%s: cannot open file '%s': unsupported channel count %d", context_name, pathname, (int)waveform->info.channels);
        sf_close(sndfile);
        free(canonical);
        g_free(pathname);
        return NULL;
    }
    g_free(pathname);
    uint32_t preloaded_frames = waveform->info.frames;
    // If sample is larger than 2x prefetch buffer size, then load only
    // a prefetch buffer worth of data, and stream the rest.
    if (preloaded_frames > 2 * bank.streaming_prefetch_size)
        preloaded_frames = bank.streaming_prefetch_size;
    waveform->id = ++bank.serial_no;
    waveform->bytes = waveform->info.channels * 2 * preloaded_frames;
    waveform->data = malloc(waveform->bytes);
    waveform->refcount = 1;
    waveform->canonical_name = canonical;
    waveform->display_name = g_filename_display_name(canonical);
    waveform->has_loop = FALSE;
    waveform->levels = NULL;
    waveform->level_count = 0;
    waveform->preloaded_frames = preloaded_frames;
    waveform->tarfile = tarfile;
    waveform->taritem = taritem;

    if (sf_command(sndfile, SFC_GET_INSTRUMENT, &instrument, sizeof(SF_INSTRUMENT)))
    {
        for (int i = 0; i < instrument.loop_count; i++)
        {
            if (instrument.loops[i].mode == SF_LOOP_FORWARD)
            {
                waveform->loop_start = instrument.loops[i].start;
                waveform->loop_end = instrument.loops[i].end;
                waveform->has_loop = TRUE;
                break;
            }
        }
    }

    nshorts = waveform->info.channels * preloaded_frames;
    for (uint32_t i = 0; i < nshorts; i++)
        waveform->data[i] = 0;
    sf_readf_short(sndfile, waveform->data, preloaded_frames);
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
    while(bank.std_waveforms)
    {
        cbox_waveform_unref((struct cbox_waveform *)bank.std_waveforms->data);
        bank.std_waveforms = g_slist_delete_link(bank.std_waveforms, bank.std_waveforms);
    }
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
    g_free(waveform->canonical_name);
    for (int i = 0; i < waveform->level_count; i++)
        free(waveform->levels[i].data);
    free(waveform->levels);
    free(waveform->data);
    free(waveform);

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct waves_foreach_data
{
    struct cbox_command_target *fb;
    GError **error;
    gboolean success;
};

void wave_list_cb(void *user_data, struct cbox_waveform *waveform)
{
    struct waves_foreach_data *wfd = user_data;

    wfd->success = wfd->success && cbox_execute_on(wfd->fb, NULL, "/waveform", "i", wfd->error, (int)waveform->id);
}

static gboolean waves_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        // XXXKF this only supports 4GB - not a big deal for now yet?
        return cbox_execute_on(fb, NULL, "/bytes", "i", error, (int)cbox_wavebank_get_bytes()) &&
            cbox_execute_on(fb, NULL, "/max_bytes", "i", error, (int)cbox_wavebank_get_maxbytes()) &&
            cbox_execute_on(fb, NULL, "/count", "i", error, (int)cbox_wavebank_get_count())
            ;
    }
    else if (!strcmp(cmd->command, "/list") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        struct waves_foreach_data wfd = { fb, error, TRUE };
        cbox_wavebank_foreach(wave_list_cb, &wfd);
        return wfd.success;
    }
    else if (!strcmp(cmd->command, "/info") && !strcmp(cmd->arg_types, "i"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        int id = CBOX_ARG_I(cmd, 0);
        struct cbox_waveform *waveform = cbox_wavebank_peek_waveform_by_id(id);
        if (waveform == NULL)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Waveform %d not found", id);
            return FALSE;
        }
        assert(id == waveform->id);
        if (!cbox_execute_on(fb, NULL, "/filename", "s", error, waveform->canonical_name)) // XXXKF convert to utf8
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/name", "s", error, waveform->display_name))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/bytes", "i", error, (int)waveform->bytes))
            return FALSE;
        if (waveform->has_loop && !cbox_execute_on(fb, NULL, "/loop", "ii", error, (int)waveform->loop_start, (int)waveform->loop_end))
            return FALSE;
        return TRUE;
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

struct cbox_command_target cbox_waves_cmd_target =
{
    .process_cmd = waves_process_cmd,
    .user_data = NULL
};
