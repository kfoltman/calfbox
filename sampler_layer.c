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

#include "config-api.h"
#include "dspmath.h"
#include "errors.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "sampler.h"
#include "sfzloader.h"
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <memory.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

static void sampler_layer_data_set_modulation(struct sampler_layer_data *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags, gboolean propagating_defaults)
{
    GSList *p = l->modulations;
    while(p)
    {
        struct sampler_modulation *sm = p->data;
        if (sm->src == src && sm->src2 == src2 && sm->dest == dest)
        {
            // do not overwrite locally set value with defaults
            if (propagating_defaults && sm->has_value)
                return;
            sm->amount = amount;
            sm->flags = flags;
            sm->has_value = !propagating_defaults;
            return;
        }
        p = g_slist_next(p);
    }
    struct sampler_modulation *sm = g_malloc0(sizeof(struct sampler_modulation));
    sm->src = src;
    sm->src2 = src2;
    sm->dest = dest;
    sm->amount = amount;
    sm->flags = flags;
    sm->has_value = !propagating_defaults;
    l->modulations = g_slist_prepend(l->modulations, sm);
}

void sampler_layer_set_modulation1(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_moddest dest, float amount, int flags)
{
    sampler_layer_data_set_modulation(&l->data, src, smsrc_none, dest, amount, flags, FALSE);
}

void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags)
{
    sampler_layer_data_set_modulation(&l->data, src, src2, dest, amount, flags, FALSE);
}

void sampler_layer_data_add_nif(struct sampler_layer_data *l, SamplerNoteInitFunc notefunc, int variant, float param, gboolean propagating_defaults)
{
    GSList *p = l->nifs;
    while(p)
    {
        struct sampler_noteinitfunc *nif = p->data;
        if (nif->notefunc == notefunc && nif->variant == variant)
        {
            // do not overwrite locally set value with defaults
            if (propagating_defaults && nif->has_value)
                return;
            nif->param = param;
            nif->has_value = !propagating_defaults;
            return;
        }
        p = g_slist_next(p);
    }
    struct sampler_noteinitfunc *nif = malloc(sizeof(struct sampler_noteinitfunc));
    nif->notefunc = notefunc;
    nif->variant = variant;
    nif->param = param;
    nif->has_value = !propagating_defaults;
    l->nifs = g_slist_prepend(l->nifs, nif);
}

void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc, int variant, float param)
{
    sampler_layer_data_add_nif(&l->data, notefunc, variant, param, FALSE);
}

static gboolean sampler_layer_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct sampler_layer *layer = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!((!layer->parent_program || cbox_execute_on(fb, NULL, "/parent_program", "o", error, layer->parent_program)) && 
            (!layer->parent_group || cbox_execute_on(fb, NULL, "/parent_group", "o", error, layer->parent_group)) && 
            CBOX_OBJECT_DEFAULT_STATUS(layer, fb, error)))
            return FALSE;
        return TRUE;
    }
    if ((!strcmp(cmd->command, "/as_string") || !strcmp(cmd->command, "/as_string_full")) && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        gchar *res = sampler_layer_to_string(layer, !strcmp(cmd->command, "/as_string_full"));
        gboolean result = cbox_execute_on(fb, NULL, "/value", "s", error, res);
        g_free(res);
        return result;
    }
    if (!strcmp(cmd->command, "/set_param") && !strcmp(cmd->arg_types, "ss"))
    {
        const char *key = CBOX_ARG_S(cmd, 0);
        const char *value = CBOX_ARG_S(cmd, 1);
        if (sampler_layer_apply_param(layer, key, value, error))
        {
            sampler_update_layer(layer->module, layer);
            sampler_update_program_layers(layer->module, layer->parent_program);
            return TRUE;
        }
        return FALSE;
    }
    if (!strcmp(cmd->command, "/new_region") && !strcmp(cmd->arg_types, ""))
    {
        if (layer->parent_group)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create a region within a region");
            return FALSE;
        }
        struct sampler_layer *l = sampler_layer_new(layer->module, layer->parent_program, layer);
        sampler_layer_data_finalize(&l->data, l->parent_group ? &l->parent_group->data : NULL, layer->module);
        sampler_layer_reset_switches(l, l->module);
        sampler_update_layer(l->module, l);

        sampler_program_add_layer(layer->parent_program, l);
        sampler_update_program_layers(layer->module, layer->parent_program);
        
        return cbox_execute_on(fb, NULL, "/uuid", "o", error, l);
    }
    if (!strcmp(cmd->command, "/get_children") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, layer->child_layers);
        gpointer key, value;
        while(g_hash_table_iter_next(&iter, &key, &value))
        {
            if (!cbox_execute_on(fb, NULL, "/region", "o", error, key))
                return FALSE;
        }
        return TRUE;
    }
    // otherwise, treat just like an command on normal (non-aux) output
    return cbox_object_default_process_cmd(ct, fb, cmd, error);
    
}


