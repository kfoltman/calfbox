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
#include <locale.h>

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
            sampler_layer_update(layer);
            sampler_program_update_layers(layer->parent_program);
            return TRUE;
        }
        return FALSE;
    }
    if (!strcmp(cmd->command, "/new_region") && !strcmp(cmd->arg_types, ""))
    {
        // XXXKF needs a string argument perhaps
        if (layer->parent_group)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create a region within a region");
            return FALSE;
        }
        struct sampler_layer *l = sampler_layer_new(layer->module, layer->parent_program, layer);
        sampler_layer_data_finalize(&l->data, l->parent_group ? &l->parent_group->data : NULL, layer->parent_program);
        sampler_layer_reset_switches(l, l->module);
        sampler_layer_update(l);

        sampler_program_add_layer(layer->parent_program, l);
        sampler_program_update_layers(layer->parent_program);
        
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
#define PROC_FIELDS_INITIALISER_string(name) \
    ld->name = NULL; \
    ld->name##_changed = FALSE; \
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
#define PROC_FIELDS_INITIALISER_eq(name, parname, index) \
    EQ_FIELDS(PROC_SUBSTRUCT_RESET_FIELD, name, ld); \
    EQ_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name, ld)
#define PROC_FIELDS_INITIALISER_ccrange(name) \
    ld->name##locc = 0; \
    ld->name##hicc = 127; \
    ld->name##cc_number = 0; \
    ld->has_##name##locc = 0; \
    ld->has_##name##hicc = 0;

CBOX_CLASS_DEFINITION_ROOT(sampler_layer)

struct sampler_layer *sampler_layer_new(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group)
{
    struct sampler_layer *l = calloc(1, sizeof(struct sampler_layer));
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
        g_hash_table_replace(parent_group->child_layers, l, l);
        l->runtime = NULL;
        CBOX_OBJECT_REGISTER(l);
        return l;
    }
    l->parent_program = parent_program;

    struct sampler_layer_data *ld = &l->data;    
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_INITIALISER)
    
    ld->eff_waveform = NULL;
    ld->eff_freq = 44100;
    ld->velcurve[0] = 0;
    ld->velcurve[127] = 1;
    for (int i = 1; i < 127; i++)
        ld->velcurve[i] = -1;
    ld->modulations = NULL;
    ld->nifs = NULL;
    ld->on_locc = 0;
    ld->on_hicc = 127;
    ld->on_cc_number = -1;

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
#define PROC_FIELDS_CLONE_string(name) \
    dst->name = src->name ? g_strdup(src->name) : NULL; \
    dst->name##_changed = src->name##_changed; \
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
#define PROC_FIELDS_CLONE_eq(name, parname, index) \
        EQ_FIELDS(PROC_SUBSTRUCT_CLONE, name, dst, src) \
        if (!copy_hasattr) \
            EQ_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name, dst)
#define PROC_FIELDS_CLONE_ccrange(name) \
    dst->name##locc = src->name##locc; \
    dst->name##hicc = src->name##hicc; \
    dst->name##cc_number = src->name##cc_number; \
    dst->has_##name##locc = copy_hasattr ? src->has_##name##locc : FALSE; \
    dst->has_##name##hicc = copy_hasattr ? src->has_##name##hicc : FALSE;

void sampler_layer_data_clone(struct sampler_layer_data *dst, const struct sampler_layer_data *src, gboolean copy_hasattr)
{
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
    dst->eff_waveform = src->eff_waveform;
    if (dst->eff_waveform)
        cbox_waveform_ref(dst->eff_waveform);
}

