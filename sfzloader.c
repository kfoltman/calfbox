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

#include "sampler.h"
#include "sfzparser.h"

#define DUMP_LAYER_ATTRIBS 0

struct sfz_load_state
{
    struct sampler_module *m;
    const char *filename;
    struct sampler_program *program;
    struct sampler_layer *group;
    struct sampler_layer *region;
    GError **error;
};

static void load_sfz_end_region(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    // printf("-- copy current region to the list of layers\n");
    struct sampler_layer *l = ls->region;
    sampler_layer_data_finalize(&l->data, l->parent_group ? &l->parent_group->data : NULL, ls->m);
    sampler_layer_reset_switches(l, ls->m);
    sampler_update_layer(ls->m, l);
    sampler_program_add_layer(ls->program, ls->region);

#if DUMP_LAYER_ATTRIBS
    fprintf(stdout, "<region>");
    sampler_layer_dump(ls->region, stdout);
#endif
    ls->region = NULL;
}

static void load_sfz_group(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    if (ls->group && ls->group->data.waveform)
    {
        cbox_waveform_unref(ls->group->data.waveform);
        ls->group->data.waveform = NULL;
    }
    if (ls->region)
        load_sfz_end_region(client);
    // printf("-- start group\n");
    ls->group = sampler_layer_new(ls->m, ls->program, NULL);
}

static void load_sfz_region(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    
    if (ls->region)
    {
        load_sfz_end_region(client);
    }
    else
    {
#if DUMP_LAYER_ATTRIBS
        fprintf(stdout, "<group>");
        sampler_layer_dump(&ls->group, stdout);
#endif
    }
    ls->region = sampler_layer_new(ls->m, ls->program, ls->group);
    // g_warning("-- start region");
}

static gboolean load_sfz_key_value(struct sfz_parser_client *client, const char *key, const char *value)
{
    struct sfz_load_state *ls = client->user_data;
    struct sampler_layer *l = ls->region ? ls->region : ls->group;
    int unhandled = 0;
    if (!ls->region && !ls->group)
    {
        g_warning("Cannot use parameter '%s' outside of region or group", key);
        return TRUE;
    }
    
    if (!sampler_layer_apply_param(l, key, value, ls->error))
        return FALSE;

    return TRUE;
}

gboolean sampler_module_load_program_sfz(struct sampler_module *m, struct sampler_program *prg, const char *sfz, int is_from_string, GError **error)
{
    struct sfz_load_state ls = { .group = NULL, .m = m, .filename = sfz, .region = NULL, .error = error, .program = prg };
    struct sfz_parser_client c = { .user_data = &ls, .region = load_sfz_region, .group = load_sfz_group, .key_value = load_sfz_key_value };
    g_clear_error(error);

    gboolean status = is_from_string ? load_sfz_from_string(sfz, strlen(sfz), &c, error) : load_sfz(sfz, &c, error);
    if (!status)
        return FALSE;

    if (ls.region)
        load_sfz_end_region(&c);
    if (ls.group && ls.group->data.waveform)
    {
        cbox_waveform_unref(ls.group->data.waveform);
        ls.group->data.waveform = NULL;
    }
    
    prg->layers = g_slist_reverse(prg->layers);
    prg->layers_release = g_slist_reverse(prg->layers_release);
    return TRUE;
}