#define PROC_FIELDS_INITIALISER(type, name, def_value) \
    ld->name = def_value; \
    ld->has_##name = 0;
#define PROC_FIELDS_INITIALISER_enum(type, name, def_value) \
    PROC_FIELDS_INITIALISER(type, name, def_value)
#define PROC_FIELDS_INITIALISER_dBamp(type, name, def_value) \
    ld->name = def_value; \
    ld->name##_linearized = -1; \
    ld->has_##name = 0;
#define PROC_FIELDS_INITIALISER_dahdsr(name, parname, index) \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_RESET_FIELD, name, ld); \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name, ld)
#define PROC_FIELDS_INITIALISER_lfo(name, parname, index) \
    LFO_FIELDS(PROC_SUBSTRUCT_RESET_FIELD, name, ld); \
    LFO_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name, ld)

CBOX_CLASS_DEFINITION_ROOT(sampler_layer)

struct sampler_layer *sampler_layer_new(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group)
{
    struct sampler_layer *l = malloc(sizeof(struct sampler_layer));
    struct cbox_document *doc = CBOX_GET_DOCUMENT(parent_program);
    memset(l, 0, sizeof(struct sampler_layer));
    CBOX_OBJECT_HEADER_INIT(l, sampler_layer, doc);
    cbox_command_target_init(&l->cmd_target, sampler_layer_process_cmd, l);
    
    l->module = m;
    l->child_layers = g_hash_table_new(NULL, NULL);
    if (parent_group)
    {
        sampler_layer_data_clone(&l->data, &parent_group->data, FALSE);
        l->parent_program = parent_program;
        l->parent_group = parent_group;
        g_hash_table_add(parent_group->child_layers, l);
        l->runtime = NULL;
        CBOX_OBJECT_REGISTER(l);
        return l;
    }
    l->parent_program = parent_program;
    struct sampler_layer_data *ld = &l->data;
    ld->waveform = NULL;
    ld->has_waveform = FALSE;
    
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_INITIALISER)
    
    ld->eff_freq = 44100;
    ld->velcurve[0] = 0;
    ld->velcurve[127] = 1;
    for (int i = 1; i < 127; i++)
        ld->velcurve[i] = -1;
    ld->modulations = NULL;
    ld->nifs = NULL;

    ld->eff_use_keyswitch = 0;
    l->last_key = -1;
    if (!parent_group)
    {
        sampler_layer_set_modulation1(l, 74, smdest_cutoff, 9600, 2);
        sampler_layer_set_modulation1(l, 71, smdest_resonance, 12, 2);
        sampler_layer_set_modulation(l, smsrc_pitchlfo, 1, smdest_pitch, 100, 0);
    }
    l->runtime = NULL;
    l->unknown_keys = NULL;
    CBOX_OBJECT_REGISTER(l);
    return l;
}

#define PROC_FIELDS_CLONE(type, name, def_value) \
    dst->name = src->name; \
    dst->has_##name = copy_hasattr ? src->has_##name : FALSE;
#define PROC_FIELDS_CLONE_dBamp PROC_FIELDS_CLONE
#define PROC_FIELDS_CLONE_enum PROC_FIELDS_CLONE
#define PROC_FIELDS_CLONE_dahdsr(name, parname, index) \
        DAHDSR_FIELDS(PROC_SUBSTRUCT_CLONE, name, dst, src) \
        if (!copy_hasattr) \
            DAHDSR_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name, dst) 
#define PROC_FIELDS_CLONE_lfo(name, parname, index) \
        LFO_FIELDS(PROC_SUBSTRUCT_CLONE, name, dst, src) \
        if (!copy_hasattr) \
            LFO_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name, dst)

