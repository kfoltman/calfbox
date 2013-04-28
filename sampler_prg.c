/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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

#include "rt.h"
#include "sampler.h"
#include "sampler_prg.h"
#include "sfzloader.h"

#include <assert.h>

CBOX_CLASS_DEFINITION_ROOT(sampler_program)

GSList *sampler_program_get_next_layer(struct sampler_program *prg, struct sampler_channel *c, GSList *next_layer, int note, int vel, float random, gboolean is_first)
{
    int ch = (c - c->module->channels) + 1;
    for(;next_layer;next_layer = g_slist_next(next_layer))
    {
        struct sampler_layer *lr = next_layer->data;
        struct sampler_layer_data *l = lr->runtime;
        if (!l->eff_waveform)
            continue;
        if ((l->trigger == stm_first && !is_first) ||
            (l->trigger == stm_legato && is_first))
            continue;
        if (l->sw_last != -1)
        {
            if (note >= l->sw_lokey && note <= l->sw_hikey)
                lr->last_key = note;
        }
        if (note >= l->lokey && note <= l->hikey && vel >= l->lovel && vel <= l->hivel && ch >= l->lochan && ch <= l->hichan && random >= l->lorand && random < l->hirand && 
            (l->cc_number == -1 || (c->cc[l->cc_number] >= l->locc && c->cc[l->cc_number] <= l->hicc)))
        {
            if (!l->eff_use_keyswitch || 
                ((l->sw_last == -1 || l->sw_last == lr->last_key) &&
                 (l->sw_down == -1 || (c->switchmask[l->sw_down >> 5] & (1 << (l->sw_down & 31)))) &&
                 (l->sw_up == -1 || !(c->switchmask[l->sw_up >> 5] & (1 << (l->sw_up & 31)))) &&
                 (l->sw_previous == -1 || l->sw_previous == c->previous_note)))
            {
                gboolean play = lr->current_seq_position == 1;
                lr->current_seq_position++;
                if (lr->current_seq_position >= l->seq_length)
                    lr->current_seq_position = 1;
                if (play)
                    return next_layer;
            }
        }
    }
    return NULL;
}

static gboolean return_layers(GSList *layers, const char *keyword, struct cbox_command_target *fb, GError **error)
{
    for (GSList *p = layers; p; p = g_slist_next(p))
    {
        if (!cbox_execute_on(fb, NULL, keyword, "o", error, p->data))
            return FALSE;
    }
    return TRUE;
}

static gboolean sampler_program_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct sampler_program *program = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!((!program->name || cbox_execute_on(fb, NULL, "/name", "s", error, program->name)) &&
            cbox_execute_on(fb, NULL, "/sample_dir", "s", error, program->sample_dir) &&
            cbox_execute_on(fb, NULL, "/source_file", "s", error, program->source_file) &&
            cbox_execute_on(fb, NULL, "/program_no", "i", error, program->prog_no) &&
            cbox_execute_on(fb, NULL, "/in_use", "i", error, program->in_use) &&
            CBOX_OBJECT_DEFAULT_STATUS(program, fb, error)))
            return FALSE;
        return TRUE;
    }
    if (!strcmp(cmd->command, "/regions") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return return_layers(program->all_layers, "/region", fb, error);
    }
    if (!strcmp(cmd->command, "/groups") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        if (!cbox_execute_on(fb, NULL, "/default_group", "o", error, program->default_group))
            return FALSE;
        return return_layers(program->groups, "/group", fb, error);
    }
    if (!strcmp(cmd->command, "/control_inits") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (GSList *p = program->ctrl_init_list; p; p = p->next)
        {
            const struct sampler_ctrlinit *cin = (const struct sampler_ctrlinit *)&p->data;
            if (!cbox_execute_on(fb, NULL, "/control_init", "ii", error, (int)cin->controller, (int)cin->value))
                return FALSE;
        }
        return TRUE;
    }
    if (!strcmp(cmd->command, "/add_control_init") && !strcmp(cmd->arg_types, "ii"))
    {
        sampler_program_add_controller_init(program, CBOX_ARG_I(cmd, 0), CBOX_ARG_I(cmd, 1));
        return TRUE;
    }
    if (!strcmp(cmd->command, "/delete_control_init") && !strcmp(cmd->arg_types, "ii"))
    {
        sampler_program_remove_controller_init(program, CBOX_ARG_I(cmd, 0), CBOX_ARG_I(cmd, 1));
        return TRUE;
    }
    if (!strcmp(cmd->command, "/new_group") && !strcmp(cmd->arg_types, ""))
    {
        struct sampler_layer *l = sampler_layer_new(program->module, program, NULL);
        sampler_program_add_group(program, l);
        return cbox_execute_on(fb, NULL, "/uuid", "o", error, l);
    }
    return cbox_object_default_process_cmd(ct, fb, cmd, error);
    
}

