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

#include "app.h"
#include "blob.h"
#include "engine.h"
#include "instr.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_prg.h"
#include "sfzloader.h"
#include "tarfile.h"

#include <assert.h>

CBOX_CLASS_DEFINITION_ROOT(sampler_program)

// GSList *sampler_channel_get_next_layer(struct sampler_channel *c, GSList *next_layer, int note, int vel, float random, gboolean is_first, gboolean is_release)

struct sampler_layer *sampler_rll_iterator_next(struct sampler_rll_iterator *iter)
{
retry:
    while(iter->next_layer)
    {
        struct sampler_layer *lr = iter->next_layer->data;
        struct sampler_layer_data *l = lr->runtime;
        iter->next_layer = g_slist_next(iter->next_layer);
        if (!l->eff_waveform)
            continue;

        if (l->eff_use_simple_trigger_logic)
        {
            if (iter->note >= l->lokey && iter->note <= l->hikey &&
                iter->vel >= l->lovel && iter->vel <= l->hivel)
                return lr;
            else
                continue;
        }

        if ((l->trigger == stm_first && !iter->is_first) ||
            (l->trigger == stm_legato && iter->is_first) ||
            (l->trigger == stm_release && !iter->is_release)) // sw_last keyswitches are still added to the note-on list in RLL
            continue;
        int ccval = -1;
        struct sampler_channel *c = iter->channel;
        struct sampler_module *m = c->module;
        if (iter->note >= l->lokey && iter->note <= l->hikey &&
            iter->vel >= l->lovel && iter->vel <= l->hivel &&
            c >= &m->channels[l->lochan - 1] && c <= &m->channels[l->hichan - 1] &&
            iter->random >= l->lorand && iter->random < l->hirand &&
            c->pitchwheel >= l->lobend && c->pitchwheel < l->hibend &&
            c->last_chanaft >= l->lochanaft && c->last_chanaft <= l->hichanaft &&
            c->last_polyaft >= l->lopolyaft && c->last_polyaft <= l->hipolyaft &&
            c->module->module.engine->master->tempo >= l->lobpm && c->module->module.engine->master->tempo < l->hibpm &&
            (!l->cc.is_active || (ccval = sampler_channel_getintcc(c, NULL, l->cc.cc_number), ccval >= l->cc.locc && ccval <= l->cc.hicc)))
        {
            if (!l->eff_use_keyswitch || 
                ((l->sw_down == -1 || (c->switchmask[l->sw_down >> 5] & (1 << (l->sw_down & 31)))) &&
                 (l->sw_up == -1 || !(c->switchmask[l->sw_up >> 5] & (1 << (l->sw_up & 31)))) &&
                 (l->sw_previous == -1 || l->sw_previous == c->previous_note)))
            {
                gboolean play = lr->current_seq_position == 1;
                lr->current_seq_position++;
                if (lr->current_seq_position > l->seq_length)
                    lr->current_seq_position = 1;
                if (play)
                    return lr;
            }
        }
    }
    while(iter->next_keyswitch_index < iter->rll->keyswitch_group_count &&
        iter->next_keyswitch_index < MAX_KEYSWITCH_GROUPS)
    {
        struct sampler_rll *rll = iter->rll;
        uint32_t ks_group = iter->next_keyswitch_index++;

        uint8_t ks_state = iter->channel->keyswitch_state[ks_group];
        if (ks_state == 255) // nothing defined for this switch state
            continue;
        GSList **layers_by_range = iter->is_release ? rll->release_layers_by_range : rll->layers_by_range;
        layers_by_range += (rll->keyswitch_groups[ks_group]->group_offset + ks_state) * rll->layers_by_range_count;
        iter->next_layer = layers_by_range[rll->ranges_by_key[iter->note]];

        if (iter->next_layer)
            goto retry;
    }
    return NULL;
}