void sampler_layer_data_clone(struct sampler_layer_data *dst, const struct sampler_layer_data *src, gboolean copy_hasattr)
{
    dst->waveform = src->waveform;
    if (dst->waveform)
        cbox_waveform_ref(dst->waveform);
    if (copy_hasattr)
        dst->has_waveform = src->has_waveform;
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_CLONE)
    memcpy(dst->velcurve, src->velcurve, 128 * sizeof(float));
    dst->modulations = g_slist_copy(src->modulations);
    for(GSList *mod = dst->modulations; mod; mod = mod->next)
    {
        struct sampler_modulation *srcm = mod->data;
        struct sampler_modulation *dstm = g_malloc(sizeof(struct sampler_modulation));
        memcpy(dstm, srcm, sizeof(struct sampler_modulation));
        dstm->has_value = copy_hasattr ? srcm->has_value : FALSE;
        mod->data = dstm;
    }
    dst->nifs = g_slist_copy(src->nifs);
    for(GSList *nif = dst->nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *dstn = g_malloc(sizeof(struct sampler_noteinitfunc));
        struct sampler_noteinitfunc *srcn = nif->data;
        memcpy(dstn, srcn, sizeof(struct sampler_noteinitfunc));
        dstn->has_value = copy_hasattr ? srcn->has_value : FALSE;
        nif->data = dstn;
    }
}

#define PROC_FIELDS_CLONEPARENT(type, name, def_value) \
    if (!l->has_##name) \
        l->name = parent ? parent->name : def_value;
#define PROC_FIELDS_CLONEPARENT_dBamp PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_enum PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_dahdsr(name, parname, index) \
        DAHDSR_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_lfo(name, parname, index) \
        LFO_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)

static void sampler_layer_data_getdefaults(struct sampler_layer_data *l, struct sampler_layer_data *parent)
{
    if (!l->has_waveform)
        l->waveform = parent ? parent->waveform : NULL;
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_CLONEPARENT)
    // XXXKF: add handling for velcurve
    if (parent)
    {
        // set NIFs used by parent
        for(GSList *mod = parent->nifs; mod; mod = mod->next)
        {
            struct sampler_noteinitfunc *nif = mod->data;
            sampler_layer_data_add_nif(l, nif->notefunc, nif->variant, nif->param, TRUE);
        }
        // set modulations used by parent
        for(GSList *mod = parent->modulations; mod; mod = mod->next)
        {
            struct sampler_modulation *srcm = mod->data;
            sampler_layer_data_set_modulation(l, srcm->src, srcm->src2, srcm->dest, srcm->amount, srcm->flags, TRUE);
        }
    }
}

void sampler_layer_set_waveform(struct sampler_layer *l, struct cbox_waveform *waveform)
{
    l->data.has_waveform = waveform != NULL;
    l->data.waveform = waveform;
}

#define PROC_FIELDS_FINALISER(type, name, def_value) 
#define PROC_FIELDS_FINALISER_enum(type, name, def_value) 
#define PROC_FIELDS_FINALISER_dBamp(type, name, def_value) \
    l->name##_linearized = dB2gain(l->name);
