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

#ifndef CBOX_SAMPLER_IMPL_H
#define CBOX_SAMPLER_IMPL_H

extern void sampler_gen_reset(struct sampler_gen *v);
extern uint32_t sampler_gen_sample_playback(struct sampler_gen *v, float *leftright, uint32_t limit);
extern void sampler_program_change_byidx(struct sampler_module *m, struct sampler_channel *c, int program_idx);
extern void sampler_program_change(struct sampler_module *m, struct sampler_channel *c, int program);

static inline int sfz_note_from_string(const char *note)
{
    static const int semis[] = {9, 11, 0, 2, 4, 5, 7};
    int pos;
    int nn = tolower(note[0]);
    int nv;
    if (nn >= '0' && nn <= '9')
        return atoi(note);
    if (nn < 'a' || nn > 'g')
        return -1;
    nv = semis[nn - 'a'];

    for (pos = 1; tolower(note[pos]) == 'b' || note[pos] == '#'; pos++)
        nv += (note[pos] != '#') ? -1 : +1;

    if ((note[pos] == '-' && note[pos + 1] == '1' && note[pos + 2] == '\0') || (note[pos] >= '0' && note[pos] <= '9' && note[pos + 1] == '\0'))
    {
        return nv + 12 * (1 + atoi(note + pos));
    }

    return -1;
}

static inline gboolean atof_C_verify(const char *key, const char *value, double *result, GError **error)
{
    char *endptr = NULL;
    double res = g_ascii_strtod(value, &endptr);
    if (endptr && !*endptr && endptr != value)
    {
        *result = res;
        return TRUE;
    }
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a correct numeric value for %s", value, key);
    return FALSE;
}

#endif