#define PROC_FIELDS_CLONEPARENT(type, name, def_value) \
    if (!l->has_##name) \
        l->name = parent ? parent->name : def_value;
#define PROC_FIELDS_CLONEPARENT_string(name) \
    if (!l->has_##name && (!l->name || !parent->name || strcmp(l->name, parent->name))) { \
        g_free(l->name); \
        l->name = parent && parent->name ? g_strdup(parent->name) : NULL; \
        l->name##_changed = parent && parent->name##_changed; \
    }
#define PROC_FIELDS_CLONEPARENT_dBamp PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_enum PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_dahdsr(name, parname, index) \
        DAHDSR_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_lfo(name, parname, index) \
        LFO_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_eq(name, parname, index) \
        EQ_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_ccrange(name) \
    if (!l->has_##name##locc) \
        l->name##locc = parent ? parent->name##locc : 0; \
    if (!l->has_##name##hicc) \
        l->name##hicc = parent ? parent->name##hicc : 127; \
    if (!l->has_##name##locc && !l->has_##name##hicc) \
        l->name##cc_number = parent ? parent->name##cc_number : -1;

static void sampler_layer_data_getdefaults(struct sampler_layer_data *l, struct sampler_layer_data *parent)
{
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

#define PROC_FIELDS_FINALISER(type, name, def_value) 
#define PROC_FIELDS_FINALISER_string(name)
#define PROC_FIELDS_FINALISER_enum(type, name, def_value) 
#define PROC_FIELDS_FINALISER_dBamp(type, name, def_value) \
    l->name##_linearized = dB2gain(l->name);
#define PROC_FIELDS_FINALISER_dahdsr(name, parname, index) \
    cbox_envelope_init_dahdsr(&l->name##_shape, &l->name, m->module.srate / CBOX_BLOCK_SIZE, 100.f, &l->name##_shape == &l->amp_env_shape);
#define PROC_FIELDS_FINALISER_lfo(name, parname, index) /* no finaliser required */
#define PROC_FIELDS_FINALISER_eq(name, parname, index) l->name.effective_freq = (l->name.freq ? l->name.freq : 5 * powf(10.f, 1 + (index)));
#define PROC_FIELDS_FINALISER_ccrange(name) /* no finaliser required */

void sampler_layer_data_finalize(struct sampler_layer_data *l, struct sampler_layer_data *parent, struct sampler_program *p)
{
    struct sampler_module *m = p->module;
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_FINALISER)

    sampler_layer_data_getdefaults(l, parent);
    
    // Handle change of sample in the prent group without override on region level
    if (parent && (l->sample_changed || parent->sample_changed))
    {
        struct cbox_waveform *oldwf = l->eff_waveform;
        if (l->sample && *l->sample)
        {
            GError *error = NULL;
            l->eff_waveform = cbox_wavebank_get_waveform(p->name, p->tarfile, p->sample_dir, l->sample, &error);
            if (!l->eff_waveform)
            {
                g_warning("Cannot load waveform %s: %s", l->sample, error ? error->message : "unknown error");
                g_error_free(error);
            }
        }
        else
            l->eff_waveform = NULL;
        if (oldwf)
            cbox_waveform_unref(oldwf);
        l->sample_changed = FALSE;
    }
    
    l->eff_freq = (l->eff_waveform && l->eff_waveform->info.samplerate) ? l->eff_waveform->info.samplerate : 44100;
    l->eff_loop_mode = l->loop_mode;
    if (l->loop_mode == slm_unknown)
    {
        if (l->eff_waveform && l->eff_waveform->has_loop)
            l->eff_loop_mode = slm_loop_continuous;
        else
        if (l->eff_waveform)
            l->eff_loop_mode = l->loop_end == 0 ? slm_no_loop : slm_loop_continuous;
    }
    
    if (l->eff_loop_mode == slm_one_shot || l->eff_loop_mode == slm_no_loop || l->eff_loop_mode == slm_one_shot_chokeable)
        l->loop_start = SAMPLER_NO_LOOP;

    if ((l->eff_loop_mode == slm_loop_continuous || l->eff_loop_mode == slm_loop_sustain) && l->loop_start == SAMPLER_NO_LOOP)
        l->loop_start = 0;
    if ((l->eff_loop_mode == slm_loop_continuous || l->eff_loop_mode == slm_loop_sustain) && l->loop_start == 0 && l->eff_waveform && l->eff_waveform->has_loop)
        l->loop_start = l->eff_waveform->loop_start;
    if (l->loop_end == 0 && l->eff_waveform != NULL)
        l->loop_end = l->eff_waveform->has_loop ? l->eff_waveform->loop_end : l->eff_waveform->info.frames;

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

    // 'linearize' the virtual circular buffer - write 3 (or N) frames before end of the loop
    // and 3 (N) frames at the start of the loop, and play it; in rare cases this will need to be
    // repeated twice if output write pointer is close to CBOX_BLOCK_SIZE or playback rate is very low,
    // but that's OK.
    if (l->eff_waveform && l->eff_waveform->preloaded_frames == (size_t)l->eff_waveform->info.frames)
    {
        int shift = l->eff_waveform->info.channels == 2 ? 1 : 0;
        uint32_t halfscratch = MAX_INTERPOLATION_ORDER << shift;
        memcpy(&l->scratch_loop[0], &l->eff_waveform->data[(l->loop_end - MAX_INTERPOLATION_ORDER) << shift], halfscratch * sizeof(int16_t) );
        memcpy(&l->scratch_end[0], &l->eff_waveform->data[(l->loop_end - MAX_INTERPOLATION_ORDER) << shift], halfscratch * sizeof(int16_t) );
        memset(l->scratch_end + halfscratch, 0, halfscratch * sizeof(int16_t));
        if (l->loop_start != (uint32_t)-1)
            memcpy(l->scratch_loop + halfscratch, &l->eff_waveform->data[l->loop_start << shift], halfscratch * sizeof(int16_t));
        else
            memset(l->scratch_loop + halfscratch, 0, halfscratch * sizeof(int16_t));
    }
    if (sampler_layer_data_is_4pole(l))
        l->resonance_scaled = sqrtf(l->resonance_linearized / 0.707f) * 0.5f;
    else
        l->resonance_scaled = l->resonance_linearized;
    if (l->cutoff < 20)
        l->logcutoff = -1;
    else
        l->logcutoff = 1200.0 * log(l->cutoff / 440.0) / log(2) + 5700.0;
    
    l->eq_bitmask = ((l->eq1.gain != 0 || l->eq1.vel2gain != 0) ? 1 : 0)
        | ((l->eq2.gain != 0 || l->eq2.vel2gain != 0) ? 2 : 0)
        | ((l->eq3.gain != 0 || l->eq3.vel2gain != 0) ? 4 : 0);
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
    // layer%d should be ignored, it's handled by sampler_program_new_from_cfg
    if (!strncmp(key, "layer", 5) && isdigit(key[5]))
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

struct sampler_layer *sampler_layer_new_from_section(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group, const char *cfg_section)
{
    struct sampler_layer *l = sampler_layer_new(m, parent_program, parent_group ? parent_group : parent_program->default_group);
    sampler_layer_load_overrides(l, cfg_section);
    sampler_layer_data_finalize(&l->data, l->parent_group ? &l->parent_group->data : NULL, parent_program);
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

static double atof_C(const char *value)
{
    return g_ascii_strtod(value, NULL);
}

static gboolean parse_envelope_param(struct sampler_layer *layer, struct cbox_dahdsr *env, struct sampler_dahdsr_has_fields *has_fields, int env_type, const char *key, const char *value)
{
    static const enum sampler_modsrc srcs[] = { smsrc_ampenv, smsrc_filenv, smsrc_pitchenv };
    static const enum sampler_moddest dests[] = { smdest_gain, smdest_cutoff, smdest_pitch };
    enum sampler_modsrc src = srcs[env_type];
    enum sampler_moddest dest = dests[env_type];
    float fvalue = atof_C(value);
    
#define PROC_SET_ENV_FIELD(name, index, def_value) \
        if (!strcmp(key, #name)) {\
            env->name = fvalue; \
            has_fields->name = 1; \
            return TRUE; \
        }
    DAHDSR_FIELDS(PROC_SET_ENV_FIELD)
    if (!strcmp(key, "depth"))
        sampler_layer_set_modulation1(layer, src, dest, atof_C(value), 0);
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
        sampler_layer_set_modulation(layer, src, smsrc_vel, dest, atof_C(value), 0);
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
    float fvalue = atof_C(value);
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

static gboolean parse_eq_param(struct sampler_layer *layer, struct sampler_eq_params *params, struct sampler_eq_has_fields *has_fields, int eq_index, const char *key, const char *value)
{
#define PROC_SET_EQ_FIELD(name, index, def_value) \
        if (!strcmp(key, #name)) {\
            params->name = fvalue; \
            has_fields->name = 1; \
            return TRUE; \
        }
    float fvalue = atof_C(value);
    EQ_FIELDS(PROC_SET_EQ_FIELD)
    return FALSE;
}

#define PARSE_PARAM_midi_note_t(field, strname, valuestr) \
    return ((l->data.field = sfz_note_from_string(value)), (l->data.has_##field = 1));
#define PARSE_PARAM_int(field, strname, valuestr) \
    return ((l->data.field = atoi(value)), (l->data.has_##field = 1));
#define PARSE_PARAM_uint32_t(field, strname, valuestr) \
    return ((l->data.field = (uint32_t)strtoul(value, NULL, 10)), (l->data.has_##field = 1));
#define PARSE_PARAM_float(field, strname, valuestr) \
    return ((l->data.field = atof_C(value)), (l->data.has_##field = 1));

#define PROC_APPLY_PARAM(type, name, def_value) \
    if (!strcmp(key, #name)) { \
        PARSE_PARAM_##type(name, #name, value) \
    }
#define PROC_APPLY_PARAM_string(name) \
    if (!strcmp(key, #name)) { \
        if (l->data.name && value && !strcmp(l->data.name, value)) \
            return (l->data.has_##name = 1); \
        g_free(l->data.name); \
        return ((l->data.name = g_strdup(value)), (l->data.name##_changed = 1), (l->data.has_##name = 1)); \
    }
#define PROC_APPLY_PARAM_dBamp(type, name, def_value) \
    if (!strcmp(key, #name)) { \
        return ((l->data.name = atof_C(value)), (l->data.has_##name = 1)); \
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
#define PROC_APPLY_PARAM_eq(name, parname, index) \
    if (!strncmp(key, #parname "_", sizeof(#parname))) \
        return parse_eq_param(l, &l->data.name, &l->data.has_##name, index, key + sizeof(#parname), value);
#define PROC_APPLY_PARAM_ccrange(name)  /* handled separately in apply_param */

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
    
    if (!strcmp(key, "lolev"))
        l->data.lovel = atoi(value), l->data.has_lovel = 1;
    else if (!strcmp(key, "hilev"))
        l->data.hivel = atoi(value), l->data.has_hivel = 1;
    else if (!strcmp(key, "benddown"))
        l->data.bend_down = atoi(value), l->data.has_bend_down = 1;
    else if (!strcmp(key, "bendup"))
        l->data.bend_up = atoi(value), l->data.has_bend_up = 1;
    else if (!strcmp(key, "loopstart"))
        l->data.loop_start = atoi(value), l->data.has_loop_start = 1;
    else if (!strcmp(key, "loopend"))
        l->data.loop_end = atoi(value), l->data.has_loop_end = 1;
    else if (!strcmp(key, "cutoff_chanaft"))
        sampler_layer_set_modulation1(l, smsrc_chanaft, smdest_cutoff, atof_C(value), 0);
    else if (!strcmp(key, "amp_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 0, atof_C(value));
    else if (!strcmp(key, "fil_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 1, atof_C(value));
    else if (!strcmp(key, "pitch_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 2, atof_C(value));
    else if (!strcmp(key, "pitch_veltrack"))
        sampler_layer_add_nif(l, sampler_nif_vel2pitch, 0, atof_C(value));
    else if (!strcmp(key, "reloffset_veltrack"))
        sampler_layer_add_nif(l, sampler_nif_vel2reloffset, 0, atof_C(value));
    else if (!strncmp(key, "delay_cc", 8))
    {
        int ccno = atoi(key + 8);
        if (ccno > 0 && ccno < 120)
            sampler_layer_add_nif(l, sampler_nif_cc2delay, ccno, atof_C(value));
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "reloffset_cc", 12))
    {
        int ccno = atoi(key + 12);
        if (ccno > 0 && ccno < 120)
            sampler_layer_add_nif(l, sampler_nif_cc2reloffset, ccno, atof_C(value));
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "cutoff_cc", 9))
    {
        int ccno = atoi(key + 9);
        if (ccno > 0 && ccno < 120)
            sampler_layer_set_modulation1(l, ccno, smdest_cutoff, atof_C(value), 0);
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "pitch_cc", 8))
    {
        int ccno = atoi(key + 8);
        if (ccno > 0 && ccno < 120)
            sampler_layer_set_modulation1(l, ccno, smdest_pitch, atof_C(value), 0);
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "tonectl_cc", 10))
    {
        int ccno = atoi(key + 10);
        if (ccno > 0 && ccno < 120)
            sampler_layer_set_modulation1(l, ccno, smdest_tonectl, atof_C(value), 0);
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "gain_cc", 7))
    {
        int ccno = atoi(key + 7);
        if (ccno > 0 && ccno < 120)
            sampler_layer_set_modulation1(l, ccno, smdest_gain, atof_C(value), 0);
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
            l->data.velcurve[point] = atof_C(value);
            if (l->data.velcurve[point] < 0)
                l->data.velcurve[point] = 0;
            if (l->data.velcurve[point] > 1)
                l->data.velcurve[point] = 1;
        }
        else
            goto unknown_key;
    }
    else if (!strncmp(key, "on_locc", 7) || !strncmp(key, "on_hicc", 7))
    {
        int cc = atoi(key + 7);
        if (cc > 0 && cc < 120)
        {
            if (*value)
            {
                l->data.on_cc_number = cc;
                if (key[3] == 'l')
                {
                    l->data.on_locc = atoi(value);
                    l->data.has_on_locc = TRUE;
                }
                else
                {
                    l->data.on_hicc = atoi(value);
                    l->data.has_on_hicc = TRUE;
                }
            }
            else
            {
                l->data.on_cc_number = -1;
                l->data.has_on_locc = FALSE;
                l->data.has_on_hicc = FALSE;
            }
        }
        else
            return FALSE;
    }
    else if (!strncmp(key, "locc", 4) || !strncmp(key, "hicc", 4))
    {
        int cc = atoi(key + 4);
        if (cc > 0 && cc < 120)
        {
            if (*value)
            {
                l->data.cc_number = cc;
                if (key[0] == 'l')
                {
                    l->data.locc = atoi(value);
                    l->data.has_locc = TRUE;
                }
                else
                {
                    l->data.hicc = atoi(value);
                    l->data.has_hicc = TRUE;
                }
            }
            else
            {
                l->data.cc_number = -1;
                l->data.has_locc = FALSE;
                l->data.has_hicc = FALSE;
            }
        }
        else
            return FALSE;
    }
    else if (!strcmp(key, "loopmode"))
    {
        key = "loop_mode";
        goto try_now; // yes, goto, why not?
    }
    else if (!strcmp(key, "offby"))
    {
        key = "off_by";
        goto try_now;
    }
    else if (!strncmp(key, "genericmod_", 11))
    {
        char **tokens = g_strsplit(key, "_", 5);
        sampler_layer_set_modulation(l, atoi(tokens[1]), atoi(tokens[2]), atoi(tokens[3]), atof(value), atoi(tokens[4]));
        g_strfreev(tokens);
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
        g_string_append_printf(outstr, " %s=%s", #name, g_ascii_dtostr(floatbuf, floatbufsize, l->name));

#define PROC_FIELDS_TO_FILEPTR(type, name, def_value) \
    TYPE_PRINTF_##type(name, def_value)
#define PROC_FIELDS_TO_FILEPTR_string(name) \
    if (show_inherited || l->has_##name) \
        g_string_append_printf(outstr, " %s=%s", #name, l->name ? l->name : "");
#define PROC_FIELDS_TO_FILEPTR_dBamp(type, name, def_value) \
    if (show_inherited || l->has_##name) \
        g_string_append_printf(outstr, " %s=%s", #name, g_ascii_dtostr(floatbuf, floatbufsize, l->name));
#define PROC_FIELDS_TO_FILEPTR_enum(enumtype, name, def_value) \
    if ((show_inherited || l->has_##name) && (tmpstr = enumtype##_to_string(l->name)) != NULL) \
        g_string_append_printf(outstr, " %s=%s", #name, tmpstr);

#define ENV_PARAM_OUTPUT(param, index, def_value, env, envfield, envname) \
    if (show_inherited || l->has_##envfield.param) \
        g_string_append_printf(outstr, " " #envname "_" #param "=%s", g_ascii_dtostr(floatbuf, floatbufsize, env.param));

#define PROC_FIELDS_TO_FILEPTR_dahdsr(name, parname, index) \
    DAHDSR_FIELDS(ENV_PARAM_OUTPUT, l->name, name, parname)
#define PROC_FIELDS_TO_FILEPTR_lfo(name, parname, index) \
    LFO_FIELDS(ENV_PARAM_OUTPUT, l->name, name, parname)
#define PROC_FIELDS_TO_FILEPTR_eq(name, parname, index) \
    EQ_FIELDS(ENV_PARAM_OUTPUT, l->name, name, parname)
#define PROC_FIELDS_TO_FILEPTR_ccrange(name) \
    if (l->has_##name##locc) \
        g_string_append_printf(outstr, " " #name "locc%d=%d", l->name##cc_number, l->name##locc); \
    if (l->has_##name##hicc) \
        g_string_append_printf(outstr, " " #name "hicc%d=%d", l->name##cc_number, l->name##hicc);

gchar *sampler_layer_to_string(struct sampler_layer *lr, gboolean show_inherited)
{
    struct sampler_layer_data *l = &lr->data;
    GString *outstr = g_string_sized_new(200);
    const char *tmpstr;
    char floatbuf[G_ASCII_DTOSTR_BUF_SIZE];
    int floatbufsize = G_ASCII_DTOSTR_BUF_SIZE;
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_FILEPTR)
    
    static const char *addrandom_variants[] = { "amp", "fil", "pitch" };
    static const char *modsrc_names[] = { "chanaft", "vel", "polyaft", "pitch", "pitcheg", "fileg", "ampeg", "pitchlfo", "fillfo", "amplfo", "" };
    static const char *moddest_names[] = { "gain", "pitch", "cutoff", "resonance", "tonectl" };
    for(GSList *nif = l->nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *nd = nif->data;
        if (!nd->has_value && !show_inherited)
            continue;
        #define PROC_ENVSTAGE_NAME(name, index, def_value) #name, 
        static const char *env_stages[] = { DAHDSR_FIELDS(PROC_ENVSTAGE_NAME) };
        int v = nd->variant;
        g_ascii_dtostr(floatbuf, floatbufsize, nd->param);
        
        if (nd->notefunc == sampler_nif_addrandom && v >= 0 && v <= 2)
            g_string_append_printf(outstr, " %s_random=%s", addrandom_variants[nd->variant], floatbuf);
        else if (nd->notefunc == sampler_nif_vel2pitch)
            g_string_append_printf(outstr, " pitch_veltrack=%s", floatbuf);
        else if (nd->notefunc == sampler_nif_vel2reloffset)
            g_string_append_printf(outstr, " reloffset_veltrack=%s", floatbuf);
        else if (nd->notefunc == sampler_nif_cc2delay && v >= 0 && v < 120)
            g_string_append_printf(outstr, " delay_cc%d=%s", nd->variant, floatbuf);
        else if (nd->notefunc == sampler_nif_cc2reloffset && v >= 0 && v < 120)
            g_string_append_printf(outstr, " reloffset_cc%d=%s", nd->variant, floatbuf);
        else if (nd->notefunc == sampler_nif_vel2env && v >= 0 && (v & 15) < 6 && (v >> 4) < 3)
            g_string_append_printf(outstr, " %seg_vel2%s=%s", addrandom_variants[nd->variant >> 4], env_stages[1 + (v & 15)], floatbuf);
    }
    for(GSList *mod = l->modulations; mod; mod = mod->next)
    {
        struct sampler_modulation *md = mod->data;
        if (!md->has_value && !show_inherited)
            continue;
        g_ascii_dtostr(floatbuf, floatbufsize, md->amount);

        if (md->src2 == smsrc_none)
        {
            if (md->src < 120)
            {
                g_string_append_printf(outstr, " %s_cc%d=%s", moddest_names[md->dest], md->src, floatbuf);
                continue;
            }
            if (md->src < 120 + sizeof(modsrc_names) / sizeof(modsrc_names[0]))
            {
                if ((md->src == smsrc_filenv && md->dest == smdest_cutoff) ||
                    (md->src == smsrc_pitchenv && md->dest == smdest_pitch) ||
                    (md->src == smsrc_fillfo && md->dest == smdest_cutoff) ||
                    (md->src == smsrc_pitchlfo && md->dest == smdest_pitch))
                    g_string_append_printf(outstr, " %s_depth=%s", modsrc_names[md->src - 120], floatbuf);
                else
                    g_string_append_printf(outstr, " %s_%s=%s", moddest_names[md->dest], modsrc_names[md->src - 120], floatbuf);
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
                g_string_append_printf(outstr, " %slfo_depth%s=%s", moddest_names[md->dest], modsrc_names[md->src2 - 120], floatbuf);
                continue;
            default:
                if (md->src2 < 120)
                {
                    g_string_append_printf(outstr, " %slfo_depthcc%d=%s", moddest_names[md->dest], md->src2, floatbuf);
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
                g_string_append_printf(outstr, " %seg_vel2depth=%s", moddest_names[md->dest], floatbuf);
                continue;
            }
            if (md->src2 < 120)
            {
                g_string_append_printf(outstr, " %s_depthcc%d=%s", modsrc_names[md->src - 120], md->src2, floatbuf);
                continue;
            }
        }
        g_string_append_printf(outstr, " genericmod_%d_%d_%d_%d=%s", md->src, md->src2, md->dest, md->flags, floatbuf);
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

void sampler_layer_data_close(struct sampler_layer_data *l)
{
    g_slist_free_full(l->nifs, g_free);
    g_slist_free_full(l->modulations, g_free);
    if (l->eff_waveform)
    {
        cbox_waveform_unref(l->eff_waveform);
        l->eff_waveform = NULL;
    }
    g_free(l->sample);
}

void sampler_layer_data_destroy(struct sampler_layer_data *l)
{
    sampler_layer_data_close(l);
    free(l);
}

struct sampler_layer *sampler_layer_new_clone(struct sampler_layer *layer,
    struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent_group)
{
    struct sampler_layer *l = sampler_layer_new(m, parent_program, parent_group);
    sampler_layer_data_clone(&l->data, &layer->data, TRUE);
    if (layer->unknown_keys)
    {
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, layer->unknown_keys);
        gpointer key, value;
        while(g_hash_table_iter_next(&iter, &key, &value))
            sampler_layer_apply_param(l, (gchar *)key, (gchar *)value, NULL);
    }

    GHashTableIter iter;
    g_hash_table_iter_init(&iter, layer->child_layers);
    gpointer key, value;
    while(g_hash_table_iter_next(&iter, &key, &value))
    {
        sampler_layer_new_clone(key, m, parent_program, l);
        // g_hash_table_insert(l->child_layers, chl, NULL);
    }

    return l;
}

void sampler_layer_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct sampler_layer *l = CBOX_H2O(objhdr);
    struct sampler_program *prg = l->parent_program;
    assert(g_hash_table_size(l->child_layers) == 0);

    if (l->parent_group)
    {
        g_hash_table_remove(l->parent_group->child_layers, l);
        if (prg && prg->rll)
        {
            sampler_program_delete_layer(prg, l);
            sampler_program_update_layers(l->parent_program);
        }
        l->parent_group = NULL;
    }
    sampler_layer_data_close(&l->data);
    if (l->runtime)
        sampler_layer_data_destroy(l->runtime);
    if (l->unknown_keys)
        g_hash_table_destroy(l->unknown_keys);
    if (l->child_layers)
        g_hash_table_destroy(l->child_layers);

    free(l);
}

//////////////////////////////////////////////////////////////////////////

struct sampler_layer_update_cmd
{
    struct sampler_module *module;
    struct sampler_layer *layer;
    struct sampler_layer_data *new_data;
    struct sampler_layer_data *old_data;
};

static int sampler_layer_update_cmd_prepare(void *data)
{
    struct sampler_layer_update_cmd *cmd = data;
    cmd->old_data = cmd->layer->runtime;
    cmd->new_data = calloc(1, sizeof(struct sampler_layer_data));
    
    sampler_layer_data_clone(cmd->new_data, &cmd->layer->data, TRUE);
    sampler_layer_data_finalize(cmd->new_data, cmd->layer->parent_group ? &cmd->layer->parent_group->data : NULL, cmd->layer->parent_program);
    if (cmd->layer->runtime == NULL)
    {
        // initial update of the layer, so none of the voices need updating yet
        // because the layer hasn't been allocated to any voice
        cmd->layer->runtime = cmd->new_data;
        return 1;
    }
    return 0;
}

static int sampler_layer_update_cmd_execute(void *data)
{
    struct sampler_layer_update_cmd *cmd = data;
    
    for (int i = 0; i < 16; i++)
    {
        FOREACH_VOICE(cmd->module->channels[i].voices_running, v)
        {
            if (v->layer == cmd->layer->runtime)
            {
                v->layer = cmd->new_data;
                v->layer_changed = TRUE;
                sampler_voice_update_params_from_layer(v);
            }
        }
    }
    cmd->old_data = cmd->layer->runtime;
    cmd->layer->runtime = cmd->new_data;
    return 10;
}

static void sampler_layer_update_cmd_cleanup(void *data)
{
    struct sampler_layer_update_cmd *cmd = data;
    
    sampler_layer_data_destroy(cmd->old_data);
    free(cmd);
}

void sampler_layer_update(struct sampler_layer *l)
{
    // if changing a group, update all child regions instead
    if (g_hash_table_size(l->child_layers))
    {
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, l->child_layers);
        gpointer key, value;
        while(g_hash_table_iter_next(&iter, &key, &value))
        {
            sampler_layer_data_finalize(&((struct sampler_layer *)key)->data, &l->data, l->parent_program);
            sampler_layer_update((struct sampler_layer *)key);
        }
        return;
    }
    static struct cbox_rt_cmd_definition rtcmd = {
        .prepare = sampler_layer_update_cmd_prepare,
        .execute = sampler_layer_update_cmd_execute,
        .cleanup = sampler_layer_update_cmd_cleanup,
    };
    
    struct sampler_layer_update_cmd *lcmd = malloc(sizeof(struct sampler_layer_update_cmd));
    lcmd->module = l->module;
    lcmd->layer = l;
    lcmd->new_data = NULL;
    lcmd->old_data = NULL;
    
    cbox_rt_execute_cmd_async(l->module->module.rt, &rtcmd, lcmd);
}