#define PROC_FIELDS_FINALISER_dahdsr(name, parname, index) \
    cbox_envelope_init_dahdsr(&l->name##_shape, &l->name, m->module.srate / CBOX_BLOCK_SIZE, 100.f);
#define PROC_FIELDS_FINALISER_lfo(name, parname, index) /* no finaliser required */

void sampler_layer_data_finalize(struct sampler_layer_data *l, struct sampler_layer_data *parent, struct sampler_module *m)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_FINALISER)

    sampler_layer_data_getdefaults(l, parent);
    l->eff_freq = (l->waveform && l->waveform->info.samplerate) ? l->waveform->info.samplerate : 44100;
    // XXXKF should have a distinction between 'configured' and 'effective' loop start/end
    if (l->loop_mode == slm_unknown)
    {
        if (l->waveform && l->waveform->has_loop)
            l->loop_mode = slm_loop_continuous;
        else
            l->loop_mode = l->loop_end == 0 ? slm_no_loop : slm_loop_continuous;
    }
    
    if (l->loop_mode == slm_one_shot || l->loop_mode == slm_no_loop || l->loop_mode == slm_one_shot_chokeable)
        l->loop_start = -1;

    if ((l->loop_mode == slm_loop_continuous || l->loop_mode == slm_loop_sustain) && l->loop_start == -1)
        l->loop_start = 0;
    if ((l->loop_mode == slm_loop_continuous || l->loop_mode == slm_loop_sustain) && l->loop_start == 0 && l->waveform && l->waveform->has_loop)
        l->loop_start = l->waveform->loop_start;
    if (l->loop_end == 0 && l->waveform != NULL && l->waveform->has_loop)
        l->loop_end = l->waveform->loop_end;

    if (l->off_mode == som_unknown)
        l->off_mode = l->off_by != 0 ? som_fast : som_normal;

    // if no amp_velcurve_nnn setting, default to quadratic
    if (l->velcurve_quadratic == -1)
        l->velcurve_quadratic = 1;
    
    if (l->key >= 0 && l->key <= 127)
        l->lokey = l->hikey = l->pitch_keycenter = l->key;

    // interpolate missing points in velcurve
    int start = 0;
    for (int i = 1; i < 128; i++)
    {
        if (l->velcurve[i] == -1)
            continue;
        float sv = l->velcurve[start];
        float ev = l->velcurve[i];
        if (l->velcurve_quadratic)
        {
            for (int j = start; j <= i; j++)
                l->eff_velcurve[j] = sv + (ev - sv) * (j - start) * (j - start) / ((i - start) * (i - start));
        }
        else
        {
            for (int j = start; j <= i; j++)
                l->eff_velcurve[j] = sv + (ev - sv) * (j - start) / (i - start);
        }
        start = i;
    }
    l->eff_use_keyswitch = ((l->sw_down != -1) || (l->sw_up != -1) || (l->sw_last != -1) || (l->sw_previous != -1));
}

void sampler_layer_reset_switches(struct sampler_layer *l, struct sampler_module *m)
{
    l->last_key = l->data.sw_lokey;
    l->current_seq_position = l->data.seq_position;
}

struct layer_foreach_struct
{
    struct sampler_layer *layer;
    const char *cfg_section;
};

static void layer_foreach_func(void *user_data, const char *key)
{
    if (!strcmp(key, "file"))
        key = "sample";
    // import is handled in sampler_load_layer_overrides
    if (!strcmp(key, "import"))
        return;
    struct layer_foreach_struct *lfs = user_data;
    const char *value = cbox_config_get_string(lfs->cfg_section, key);
    GError *error = NULL;
    if (!sampler_layer_apply_param(lfs->layer, key, value, &error))
    {
        if (error)
            g_warning("Error '%s', context: %s in section %s", error->message, key, lfs->cfg_section);
        else
            g_warning("Unknown sample layer parameter: %s in section %s", key, lfs->cfg_section);
    }
}

void sampler_layer_load_overrides(struct sampler_layer *l, const char *cfg_section)
{
    char *imp = cbox_config_get_string(cfg_section, "import");
    if (imp)
        sampler_layer_load_overrides(l, imp);
    
    struct layer_foreach_struct lfs = {
        .layer = l,
        .cfg_section = cfg_section
    };
    cbox_config_foreach_key(layer_foreach_func, cfg_section, &lfs);
}

struct sampler_layer *sampler_layer_new_from_section(struct sampler_module *m, struct sampler_program *parent_program, const char *cfg_section)
{
    struct sampler_layer *l = sampler_layer_new(m, parent_program, NULL);
    sampler_layer_load_overrides(l, cfg_section);
    sampler_layer_data_finalize(&l->data, l->parent_group ? &l->parent_group->data : NULL, m);
    sampler_layer_reset_switches(l, m);
    return l;
}