struct sampler_program *sampler_program_new(struct sampler_module *m, int prog_no, const char *name, const char *sample_dir)
{
    struct cbox_document *doc = CBOX_GET_DOCUMENT(&m->module);
    struct sampler_program *prg = malloc(sizeof(struct sampler_program));
    memset(prg, 0, sizeof(*prg));
    CBOX_OBJECT_HEADER_INIT(prg, sampler_program, doc);
    cbox_command_target_init(&prg->cmd_target, sampler_program_process_cmd, prg);

    prg->module = m;
    prg->prog_no = prog_no;
    prg->name = g_strdup(name);
    prg->sample_dir = g_strdup(sample_dir);
    prg->source_file = NULL;
    prg->all_layers = NULL;
    prg->rll = NULL;
    prg->groups = NULL;
    prg->ctrl_init_list = NULL;
    prg->default_group = sampler_layer_new(m, prg, NULL);
    prg->deleting = FALSE;
    prg->in_use = 0;
    CBOX_OBJECT_REGISTER(prg);
    return prg;
}

struct sampler_program *sampler_program_new_from_cfg(struct sampler_module *m, const char *cfg_section, const char *name, int pgm_id, GError **error)
{
    int i;
    
    char *name2 = NULL, *sfz_path = NULL, *spath = NULL;
    const char *sfz = NULL;
    
    g_clear_error(error);
    if (!strncmp(cfg_section, "spgm:!", 6))
    {
        sfz = cfg_section + 6;
        name2 = strrchr(name, '/');
        if (name2)
            name2++;
    }
    else
    {    
        if (!sfz && !cbox_config_has_section(cfg_section))
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot load sampler program '%s' from section '%s': section not found", name, cfg_section);
            return FALSE;
        }
        name2 = cbox_config_get_string(cfg_section, "name");

        sfz_path = cbox_config_get_string(cfg_section, "sfz_path");
        spath = cbox_config_get_string(cfg_section, "sample_path");
        sfz = cbox_config_get_string(cfg_section, "sfz");
    }
    
    if (sfz && !sfz_path && !spath)
    {
        char *lastslash = strrchr(sfz, '/');
        if (lastslash && !sfz_path && !spath)
        {
            char *tmp = g_strndup(sfz, lastslash - sfz);
            sfz_path = cbox_config_permify(tmp);
            g_free(tmp);
            sfz = lastslash + 1;
        }
    }

    struct sampler_program *prg = sampler_program_new(
        m,
        pgm_id != -1 ? pgm_id : cbox_config_get_int(cfg_section, "program", 0),
        name2 ? name2 : name,
        spath ? spath : (sfz_path ? sfz_path : "")
    );
    
    if (sfz)
    {
        if (sfz_path)
            prg->source_file = g_build_filename(sfz_path, sfz, NULL);
        else
        {
            prg->source_file = g_strdup(sfz);
        }

        if (sampler_module_load_program_sfz(m, prg, prg->source_file, FALSE, error))
            return prg;
        CBOX_DELETE(prg);
        return NULL;
    }
    
    for (i = 0; ; i++)
    {
        char *where = NULL;
        gchar *s = g_strdup_printf("layer%d", 1 + i);
        const char *layer_section = cbox_config_get_string(cfg_section, s);
        g_free(s);
        if (!layer_section)
            break;
        where = g_strdup_printf("slayer:%s", layer_section);
        
        prg->source_file = g_strdup_printf("config:%s", cfg_section);
        struct sampler_layer *l = sampler_layer_new_from_section(m, prg, where);
        if (!l)
            g_warning("Sample layer '%s' cannot be created - skipping", layer_section);
        else 
        {
            sampler_layer_update(l);
            if (!l->data.eff_waveform)
                g_warning("Sample layer '%s' does not have a waveform - skipping", layer_section);
            else
                sampler_program_add_layer(prg, l);
        }
        g_free(where);
    }
    prg->all_layers = g_slist_reverse(prg->all_layers);
    sampler_program_update_layers(prg);    
    return prg;
}

