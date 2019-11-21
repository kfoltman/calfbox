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

enum sfz_load_section_type
{
    slst_normal,
    slst_control,
    slst_effect,
    slst_curve,
};

struct sfz_load_state
{
    struct sampler_module *m;
    const char *filename;
    struct sampler_program *program;
    struct sampler_layer *global, *master, *group, *region, *target;
    enum sfz_load_section_type section_type;
    GError **error;
};

static void load_sfz_end_region(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    // printf("-- copy current region to the list of layers\n");
    struct sampler_layer *l = ls->region;
    sampler_layer_data_finalize(&l->data, l->parent ? &l->parent->data : NULL, ls->program);
    sampler_layer_reset_switches(l, ls->m);
    sampler_layer_update(l);
    sampler_program_add_layer(ls->program, ls->region);

    ls->region = NULL;
}

static void end_token(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
#if DUMP_LAYER_ATTRIBS
    if (ls->group)
    {
        fprintf(stdout, "<group>");
        sampler_layer_dump(&ls->group, stdout);
    }
    if (ls->region)
    {
        fprintf(stdout, "<region>");
        sampler_layer_dump(&ls->region, stdout);
    }
#endif
    if (ls->region)
        load_sfz_end_region(client);
    ls->section_type = slst_normal;
}

static gboolean load_sfz_global(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    // printf("-- start global\n");
    ls->target = ls->global = ls->program->global;
    ls->master = ls->global->default_child;
    ls->group = ls->master->default_child;
    return TRUE;
}

static gboolean load_sfz_master(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    // printf("-- start master\n");
    ls->target = ls->master = sampler_layer_new(ls->m, ls->program, ls->program->global);
    ls->group = ls->master->default_child = sampler_layer_new(ls->m, ls->program, ls->master);
    return TRUE;
}

static gboolean load_sfz_group(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    // printf("-- start group\n");
    ls->target = ls->group = sampler_layer_new(ls->m, ls->program, ls->master);
    return TRUE;
}

static gboolean load_sfz_region(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    
    ls->target = ls->region = sampler_layer_new(ls->m, ls->program, ls->group);
    // g_warning("-- start region");
    return TRUE;
}

static gboolean load_sfz_key_value(struct sfz_parser_client *client, const char *key, const char *value)
{
    struct sfz_load_state *ls = client->user_data;
    
    if (ls->section_type == slst_curve)
    {
        g_warning("Parameter found in unsupported curve section: %s=%s", key, value);
        return TRUE;
    }
    if (ls->section_type == slst_effect)
    {
        g_warning("Parameter found in unsupported effect section: %s=%s", key, value);
        return TRUE;
    }
    if (ls->section_type == slst_control)
    {
        if (!strncmp(key, "label_cc", 8))
        {
            int ctrl = atoi(key + 8);
            sampler_program_add_controller_label(ls->program, ctrl, g_strdup(value));
        }
        else if (!strncmp(key, "set_cc", 6))
        {
            int ctrl = atoi(key + 6);
            int val = atoi(value);
            if (ctrl >= 0 && ctrl <= 119 && val >=0 && val <= 127)
                sampler_program_add_controller_init(ls->program, ctrl, val);
            else
                g_warning("Invalid CC initialisation: %s=%s", key, value);
        }
        else
            g_warning("Unrecognized SFZ key in control section: %s", key);
        return TRUE;
    }
    
    struct sampler_layer *l = ls->target;
    if (!ls->target)
    {
        g_warning("Parameter '%s' entered outside of global, master, region or group", key);
        return TRUE;
    }
    
    if (!sampler_layer_apply_param(l, key, value, ls->error))
        return FALSE;

    return TRUE;
}

static gboolean handle_token(struct sfz_parser_client *client, const char *token, GError **error)
{
    struct sfz_load_state *ls = client->user_data;
    end_token(client);

    if (!strcmp(token, "region"))
        return load_sfz_region(client);

    if (!strcmp(token, "group"))
        return load_sfz_group(client);

    if (!strcmp(token, "master"))
        return load_sfz_master(client);

    if (!strcmp(token, "global"))
        return load_sfz_global(client);

    if (!strcmp(token, "control"))
    {
        ls->section_type = slst_control;
        return TRUE;
    }

    if (!strcmp(token, "curve"))
    {
        ls->section_type = slst_curve;
        return TRUE;
    }

    if (!strcmp(token, "effect"))
    {
        ls->section_type = slst_effect;
        return TRUE;
    }

    g_set_error(error, CBOX_SFZPARSER_ERROR, CBOX_SFZ_PARSER_ERROR_INVALID_HEADER, "Unexpected header <%s>", token);
    return FALSE;
}

gboolean sampler_module_load_program_sfz(struct sampler_module *m, struct sampler_program *prg, const char *sfz, int is_from_string, GError **error)
{
    struct sfz_load_state ls = { .global = prg->global, .master = prg->global->default_child, .group = prg->global->default_child->default_child, .target = NULL, .m = m, .filename = sfz, .region = NULL, .error = error, .program = prg, .section_type = slst_normal };
    struct sfz_parser_client c = { .user_data = &ls, .token = handle_token, .key_value = load_sfz_key_value };
    g_clear_error(error);

    gboolean status;
    if (is_from_string)
        status = load_sfz_from_string(sfz, strlen(sfz), &c, error);
    else
        status = load_sfz(sfz, prg->tarfile, &c, error);
    if (!status)
    {
        if (ls.region)
            CBOX_DELETE(ls.region);
        return FALSE;
    }

    end_token(&c);
    
    prg->all_layers = g_slist_reverse(prg->all_layers);
    sampler_program_update_layers(prg);
    return TRUE;
}