static int sfz_note_from_string(const char *note)
{
    static const int semis[] = {9, 11, 0, 2, 4, 5, 7};
    int pos;
    int nn = tolower(note[0]);
    int nv;
    if (nn >= '0' && nn <= '9')
        return atoi(note);
    if (nn < 'a' && nn > 'g')
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

static gboolean parse_envelope_param(struct sampler_layer *layer, struct cbox_dahdsr *env, struct sampler_dahdsr_has_fields *has_fields, int env_type, const char *key, const char *value)
{
    static const enum sampler_modsrc srcs[] = { smsrc_ampenv, smsrc_filenv, smsrc_pitchenv };
    static const enum sampler_moddest dests[] = { smdest_gain, smdest_cutoff, smdest_pitch };
    enum sampler_modsrc src = srcs[env_type];
    enum sampler_moddest dest = dests[env_type];
    float fvalue = atof(value);
    
#define PROC_SET_ENV_FIELD(name, index, def_value) \
        if (!strcmp(key, #name)) {\
            env->name = fvalue; \
            has_fields->name = 1; \
            return TRUE; \
        }
    DAHDSR_FIELDS(PROC_SET_ENV_FIELD)
    if (!strcmp(key, "depth"))
        sampler_layer_set_modulation1(layer, src, dest, atof(value), 0);
    else if (!strcmp(key, "vel2delay"))
        sampler_layer_add_nif(layer, sampler_nif_vel2env, (env_type << 4) + 0, fvalue);
    else if (!strcmp(key, "vel2attack"))
        sampler_layer_add_nif(layer, sampler_nif_vel2env, (env_type << 4) + 1, fvalue);
    else if (!strcmp(key, "vel2hold"))
        sampler_layer_add_nif(layer, sampler_nif_vel2env, (env_type << 4) + 2, fvalue);
    else if (!strcmp(key, "vel2decay"))
        sampler_layer_add_nif(layer, sampler_nif_vel2env, (env_type << 4) + 3, fvalue);
    else if (!strcmp(key, "vel2sustain"))
        sampler_layer_add_nif(layer, sampler_nif_vel2env, (env_type << 4) + 4, fvalue);
    else if (!strcmp(key, "vel2release"))
        sampler_layer_add_nif(layer, sampler_nif_vel2env, (env_type << 4) + 5, fvalue);
    else if (!strcmp(key, "vel2depth"))
        sampler_layer_set_modulation(layer, src, smsrc_vel, dest, atof(value), 0);
    else
        return FALSE;
    return TRUE;
}

static gboolean parse_lfo_param(struct sampler_layer *layer, struct sampler_lfo_params *params, struct sampler_lfo_has_fields *has_fields, int lfo_type, const char *key, const char *value)
{
    static const enum sampler_modsrc srcs[] = { smsrc_amplfo, smsrc_fillfo, smsrc_pitchlfo };
    static const enum sampler_moddest dests[] = { smdest_gain, smdest_cutoff, smdest_pitch };
    enum sampler_modsrc src = srcs[lfo_type];
    enum sampler_moddest dest = dests[lfo_type];

#define PROC_SET_LFO_FIELD(name, index, def_value) \
        if (!strcmp(key, #name)) {\
            params->name = fvalue; \
            has_fields->name = 1; \
            return TRUE; \
        }
    float fvalue = atof(value);
    LFO_FIELDS(PROC_SET_LFO_FIELD)
    if (!strcmp(key, "depth"))
        sampler_layer_set_modulation1(layer, src, dest, fvalue, 0);
    else if (!strcmp(key, "depthchanaft"))
        sampler_layer_set_modulation(layer, src, smsrc_chanaft, dest, fvalue, 0);
    else if (!strcmp(key, "depthpolyaft"))
        sampler_layer_set_modulation(layer, src, smsrc_polyaft, dest, fvalue, 0);
    else if (!strncmp(key, "depthcc", 7))
    {
        int cc = atoi(key + 7);
        if (cc > 0 && cc < 120)
            sampler_layer_set_modulation(layer, src, cc, dest, fvalue, 0);
        else
            return FALSE;
    }
    else 
        return FALSE;
    return TRUE;
}

#define PARSE_PARAM_midi_note_t(field, strname, valuestr) \
    return ((l->data.field = sfz_note_from_string(value)), (l->data.has_##field = 1));
#define PARSE_PARAM_int(field, strname, valuestr) \
    return ((l->data.field = atoi(value)), (l->data.has_##field = 1));
#define PARSE_PARAM_uint32_t(field, strname, valuestr) \
    return ((l->data.field = (uint32_t)strtoul(value, NULL, 10)), (l->data.has_##field = 1));
#define PARSE_PARAM_float(field, strname, valuestr) \
    return ((l->data.field = atof(value)), (l->data.has_##field = 1));

#define PROC_APPLY_PARAM(type, name, def_value) \
    if (!strcmp(key, #name)) { \
        PARSE_PARAM_##type(name, #name, value) \
    }
#define PROC_APPLY_PARAM_dBamp(type, name, def_value) \
    if (!strcmp(key, #name)) { \
        return ((l->data.name = atof(value)), (l->data.has_##name = 1)); \
    }
#define PROC_APPLY_PARAM_enum(enumtype, name, def_value) \
    if (!strcmp(key, #name)) { \
        if (!enumtype##_from_string(value, &l->data.name)) { \
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Value %s is not a correct value for %s", value, #name); \
            return FALSE; \
        } \
        l->data.has_##name = 1; \
        return TRUE; \
    }
// LFO and envelope need special handling now
#define PROC_APPLY_PARAM_dahdsr(name, parname, index) \
    if (!strncmp(key, #parname "_", sizeof(#parname))) \
        return parse_envelope_param(l, &l->data.name, &l->data.has_##name, index, key + sizeof(#parname), value);
#define PROC_APPLY_PARAM_lfo(name, parname, index) \
    if (!strncmp(key, #parname "_", sizeof(#parname))) \
        return parse_lfo_param(l, &l->data.name, &l->data.has_##name, index, key + sizeof(#parname), value);

static void sampler_layer_apply_unknown(struct sampler_layer *l, const char *key, const char *value)
{
    if (!l->unknown_keys)
        l->unknown_keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    g_hash_table_insert(l->unknown_keys, g_strdup(key), g_strdup(value));
}

gboolean sampler_layer_apply_param(struct sampler_layer *l, const char *key, const char *value, GError **error)
{
try_now:
    // XXXKF: this is seriously stupid code, this should use a hash table for
    // fixed strings, or something else that doesn't explode O(N**2) with
    // number of attributes. But premature optimization is a root of all evil.

    SAMPLER_FIXED_FIELDS(PROC_APPLY_PARAM)

    // XXXKF: to make things worse, some attributes have names different from
    // C field names, or have multiple names, or don't map 1:1 to internal model
    
    if (!strcmp(key, "sample"))
    {
        struct cbox_waveform *old_waveform = l->data.waveform;
        gchar *value_copy = g_strdup(value);
        gchar *filename = value_copy;
        if (value[0] != '*')
        {
            for (int i = 0; value_copy[i]; i++)
            {
                if (value_copy[i] == '\\')
                    value_copy[i] = '/';
            }
            filename = g_build_filename(l->parent_program->sample_dir, value_copy, NULL);
            g_free(value_copy);
        }
        struct cbox_waveform *wf = cbox_wavebank_get_waveform(l->parent_program->source_file, filename, error);
        g_free(filename);
        if (!wf)
            return FALSE;
        sampler_layer_set_waveform(l, wf);
        if (old_waveform != NULL)
            cbox_waveform_unref(old_waveform);
        return TRUE;
    }
    else if (!strcmp(key, "lolev"))
        l->data.lovel = atoi(value), l->data.has_lovel = 1;
    else if (!strcmp(key, "hilev"))
        l->data.hivel = atoi(value), l->data.has_hivel = 1;
    else if (!strcmp(key, "loopstart"))
        l->data.loop_start = atoi(value), l->data.has_loop_start = 1;
    else if (!strcmp(key, "loopend"))
        l->data.loop_end = atoi(value), l->data.has_loop_end = 1;
    else if (!strcmp(key, "loopmode"))
    {
        key = "loop_mode";
        goto try_now; // yes, goto, why not?
    }
    else if (!strcmp(key, "cutoff_chanaft"))
        sampler_layer_set_modulation1(l, smsrc_chanaft, smdest_cutoff, atof(value), 0);
    else if (!strcmp(key, "amp_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 0, atof(value));
    else if (!strcmp(key, "fil_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 1, atof(value));
    else if (!strcmp(key, "pitch_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 2, atof(value));
    else if (!strcmp(key, "pitch_veltrack"))
        sampler_layer_add_nif(l, sampler_nif_vel2pitch, 0, atof(value));
    else if (!strncmp(key, "delay_cc", 8))
    {
        int ccno = atoi(key + 8);
        if (ccno > 0 && ccno < 120)
            sampler_layer_add_nif(l, sampler_nif_cc2delay, ccno, atof(value));
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "cutoff_cc", 9))
    {
        int ccno = atoi(key + 9);
        if (ccno > 0 && ccno < 120)
            sampler_layer_set_modulation1(l, ccno, smdest_cutoff, atof(value), 0);
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "amp_velcurve_", 13))
    {
        // if not known yet, set to 0, it can always be overriden via velcurve_quadratic setting
        if (l->data.velcurve_quadratic == -1)
            l->data.velcurve_quadratic = 0;
        int point = atoi(key + 13);
        if (point >= 0 && point <= 127)
        {
            l->data.velcurve[point] = atof(value);
            if (l->data.velcurve[point] < 0)
                l->data.velcurve[point] = 0;
            if (l->data.velcurve[point] > 1)
                l->data.velcurve[point] = 1;
        }
        else
            goto unknown_key;
    }
    else
        goto unknown_key;
    
    return TRUE;
unknown_key:
    sampler_layer_apply_unknown(l, key, value);
    g_warning("Unknown SFZ property key: '%s'", key);
    return TRUE;
}

#define TYPE_PRINTF_uint32_t(name, def_value) \
    if (show_inherited || l->has_##name) \
        g_string_append_printf(outstr, " %s=%u", #name, (unsigned)(l->name));
#define TYPE_PRINTF_int(name, def_value) \
    if (show_inherited || l->has_##name) \
        g_string_append_printf(outstr, " %s=%d", #name, (int)(l->name));
#define TYPE_PRINTF_midi_note_t(name, def_value) \
    if (show_inherited || l->has_##name) { \
        int val = l->name; \
        if (val == -1) \
            g_string_append_printf(outstr, " %s=-1", #name); \
        else \
            g_string_append_printf(outstr, " %s=%c%s%d", #name, "ccddeffggaab"[val%12], "\000#\000#\000\000#\000#\000#\000#\000"+(val%12), (val/12-1)); \
    } else {}
#define TYPE_PRINTF_float(name, def_value) \
    if (show_inherited || l->has_##name) \
        g_string_append_printf(outstr, " %s=%g", #name, (float)(l->name));

#define PROC_FIELDS_TO_FILEPTR(type, name, def_value) \
    TYPE_PRINTF_##type(name, def_value)
#define PROC_FIELDS_TO_FILEPTR_dBamp(type, name, def_value) \
    if (show_inherited || l->has_##name) \
        g_string_append_printf(outstr, " %s=%g", #name, (float)(l->name));
#define PROC_FIELDS_TO_FILEPTR_enum(enumtype, name, def_value) \
    if ((show_inherited || l->has_##name) && (tmpstr = enumtype##_to_string(l->name)) != NULL) \
        g_string_append_printf(outstr, " %s=%s", #name, tmpstr);

#define ENV_PARAM_OUTPUT(param, index, def_value, env, envfield, envname) \
    if (show_inherited || l->has_##envfield.param) \
        g_string_append_printf(outstr, " " #envname "_" #param "=%g", env.param);

#define PROC_FIELDS_TO_FILEPTR_dahdsr(name, parname, index) \
    DAHDSR_FIELDS(ENV_PARAM_OUTPUT, l->name, name, parname)
#define PROC_FIELDS_TO_FILEPTR_lfo(name, parname, index) \
    LFO_FIELDS(ENV_PARAM_OUTPUT, l->name, name, parname)

gchar *sampler_layer_to_string(struct sampler_layer *lr, gboolean show_inherited)
{
    struct sampler_layer_data *l = &lr->data;
    GString *outstr = g_string_sized_new(200);
    const char *tmpstr;
    if ((show_inherited || l->has_waveform) && l->waveform && l->waveform->display_name)
        g_string_append_printf(outstr, " sample=%s", l->waveform->display_name);
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_FILEPTR)
    
    static const char *addrandom_variants[] = { "amp", "fil", "pitch" };
    static const char *modsrc_names[] = { "chanaft", "vel", "polyaft", "pitch", "pitcheg", "fileg", "ampeg", "pitchlfo", "fillfo", "amplfo", "" };
    static const char *moddest_names[] = { "gain", "pitch", "cutoff", "resonance" };
    for(GSList *nif = l->nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *nd = nif->data;
        if (!nd->has_value && !show_inherited)
            continue;
        #define PROC_ENVSTAGE_NAME(name, index, def_value) #name, 
        static const char *env_stages[] = { DAHDSR_FIELDS(PROC_ENVSTAGE_NAME) };
        int v = nd->variant;
        
        if (nd->notefunc == sampler_nif_addrandom && v >= 0 && v <= 2)
            g_string_append_printf(outstr, " %s_random=%g", addrandom_variants[nd->variant], nd->param);
        else if (nd->notefunc == sampler_nif_vel2pitch)
            g_string_append_printf(outstr, " pitch_veltrack=%g", nd->param);
        else if (nd->notefunc == sampler_nif_cc2delay && v >= 0 && v < 120)
            g_string_append_printf(outstr, " delay_cc%d=%g", nd->variant, nd->param);
        else if (nd->notefunc == sampler_nif_vel2env && v >= 0 && (v & 15) < 6 && (v >> 4) < 3)
            g_string_append_printf(outstr, " %seg_vel2%s=%g", addrandom_variants[nd->variant >> 4], env_stages[1 + (v & 15)], nd->param);
    }
    for(GSList *mod = l->modulations; mod; mod = mod->next)
    {
        struct sampler_modulation *md = mod->data;
        if (!md->has_value && !show_inherited)
            continue;

        if (md->src2 == smsrc_none)
        {
            if (md->src < 120)
            {
                g_string_append_printf(outstr, " %s_cc%d=%g", moddest_names[md->dest], md->src, md->amount);
                continue;
            }
            if (md->src < 120 + sizeof(modsrc_names) / sizeof(modsrc_names[0]))
            {
                g_string_append_printf(outstr, " %s_%s=%g", moddest_names[md->dest], modsrc_names[md->src - 120], md->amount);
                continue;
            }
        }
        if ((md->src == smsrc_amplfo && md->dest == smdest_gain) ||
            (md->src == smsrc_fillfo && md->dest == smdest_cutoff) ||
            (md->src == smsrc_pitchlfo && md->dest == smdest_pitch))
        {
            switch(md->src2)
            {
            case smsrc_chanaft:
            case smsrc_polyaft:
                g_string_append_printf(outstr, " %slfo_depth%s=%g", moddest_names[md->dest], modsrc_names[md->src2 - 120], md->amount);
                continue;
            default:
                if (md->src2 < 120)
                {
                    g_string_append_printf(outstr, " %slfo_depthcc%d=%g", moddest_names[md->dest], md->src2, md->amount);
                    continue;
                }
                break;
            }
            break;
        }
        if ((md->src == smsrc_ampenv && md->dest == smdest_gain) ||
            (md->src == smsrc_filenv && md->dest == smdest_cutoff) ||
            (md->src == smsrc_pitchenv && md->dest == smdest_pitch))
        {
            if (md->src2 == smsrc_vel)
            {
                g_string_append_printf(outstr, " %seg_vel2depth=%g", moddest_names[md->dest], md->amount);
                continue;
            }
        }
        g_string_append_printf(outstr, " genericmod_from_%d_and_%d_to_%d=%g", md->src, md->src2, md->dest, md->amount);
    }

    if (lr->unknown_keys)
    {
        GHashTableIter hti;
        gchar *key, *value;
        g_hash_table_iter_init(&hti, lr->unknown_keys);
        while(g_hash_table_iter_next(&hti, (gpointer *)&key, (gpointer *)&value))
            g_string_append_printf(outstr, " %s=%s", key, value);
    }
    
    gchar *res = outstr->str;
    g_string_free(outstr, FALSE);
    return res;
}

void sampler_layer_dump(struct sampler_layer *l, FILE *f)
{
    gchar *str = sampler_layer_to_string(l, FALSE);
    fprintf(f, "%s\n", str);
}

void sampler_layer_data_destroy(struct sampler_layer_data *l)
{
    g_slist_free_full(l->nifs, g_free);
    g_slist_free_full(l->modulations, g_free);
    if (l->waveform)
        cbox_waveform_unref(l->waveform);
}

void sampler_layer_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct sampler_layer *l = CBOX_H2O(objhdr);
    struct sampler_program *prg = l->parent_program;
    struct sampler_module *m = l->module;
    assert(g_hash_table_size(l->child_layers) == 0);

    if (l->parent_group)
    {
        g_hash_table_remove(l->parent_group->child_layers, l);
        if (prg && prg->rll)
        {
            sampler_program_delete_layer(prg, l);
            sampler_update_program_layers(m, prg);
        }
        l->parent_group = NULL;
    }
    sampler_layer_data_destroy(&l->data);
    if (l->runtime)
        sampler_layer_data_destroy(l->runtime);
    if (l->unknown_keys)
        g_hash_table_destroy(l->unknown_keys);

    free(l);
}