void sampler_rll_iterator_init(struct sampler_rll_iterator *iter, struct sampler_rll *rll, struct sampler_channel *c, int note, int vel, float random, gboolean is_first, gboolean is_release)
{
    iter->channel = c;
    iter->note = note;
    iter->vel = vel;
    iter->random = random;
    iter->is_first = is_first;
    iter->is_release = is_release;
    iter->rll = rll;
    iter->next_keyswitch_index = 0;

    if (note >= rll->lokey && note <= rll->hikey)
    {
        assert(note >= 0 && note <= 127);
        GSList **layers_by_range = is_release ? rll->release_layers_by_range : rll->layers_by_range;
        if (layers_by_range)
            iter->next_layer = layers_by_range[rll->ranges_by_key[note]];
        else
            iter->next_layer = NULL;
    }
    else
        iter->next_layer = NULL;
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
    if (!strcmp(cmd->command, "/global") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        return cbox_execute_on(fb, NULL, "/uuid", "o", error, program->global);
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
    if (!strcmp(cmd->command, "/control_labels") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (GSList *p = program->ctrl_label_list; p; p = p->next)
        {
            const struct sampler_ctrllabel *cin = (const struct sampler_ctrllabel *)&p->data;
            if (!cbox_execute_on(fb, NULL, "/control_label", "is", error, (int)cin->controller, cin->label))
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
        struct sampler_layer *l = sampler_layer_new(program->module, program, program->global->default_child);
        return cbox_execute_on(fb, NULL, "/uuid", "o", error, l);
    }
    if (!strcmp(cmd->command, "/clone_to") && !strcmp(cmd->arg_types, "si"))
    {
        struct cbox_instrument *instrument = (struct cbox_instrument *)CBOX_ARG_O(cmd, 0, program, cbox_instrument, error);
        if (!instrument)
            return FALSE;
        struct cbox_module *module = instrument->module;
        if (strcmp(module->engine_name, "sampler"))
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot copy sampler program to module '%s' of type '%s'", module->instance_name, module->engine_name);
            return FALSE;
        }
        struct sampler_program *prg = sampler_program_clone(program, (struct sampler_module *)module, CBOX_ARG_I(cmd, 1), error);
        if (!prg)
            return FALSE;
        sampler_register_program((struct sampler_module *)module, prg);
        return cbox_execute_on(fb, NULL, "/uuid", "o", error, prg);
    }
    if (!strcmp(cmd->command, "/load_file") && !strcmp(cmd->arg_types, "si"))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        struct cbox_blob *blob = cbox_blob_new_from_file(program->name, program->tarfile, program->sample_dir, CBOX_ARG_S(cmd, 0), CBOX_ARG_I(cmd, 1), error);
        if (!blob)
            return FALSE;
        return cbox_execute_on(fb, NULL, "/data", "b", error, blob);
    }
    return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

struct sampler_program *sampler_program_new(struct sampler_module *m, int prog_no, const char *name, struct cbox_tarfile *tarfile, const char *sample_dir, GError **error)
{
    gchar *perm_sample_dir = g_strdup(sample_dir);
    if (sample_dir && !perm_sample_dir)
        return NULL;

    struct cbox_document *doc = CBOX_GET_DOCUMENT(&m->module);
    struct sampler_program *prg = malloc(sizeof(struct sampler_program));
    if (!prg)
    {
        g_free(perm_sample_dir);
        return NULL;
    }
    memset(prg, 0, sizeof(*prg));
    CBOX_OBJECT_HEADER_INIT(prg, sampler_program, doc);
    cbox_command_target_init(&prg->cmd_target, sampler_program_process_cmd, prg);

    prg->module = m;
    prg->prog_no = prog_no;
    prg->name = g_strdup(name);
    prg->tarfile = tarfile;
    prg->source_file = NULL;
    prg->sample_dir = perm_sample_dir;
    prg->all_layers = NULL;
    prg->rll = NULL;
    prg->ctrl_init_list = NULL;
    prg->ctrl_label_list = NULL;
    prg->global = sampler_layer_new(m, prg, NULL);
    prg->global->default_child = sampler_layer_new(m, prg, prg->global);
    prg->global->default_child->default_child = sampler_layer_new(m, prg, prg->global->default_child);
    prg->deleting = FALSE;
    prg->in_use = 0;
    for (int i = 0; i < MAX_MIDI_CURVES; ++i)
    {
        prg->curves[i] = NULL;
        prg->interpolated_curves[i] = NULL;
    }
    CBOX_OBJECT_REGISTER(prg);
    return prg;
}

struct sampler_program *sampler_program_new_from_cfg(struct sampler_module *m, const char *cfg_section, const char *name, int pgm_id, GError **error)
{
    int i;
    
    char *name2 = NULL, *sfz_path = NULL, *spath = NULL, *tar_name = NULL;
    const char *sfz = NULL;
    struct cbox_tarfile *tarfile = NULL;
    
    g_clear_error(error);
    tar_name = cbox_config_get_string(cfg_section, "tar");
    if (!strncmp(cfg_section, "spgm:!", 6))
    {
        sfz = cfg_section + 6;
        if (!strncmp(sfz, "sbtar:", 6))
        {
            sfz_path = ".";
            gchar *p = strchr(sfz + 6, ';');
            if (p)
            {
                char *tmp = g_strndup(sfz + 6, p - sfz - 6);
                tarfile = cbox_tarpool_get_tarfile(app.tarpool, tmp, error);
                g_free(tmp);
                if (!tarfile)
                    return NULL;
                sfz = p + 1;
                name2 = p + 1;
            }
            else
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot load sampler program '%s' from section '%s': missing name of a file inside a tar archive", name, cfg_section);
                return NULL;
            }
        }
        else
        {
            name2 = strrchr(name, '/');
            if (name2)
                name2++;
        }
    }
    else
    { 
        if (tar_name)
        {
            tarfile = cbox_tarpool_get_tarfile(app.tarpool, tar_name, error);
            if (!tarfile)
                return NULL;
        }
        if (!sfz && !cbox_config_has_section(cfg_section))
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot load sampler program '%s' from section '%s': section not found", name, cfg_section);
            return FALSE;
        }
        name2 = cbox_config_get_string(cfg_section, "name");

        sfz_path = cbox_config_get_string(cfg_section, "sfz_path");
        spath = cbox_config_get_string(cfg_section, "sample_path");
        sfz = cbox_config_get_string(cfg_section, "sfz");
        if (tarfile && !sfz_path)
            sfz_path = ".";
    }
    
    if (sfz && !sfz_path && !spath && !tarfile)
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
        tarfile,
        spath ? spath : (sfz_path ? sfz_path : ""),
        error
    );
    if (!prg)
        return NULL;
    
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
    } else {
        prg->source_file = g_strdup_printf("config:%s", cfg_section);
    }
    
    for (i = 0; ; i++)
    {
        gchar *s = g_strdup_printf("group%d", 1 + i);
        const char *group_section = cbox_config_get_string(cfg_section, s);
        g_free(s);
        if (!group_section)
            break;
        
        gchar *swhere = g_strdup_printf("sgroup:%s", group_section);
        struct sampler_layer *g = sampler_layer_new_from_section(m, prg, prg->global->default_child, swhere);
        if (!g)
            g_warning("Sample layer '%s' cannot be created - skipping", group_section);
        else
        {
            // XXXKF
            // sampler_program_add_group(prg, g);
            for (int j = 0; ; j++)
            {
                char *where = NULL;
                gchar *s = g_strdup_printf("layer%d", 1 + j);
                const char *layer_section = cbox_config_get_string(swhere, s);
                g_free(s);
                if (!layer_section)
                    break;
                where = g_strdup_printf("slayer:%s", layer_section);
                struct sampler_layer *l = sampler_layer_new_from_section(m, prg, g, where);
                if (!l)
                    g_warning("Sample layer '%s' cannot be created - skipping", layer_section);
                else 
                {
                    sampler_layer_update(l);
                    if (!l->data.eff_waveform)
                    {
                        g_warning("Sample layer '%s' does not have a waveform - skipping", layer_section);
                        CBOX_DELETE((struct sampler_layer *)l);
                    }
                    else
                        sampler_program_add_layer(prg, l);
                }
                g_free(where);
            }
        }
        g_free(swhere);
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
        
        struct sampler_layer *l = sampler_layer_new_from_section(m, prg, NULL, where);
        if (!l)
            g_warning("Sample layer '%s' cannot be created - skipping", layer_section);
        else 
        {
            sampler_layer_update(l);
            if (!l->data.eff_waveform)
            {
                g_warning("Sample layer '%s' does not have a waveform - skipping", layer_section);
                CBOX_DELETE((struct sampler_layer *)l);
            }
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
    assert(l->parent);
    assert(l->parent->parent);
    assert(l->parent->parent->parent);
    assert(l->parent->parent->parent == prg->global);
    prg->all_layers = g_slist_prepend(prg->all_layers, l);
}

void sampler_program_delete_layer(struct sampler_program *prg, struct sampler_layer *l)
{
    prg->all_layers = g_slist_remove(prg->all_layers, l);
}


void sampler_program_add_controller_init(struct sampler_program *prg, uint16_t controller, uint8_t value)
{
    union sampler_ctrlinit_union u;
    u.ptr = NULL;
    u.cinit.controller = controller;
    u.cinit.value = value;
    prg->ctrl_init_list = g_slist_append(prg->ctrl_init_list, u.ptr);
}

static void sampler_ctrl_label_destroy(gpointer value)
{
    struct sampler_ctrllabel *label = value;
    free(label->label);
    free(label);
}

void sampler_program_add_controller_label(struct sampler_program *prg, uint16_t controller, gchar *text)
{
    struct sampler_ctrllabel *label = calloc(1, sizeof(struct sampler_ctrllabel));
    label->controller = controller;
    label->label = text;
    prg->ctrl_label_list = g_slist_append(prg->ctrl_label_list, label);
}

void sampler_program_remove_controller_init(struct sampler_program *prg, uint16_t controller, int which)
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

static void delete_layers_recursively(struct sampler_layer *layer)
{
    GHashTableIter iter;
    g_hash_table_iter_init(&iter, layer->child_layers);
    GSList *dellist = NULL;
    gpointer key, value;
    while(g_hash_table_iter_next(&iter, &key, &value))
        dellist = g_slist_prepend(dellist, key);
    GSList *liter = dellist;
    while(liter)
    {
        struct sampler_layer *chl = liter->data;
        delete_layers_recursively(chl);
        liter = liter->next;
    }
    g_slist_free(dellist);
    CBOX_DELETE(layer);
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
    delete_layers_recursively(prg->global);

    for (int i = 0; i < MAX_MIDI_CURVES; ++i)
    {
        g_free(prg->curves[i]);
        g_free(prg->interpolated_curves[i]);
    }
    g_free(prg->name);
    g_free(prg->sample_dir);
    g_free(prg->source_file);
    g_slist_free(prg->all_layers);
    g_slist_free(prg->ctrl_init_list);
    g_slist_free_full(prg->ctrl_label_list, sampler_ctrl_label_destroy);
    if (prg->tarfile)
        cbox_tarpool_release_tarfile(app.tarpool, prg->tarfile);
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

struct sampler_program *sampler_program_clone(struct sampler_program *prg, struct sampler_module *m, int prog_no, GError **error)
{
    struct sampler_program *newprg = sampler_program_new(m, prog_no, prg->name, prg->tarfile, prg->sample_dir, error);
    if (!newprg)
        return NULL;
    if (prg->source_file)
        newprg->source_file = g_strdup(prg->source_file);
    // The values are stored as a union aliased with the data pointer, so no need to deep-copy
    newprg->ctrl_init_list = g_slist_copy(prg->ctrl_init_list);
    // XXXKF ctrl_label_list
    newprg->rll = NULL;
    newprg->global = sampler_layer_new_clone(prg->global, m, newprg, NULL);
    sampler_program_update_layers(newprg);
    if (newprg->tarfile)
        newprg->tarfile->refs++;
    
    return newprg;
}

