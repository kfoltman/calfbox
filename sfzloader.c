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

struct sfz_load_state
{
    struct sampler_module *m;
    const char *filename;
    int in_group;
    struct sampler_layer group;
    struct sampler_layer *region;
    GList *layers;
    GError **error;
};

static void load_sfz_end_region(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    // printf("-- copy current region to the list of layers\n");
    struct sampler_layer *l = ls->region;
    cbox_envelope_init_adsr(&l->amp_env_shape, &l->amp_adsr, ls->m->srate / CBOX_BLOCK_SIZE);
    cbox_envelope_init_adsr(&l->filter_env_shape, &l->filter_adsr,  ls->m->srate / CBOX_BLOCK_SIZE);
    ls->layers = g_list_append(ls->layers, ls->region);
    ls->region = NULL;
}

static void load_sfz_group(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    if (ls->region)
        load_sfz_end_region(client);
    // printf("-- start group\n");
    sampler_layer_init(&ls->group);
    ls->in_group = 1;
}

static void load_sfz_region(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    
    if (ls->region)
    {
        load_sfz_end_region(client);
    }
    ls->region = malloc(sizeof(struct sampler_layer));
    if (ls->in_group)
    {
        memcpy(ls->region, &ls->group, sizeof(struct sampler_layer));
    }
    else
        sampler_layer_init(ls->region);
    // g_warning("-- start region");
}

static gboolean load_sfz_key_value(struct sfz_parser_client *client, const char *key, const char *value)
{
    struct sfz_load_state *ls = client->user_data;
    struct sampler_layer *l = ls->region ? ls->region : &ls->group;
    if (!ls->region && !ls->in_group)
    {
        g_warning("Cannot use parameter '%s' outside of region or group", key);
        return TRUE;
    }
    
    if (!strcmp(key, "sample"))
    {
        if (!ls->region)
        {
            g_warning("Cannot specify samples for entire groups - ignored");
            return TRUE;
        }
        struct sampler_waveform *wf = sampler_waveform_new_from_file(ls->filename, value, ls->error);
        if (!wf)
            return FALSE;
        sampler_layer_set_waveform(ls->region, wf);
    }
    else if (!strcmp(key, "lokey"))
        l->min_note = note_from_string(value);
    else if (!strcmp(key, "hikey"))
        l->max_note = note_from_string(value);
    else
        g_warning("Unhandled sfz key: %s", key);
    return TRUE;
}

gboolean sampler_module_load_program_sfz(struct sampler_module *m, struct sampler_program *prg, const char *sfz, GError **error)
{
    struct sfz_load_state ls = { .in_group = 0, .m = m, .filename = sfz, .layers = NULL, .region = NULL, .error = error };
    struct sfz_parser_client c = { .user_data = &ls, .region = load_sfz_region, .group = load_sfz_group, .key_value = load_sfz_key_value };
    g_clear_error(error);

    if (!load_sfz(sfz, &c, error))
    {
        return FALSE;
    }
    if (ls.region)
        load_sfz_end_region(&c);
    
    prg->layer_count = g_list_length(ls.layers);
    prg->layers = malloc(prg->layer_count * sizeof(struct sampler_layer *));
    GList *p = ls.layers;
    for(int i = 0; p; i++)
    {
        prg->layers[i] = p->data;
        p = g_list_next(p);
    }
    return TRUE;
}

