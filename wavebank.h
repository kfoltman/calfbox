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

#include <stdbool.h>
#include <glib.h>
#include <sndfile.h>
#include "tarfile.h"

#define MAX_INTERPOLATION_ORDER 3

#define CBOX_WAVEFORM_ERROR cbox_waveform_error_quark()

enum CboxWaveformError
{
    CBOX_WAVEFORM_ERROR_FAILED,
};

struct cbox_waveform_level
{
    int16_t *data;
    uint64_t max_rate;
};

struct cbox_waveform_generate_data
{
    float (*getfunc)(struct cbox_waveform_generate_data *generate, float v);
    void *user_data;
    int levels;
};

struct cbox_waveform
{
    int16_t *data;
    SF_INFO info;
    int id;
    int refcount;
    size_t bytes;
    size_t preloaded_frames;
    gchar *canonical_name;
    gchar *display_name;
    gboolean has_loop;
    uint32_t loop_start, loop_end;
    struct cbox_tarfile *tarfile;
    struct cbox_taritem *taritem;
    struct cbox_tarfile_sndstream sndstream;

    struct cbox_waveform_level *levels;
    int level_count;
    gboolean is_static;
    struct cbox_waveform_generate_data *generate;
};

extern struct cbox_command_target cbox_waves_cmd_target;

extern void cbox_wavebank_init(void);
extern struct cbox_waveform *cbox_wavebank_get_waveform(const char *context_name, struct cbox_tarfile *tf, const char *sample_dir, const char *filename, GError **error);
extern struct cbox_waveform *cbox_wavebank_peek_waveform_by_id(int id);
extern void cbox_wavebank_foreach(void (*cb)(void *user_data, struct cbox_waveform *waveform), void *user_data);
extern void cbox_wavebank_add_std_waveform(const char *name, float (*getfunc)(struct cbox_waveform_generate_data *generate, float v), void *user_data, int levels);
extern struct cbox_waveform *cbox_wavebank_add_mem_waveform(const char *name, void *data, uint32_t frames, int sample_rate, int channels, bool looped, uint32_t loop_start, uint32_t loop_end);
extern int cbox_wavebank_get_count(void);
extern int64_t cbox_wavebank_get_bytes(void);
extern int64_t cbox_wavebank_get_maxbytes(void);
extern void cbox_wavebank_close(void);

extern void cbox_waveform_ref(struct cbox_waveform *waveform);
extern void cbox_waveform_unref(struct cbox_waveform *waveform);


#endif