void sampler_program_add_layer(struct sampler_program *prg, struct sampler_layer *l)
{
    // Always call sampler_update_layer before sampler_program_add_layer.
    assert(l->runtime);
    prg->all_layers = g_slist_prepend(prg->all_layers, l);
}

void sampler_program_delete_layer(struct sampler_program *prg, struct sampler_layer *l)
{
    prg->all_layers = g_slist_remove(prg->all_layers, l);
}


void sampler_program_add_group(struct sampler_program *prg, struct sampler_layer *l)
{
    prg->groups = g_slist_prepend(prg->groups, l);
}

void sampler_program_add_controller_init(struct sampler_program *prg, uint8_t controller, uint8_t value)
{
    union sampler_ctrlinit_union u;
    u.ptr = NULL;
    u.cinit.controller = controller;
    u.cinit.value = value;
    prg->ctrl_init_list = g_slist_append(prg->ctrl_init_list, u.ptr);
}

void sampler_program_remove_controller_init(struct sampler_program *prg, uint8_t controller, int which)
{
    for (GSList **p = &prg->ctrl_init_list; *p; )
    {
        const struct sampler_ctrlinit *cin = (const struct sampler_ctrlinit *)&(*p)->data;
        if (cin->controller != controller)
        {
            p = &((*p)->next);
            continue;
        }
        if (which > 0)
            which--;
        GSList *q = (GSList *)cbox_rt_swap_pointers(prg->module->module.rt, (void **)p, (*p)->next);
        g_slist_free1(q);
        if (which == 0)
            break;
    }
}

void sampler_program_destroyfunc(struct cbox_objhdr *hdr_ptr)
{
    struct sampler_program *prg = CBOX_H2O(hdr_ptr);
    sampler_unselect_program(prg->module, prg);
    if (prg->rll)
    {
        sampler_rll_destroy(prg->rll);
        prg->rll = NULL;
    }
    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
        CBOX_DELETE((struct sampler_layer *)p->data);
    for (GSList *p = prg->groups; p; p = g_slist_next(p))
        CBOX_DELETE((struct sampler_layer *)p->data);
    CBOX_DELETE(prg->default_group);

    g_free(prg->name);
    g_free(prg->sample_dir);
    g_free(prg->source_file);
    g_slist_free(prg->all_layers);
    g_slist_free(prg->ctrl_init_list);
    free(prg);
}

void sampler_program_update_layers(struct sampler_program *prg)
{
    struct sampler_module *m = prg->module;
    struct sampler_rll *new_rll = sampler_rll_new_from_program(prg);
    struct sampler_rll *old_rll = cbox_rt_swap_pointers(m->module.rt, (void **)&prg->rll, new_rll);
    if (old_rll)
        sampler_rll_destroy(old_rll);
}

/////////////////////////////////////////////////////////////////////////////////

struct sampler_rll *sampler_rll_new_from_program(struct sampler_program *prg)
{
    struct sampler_rll *rll = malloc(sizeof(struct sampler_rll));
    rll->layers = NULL;
    rll->layers_release = NULL;
    rll->layers_oncc = NULL;
    for (int i = 0; i < 4; i++)
        rll->cc_trigger_bitmask[i] = 0;

    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
    {
        struct sampler_layer *l = p->data;
        int cc = l->data.on_cc_number;
        if (cc != -1)
        {
            rll->layers_oncc = g_slist_prepend(rll->layers_oncc, l);
            rll->cc_trigger_bitmask[cc >> 5] |= 1 << (cc & 31);
        }
        else if (l->data.trigger == stm_release)
            rll->layers_release = g_slist_prepend(rll->layers_release, l);
        else
            rll->layers = g_slist_prepend(rll->layers, l);
    }
    return rll;
}

void sampler_rll_destroy(struct sampler_rll *rll)
{
    g_slist_free(rll->layers);
    g_slist_free(rll->layers_release);
    g_slist_free(rll->layers_oncc);
    free(rll);
}
