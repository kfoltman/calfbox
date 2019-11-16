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
#include "sampler_impl.h"
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

void sampler_layer_data_dump_modulations(struct sampler_layer_data *l)
{
    GSList *p = l->modulations;
    while(p)
    {
        struct sampler_modulation *sm = p->data;
        if (sm->has_value)
            printf("%d x %d -> %d : %f\n", sm->src, sm->src2, sm->dest, sm->amount);
        p = g_slist_next(p);
    }
}

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

static void sampler_layer_data_unset_modulation(struct sampler_layer_data *l, struct sampler_layer_data *parent_data, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, gboolean remove_propagated)
{
    GSList *p = l->modulations;
    while(p)
    {
        struct sampler_modulation *sm = p->data;
        if (sm->src == src && sm->src2 == src2 && sm->dest == dest && sm->has_value == !remove_propagated)
        {
            if (!remove_propagated && parent_data) {
                // Try to copy over from parent
                GSList *q = parent_data->modulations;
                while(q)
                {
                    struct sampler_modulation *psm = q->data;
                    if (psm->src == src && psm->src2 == src2 && psm->dest == dest && psm->has_value)
                    {
                        memcpy(sm, psm, sizeof(*sm));
                        sm->has_value = FALSE;
                        return;
                    }
                    q = g_slist_next(q);
                }
            }
            // No parent value, just delete
            l->modulations = g_slist_delete_link(l->modulations, p);
            return;
        }
        p = g_slist_next(p);
    }
#if 0
    printf("modulation not found\n");
    printf("Looking for: %d x %d -> %d\n", src, src2, dest);
    sampler_layer_data_dump_modulations(l);
#endif
}

void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags)
{
    sampler_layer_data_set_modulation(&l->data, src, src2, dest, amount, flags, FALSE);
}

void sampler_layer_unset_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, gboolean remove_propagated)
{
    sampler_layer_data_unset_modulation(&l->data, (!remove_propagated && l->parent_group) ? &l->parent_group->data : NULL, src, src2, dest, remove_propagated);

    if (l->child_layers) {
        // Also recursively remove propagated copies from child layers, if any
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, l->child_layers);
        gpointer key, value;
        while(g_hash_table_iter_next(&iter, &key, &value))
        {
            struct sampler_layer *child = value;
            sampler_layer_unset_modulation(child, src, src2, dest, TRUE);
        }
    }
}

void sampler_layer_data_add_nif(struct sampler_layer_data *l, SamplerNoteInitFunc notefunc_voice, SamplerNoteInitFunc2 notefunc_prevoice, int variant, float param, gboolean propagating_defaults)
{
    assert(!(notefunc_voice && notefunc_prevoice));
    GSList **list = notefunc_voice ? &l->voice_nifs : &l->prevoice_nifs;
    GSList *p = *list;
    while(p)
    {
        struct sampler_noteinitfunc *nif = p->data;
        if (nif->notefunc_voice == notefunc_voice && nif->notefunc_prevoice == notefunc_prevoice && nif->variant == variant)
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
    nif->notefunc_voice = notefunc_voice;
    nif->notefunc_prevoice = notefunc_prevoice;
    nif->variant = variant;
    nif->param = param;
    nif->has_value = !propagating_defaults;
    *list = g_slist_prepend(*list, nif);
}

void sampler_layer_data_remove_nif(struct sampler_layer_data *l, struct sampler_layer_data *parent_data, SamplerNoteInitFunc notefunc_voice, SamplerNoteInitFunc2 notefunc_prevoice, int variant, gboolean remove_propagated)
{
    assert(!(notefunc_voice && notefunc_prevoice));
    GSList **list = notefunc_voice ? &l->voice_nifs : &l->prevoice_nifs;
    GSList *p = *list;
    while(p)
    {
        struct sampler_noteinitfunc *nif = p->data;
        if (nif->notefunc_voice == notefunc_voice && nif->notefunc_prevoice == notefunc_prevoice && nif->variant == variant && nif->has_value == !remove_propagated)
        {
            if (!remove_propagated && parent_data) {
                // Try to copy over from parent
                GSList *q = notefunc_voice ? parent_data->voice_nifs : parent_data->prevoice_nifs;
                while(q)
                {
                    struct sampler_noteinitfunc *pnif = q->data;
                    if (pnif->notefunc_voice == notefunc_voice && pnif->notefunc_prevoice == notefunc_prevoice && pnif->variant == variant && pnif->has_value)
                    {
                        memcpy(nif, pnif, sizeof(*pnif));
                        nif->has_value = FALSE;
                        return;
                    }
                    q = g_slist_next(q);
                }
            }
            *list = g_slist_delete_link(*list, p);
            return;
        }
        p = g_slist_next(p);
    }
}

void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc_voice, SamplerNoteInitFunc2 notefunc_prevoice, int variant, float param)
{
    sampler_layer_data_add_nif(&l->data, notefunc_voice, notefunc_prevoice, variant, param, FALSE);
}

void sampler_layer_remove_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc_voice, SamplerNoteInitFunc2 notefunc_prevoice, int variant, gboolean remove_propagated)
{
    sampler_layer_data_remove_nif(&l->data, !remove_propagated && l->parent_group ? &l->parent_group->data : NULL, notefunc_voice, notefunc_prevoice, variant, remove_propagated);

    if (l->child_layers) {
        // Also recursively remove propagated copies from child layers, if any
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, l->child_layers);
        gpointer key, value;
        while(g_hash_table_iter_next(&iter, &key, &value))
        {
            struct sampler_layer *child = value;
            sampler_layer_remove_nif(child, notefunc_voice, notefunc_prevoice, variant, TRUE);
        }
    }
}

enum sampler_layer_param_type
{
    slpt_invalid,
    slpt_alias,
    slpt_int,
    slpt_uint32_t,
    slpt_float,
    slpt_dBamp,
    slpt_midi_note_t,
    slpt_enum,
    slpt_string,
    slpt_midicurve,
    slpt_ccrange,
    // modulation matrix
    slpt_depthcc, // src * CC -> dest
    slpt_amountcc, // CC -> dest
    slpt_amount, // src -> dest
    slpt_modulation, // src1 * src2 -> dest
    slpt_generic_modulation,
    // note init functions
    slpt_voice_nif,
    slpt_prevoice_nif,
    slpt_voice_cc_nif,
    slpt_prevoice_cc_nif,
    slpt_reserved,
};

struct sampler_layer_param_entry
{
    const char *name;
    size_t offset;
    enum sampler_layer_param_type type;
    double def_value;
    uint32_t extra_int;
    void *extra_ptr;
    void (*set_has_value)(struct sampler_layer_data *, gboolean);
};

#define PROC_SUBSTRUCT_FIELD_SETHASFUNC(name, index, def_value, parent) \
    static void sampler_layer_data_##parent##_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##parent.name = value; }

#define PROC_FIELD_SETHASFUNC(type, name, default_value) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##name = value; }
#define PROC_FIELD_SETHASFUNC_string(name) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##name = value; }
#define PROC_FIELD_SETHASFUNC_dBamp(type, name, default_value) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##name = value; }
#define PROC_FIELD_SETHASFUNC_enum(type, name, default_value) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##name = value; }
#define PROC_FIELD_SETHASFUNC_dahdsr(field, name, default_value) \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_FIELD_SETHASFUNC, field)
#define PROC_FIELD_SETHASFUNC_lfo(field, name, default_value) \
    LFO_FIELDS(PROC_SUBSTRUCT_FIELD_SETHASFUNC, field)
#define PROC_FIELD_SETHASFUNC_eq(field, name, default_value) \
    EQ_FIELDS(PROC_SUBSTRUCT_FIELD_SETHASFUNC, field)
#define PROC_FIELD_SETHASFUNC_ccrange(name, parname) \
    static void sampler_layer_data_set_has_##name##_lo(struct sampler_layer_data *l, gboolean value) { l->name.has_locc = value; } \
    static void sampler_layer_data_set_has_##name##_hi(struct sampler_layer_data *l, gboolean value) { l->name.has_hicc = value; }
#define PROC_FIELD_SETHASFUNC_midicurve(name) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, uint32_t index, gboolean value) { l->has_##name[index] = value; }

SAMPLER_FIXED_FIELDS(PROC_FIELD_SETHASFUNC)

#define FIELD_AMOUNT(name, src, dest) \
    { name, -1, slpt_amount, 0, (smsrc_##src << 16) | smdest_##dest, NULL, NULL },
#define FIELD_AMOUNT_CC(name, dest) \
    { name, -1, slpt_amountcc, 0, smdest_##dest, NULL, NULL },
#define FIELD_VOICE_NIF(name, nif, variant) \
    { name, -1, slpt_voice_nif, 0, variant, nif, NULL },
#define FIELD_PREVOICE_NIF(name, nif, variant) \
    { name, -1, slpt_prevoice_nif, 0, variant, nif, NULL },
#define FIELD_VOICE_CC_NIF(name, nif) \
    { name, -1, slpt_voice_cc_nif, 0, 0, nif, NULL },
#define FIELD_PREVOICE_CC_NIF(name, nif) \
    { name, -1, slpt_prevoice_cc_nif, 0, 0, nif, NULL },
#define FIELD_ALIAS(alias, name) \
    { alias, -1, slpt_alias, 0, 0, name, NULL },

#define LOFS(field) offsetof(struct sampler_layer_data, field)
#define PROC_SUBSTRUCT_FIELD_DESCRIPTOR(name, index, def_value, parent, parent_name, parent_index, parent_struct) \
    { #parent_name "_" #name, offsetof(struct sampler_layer_data, parent) + offsetof(struct parent_struct, name), slpt_float, def_value, parent_index * 100 + index, NULL, sampler_layer_data_##parent##_set_has_##name }, \

#define PROC_SUBSTRUCT_FIELD_DESCRIPTOR_DAHDSR(name, index, def_value, parent, parent_name, parent_index, parent_struct) \
    { #parent_name "_" #name, offsetof(struct sampler_layer_data, parent) + offsetof(struct parent_struct, name), slpt_float, def_value, parent_index * 100 + index, NULL, sampler_layer_data_##parent##_set_has_##name }, \
    FIELD_VOICE_NIF(#parent_name "_vel2" #name, sampler_nif_vel2env, (parent_index << 4) + snif_env_##name)

#define PROC_FIELD_DESCRIPTOR(type, name, default_value) \
    { #name, LOFS(name), slpt_##type, default_value, 0, NULL, sampler_layer_data_set_has_##name },
#define PROC_FIELD_DESCRIPTOR_dBamp(type, name, default_value) \
    { #name, LOFS(name), slpt_##type, default_value, 0, NULL, sampler_layer_data_set_has_##name },
#define PROC_FIELD_DESCRIPTOR_string(name) \
    { #name, LOFS(name), slpt_string, 0, LOFS(name##_changed), NULL, sampler_layer_data_set_has_##name },
#define PROC_FIELD_DESCRIPTOR_enum(enumtype, name, default_value) \
    { #name, LOFS(name), slpt_enum, (double)(enum enumtype)default_value, 0, enumtype##_from_string, sampler_layer_data_set_has_##name },
#define PROC_FIELD_DESCRIPTOR_midicurve(name) \
    { #name "_#", LOFS(name), slpt_midicurve, 0, 0, (void *)sampler_layer_data_set_has_##name, NULL },

#define PROC_FIELD_DESCRIPTOR_dahdsr(field, name, index) \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR_DAHDSR, field, name, index, cbox_dahdsr) \
    FIELD_AMOUNT(#name "_depth", name, from_##name) \
    { #name "_depthcc#", -1, slpt_depthcc, 0, (smsrc_##name << 16) | (smdest_from_##name), NULL, NULL }, \
    { #name "_vel2depth", -1, slpt_modulation, 0, (smsrc_##name << 8) | (smsrc_vel << 20) | (smdest_from_##name), NULL, NULL },

#define PROC_FIELD_DESCRIPTOR_lfo(field, name, index) \
    LFO_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR, field, name, index, sampler_lfo_params) \
    FIELD_AMOUNT(#name "_depth", name, from_##name) \
    FIELD_AMOUNT(#name "_freqpolyaft", polyaft, name##_freq) \
    FIELD_AMOUNT(#name "_freqchanaft", chanaft, name##_freq) \
    FIELD_AMOUNT_CC(#name "_freqcc#", name##_freq) \
    { #name "_depthpolyaft", -1, slpt_modulation, 0, (smsrc_##name << 8) | (smsrc_polyaft << 20) | (smdest_from_##name), NULL, NULL }, \
    { #name "_depthchanaft", -1, slpt_modulation, 0, (smsrc_##name << 8) | (smsrc_chanaft << 20) | (smdest_from_##name), NULL, NULL }, \
    { #name "_depthcc#", -1, slpt_depthcc, 0, (smsrc_##name << 16) | (smdest_from_##name), NULL, NULL },

#define PROC_FIELD_DESCRIPTOR_eq(field, name, index) \
    EQ_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR, field, name, index, sampler_eq_params) \
    FIELD_AMOUNT_CC(#name "_freqcc#", name##_freq) \
    FIELD_AMOUNT_CC(#name "_bwcc#", name##_bw) \
    FIELD_AMOUNT_CC(#name "_gaincc#", name##_gain)

#define PROC_FIELD_DESCRIPTOR_ccrange(field, parname) \
    { #parname "locc#", LOFS(field), slpt_ccrange, 0, 0, NULL, sampler_layer_data_set_has_##field##_lo }, \
    { #parname "hicc#", LOFS(field), slpt_ccrange, 127, 1, NULL, sampler_layer_data_set_has_##field##_hi },

struct sampler_layer_param_entry sampler_layer_params[] = {
    SAMPLER_FIXED_FIELDS(PROC_FIELD_DESCRIPTOR)

    FIELD_AMOUNT("cutoff_chanaft", chanaft, cutoff)
    FIELD_AMOUNT("resonance_chanaft", chanaft, resonance)
    FIELD_AMOUNT("cutoff_polyaft", polyaft, cutoff)
    FIELD_AMOUNT("resonance_polyaft", polyaft, resonance)

    FIELD_AMOUNT_CC("gain_cc#", gain)
    FIELD_AMOUNT_CC("cutoff_cc#", cutoff)
    FIELD_AMOUNT_CC("resonance_cc#", resonance)
    FIELD_AMOUNT_CC("pitch_cc#", pitch)
    FIELD_AMOUNT_CC("tonectl_cc#", tonectl)

    FIELD_VOICE_NIF("amp_random", sampler_nif_addrandom, 0)
    FIELD_VOICE_NIF("fil_random", sampler_nif_addrandom, 1)
    FIELD_VOICE_NIF("pitch_random", sampler_nif_addrandom, 2)
    FIELD_VOICE_NIF("pitch_veltrack", sampler_nif_vel2pitch, 0)
    FIELD_VOICE_NIF("offset_veltrack", sampler_nif_vel2offset, 0)
    FIELD_VOICE_NIF("reloffset_veltrack", sampler_nif_vel2reloffset, 0)
    FIELD_PREVOICE_NIF("delay_random", sampler_nif_addrandomdelay, 0)
    FIELD_PREVOICE_CC_NIF("delay_cc#", sampler_nif_cc2delay)
    FIELD_VOICE_CC_NIF("reloffset_cc#", sampler_nif_cc2reloffset)
    FIELD_VOICE_CC_NIF("offset_cc#", sampler_nif_cc2offset)

    FIELD_ALIAS("hilev", "hivel")
    FIELD_ALIAS("lolev", "lovel")
    FIELD_ALIAS("loopstart", "loop_start")
    FIELD_ALIAS("loopend", "loop_end")
    FIELD_ALIAS("loopmode", "loop_mode")
    FIELD_ALIAS("bendup", "bend_up")
    FIELD_ALIAS("benddown", "bend_down")
    FIELD_ALIAS("offby", "off_by")

    { "genericmod_#_#_#_#", -1, slpt_generic_modulation, 0, 0, NULL, NULL },
};
#define NPARAMS (sizeof(sampler_layer_params) / sizeof(sampler_layer_params[0]))

static int compare_entries(const void *p1, const void *p2)
{
    const struct sampler_layer_param_entry *e1 = p1, *e2 = p2;
    return strcmp(e1->name, e2->name);
}

void sampler_layer_prepare_params(void)
{
    qsort(sampler_layer_params, NPARAMS, sizeof(struct sampler_layer_param_entry), compare_entries);
    for (size_t i = 0; i < NPARAMS; ++i)
    {
        struct sampler_layer_param_entry *e = &sampler_layer_params[i];
        if (e->type == slpt_alias)
        {
            struct sampler_layer_param_entry prototype;
            prototype.name = e->extra_ptr;
            void *found = bsearch(&prototype, sampler_layer_params, NPARAMS, sizeof(sampler_layer_params[0]), compare_entries);
            if (!found)
                printf("Alias %s redirects to non-existent name (%s)\n", e->name, prototype.name);
            assert(found);
            e->extra_ptr = found;
        }
    }
}

#define VERIFY_FLOAT_VALUE do { if (!atof_C_verify(e->name, value, &fvalue, error)) return FALSE; } while(0)

gboolean sampler_layer_param_entry_set_from_string(const struct sampler_layer_param_entry *e, struct sampler_layer *l, const char *value, const uint32_t *args, GError **error)
{
    void *p = ((uint8_t *)&l->data) + e->offset;
    uint32_t cc;
    double fvalue;

    switch(e->type)
    {
        case slpt_midi_note_t:
        {
            midi_note_t note = sfz_note_from_string(value);
            if (note < -1)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a valid note name for %s", value, e->name);
                return FALSE;
            }
            *((midi_note_t *)p) = note;
            e->set_has_value(&l->data, 1);
            return TRUE;
        }
        case slpt_int:
        {
            char *endptr;
            errno = 0;
            int number = strtol(value, &endptr, 10);
            if (errno || *endptr || endptr == value)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a correct integer value for %s", value, e->name);
                return FALSE;
            }
            *(int *)p = number;
            e->set_has_value(&l->data, 1);
            return TRUE;
        }
        case slpt_enum:
        {
            gboolean (*func)(const char *, uint32_t *value);
            func = e->extra_ptr;
            if (!func(value, (uint32_t *)p))
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a correct value for %s", value, e->name);
                return FALSE;
            }
            e->set_has_value(&l->data, 1);
            return TRUE;
        }
        case slpt_uint32_t:
        {
            char *endptr;
            errno = 0;
            uint32_t number = (uint32_t)strtoul(value, &endptr, 10);
            if (errno || *endptr || endptr == value || value[0] == '-')
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a correct unsigned integer value for %s", value, e->name);
                return FALSE;
            }
            *(uint32_t *)p = number;
            e->set_has_value(&l->data, 1);
            return TRUE;
        }
        case slpt_float:
        case slpt_dBamp:
            VERIFY_FLOAT_VALUE;
            *((float *)p) = fvalue;
            e->set_has_value(&l->data, 1);
            return TRUE;
        case slpt_string:
            {
                char **pc = p;
                if (*pc && !strcmp(*pc, value))
                    return TRUE; // no change
                free(*pc);
                *pc = g_strdup(value);
                e->set_has_value(&l->data, 1);
                gboolean *changed_ptr = (gboolean *)(((uint8_t *)&l->data) + e->extra_int);
                *changed_ptr = 1;
            }
            return TRUE;
        case slpt_midicurve:
            VERIFY_FLOAT_VALUE;
            if (args[0] >= 0 && args[0] <= 127)
            {
                ((float *)p)[args[0]] = fvalue;
                void (*sethasfunc)(struct sampler_layer_data *, uint32_t, gboolean value) = e->extra_ptr;
                sethasfunc(&l->data, args[0], 1);
            }
            else
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Curve entry index (%u) is out of range for %s", (unsigned)args[0], e->name);
                return FALSE;
            }
            return TRUE;
        case slpt_ccrange:
        {
            cc = args[0];
            char *endptr;
            errno = 0;
            int number = strtol(value, &endptr, 10);
            if (errno || *endptr || endptr == value || number < 0 || number > 127)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a correct control change value for %s", value, e->name);
                return FALSE;
            }
            struct sampler_cc_range *range = p;
            if (!range->has_locc && !range->has_hicc)
                range->cc_number = cc;
            else if (range->cc_number != cc)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Conflicting controller number for %s (%d vs previously used %d)", e->name, cc, (int)range->cc_number);
                return FALSE;
            }
            switch(e->extra_int) {
                case 0: range->locc = number; range->is_active = 1; break;
                case 1: range->hicc = number; range->is_active = 1; break;
                default: assert(0);
            }
            e->set_has_value(&l->data, 1);
            return TRUE;
        }
        case slpt_depthcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation(l, (e->extra_int >> 16), cc, (e->extra_int & 0xFFFF), fvalue, 0);
            return TRUE;
        case slpt_amountcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation(l, cc, smsrc_none, (e->extra_int & 0xFFFF), fvalue, 0);
            return TRUE;
        case slpt_amount:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_modulation(l, (e->extra_int >> 16), smsrc_none, (e->extra_int & 0xFFFF), fvalue, 0);
            return TRUE;
        case slpt_modulation:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_modulation(l, (e->extra_int >> 8) & 0xFFF, (e->extra_int >> 20), (e->extra_int & 0xFF), fvalue, 0);
            return TRUE;
        case slpt_generic_modulation:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_modulation(l, args[0], args[1], args[2], fvalue, args[3]);
            return TRUE;
        case slpt_voice_nif:
            VERIFY_FLOAT_VALUE;
            sampler_layer_add_nif(l, e->extra_ptr, NULL, e->extra_int, fvalue);
            return TRUE;
        case slpt_prevoice_nif:
            VERIFY_FLOAT_VALUE;
            sampler_layer_add_nif(l, NULL, e->extra_ptr, e->extra_int, fvalue);
            return TRUE;
        case slpt_voice_cc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_add_nif(l, e->extra_ptr, NULL, cc, fvalue);
            return TRUE;
        case slpt_prevoice_cc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_add_nif(l, NULL, e->extra_ptr, cc, fvalue);
            return TRUE;
        case slpt_reserved:
        case slpt_invalid:
        case slpt_alias:
            break;
    }
    printf("Unhandled parameter type of parameter %s\n", e->name);
    assert(0);
    return FALSE;
}

#define COPY_NUM_FROM_PARENT(case_value, type) \
    case case_value: \
        *(type *)p = pp ? *(type *)pp : (type)e->def_value; \
        e->set_has_value(&l->data, 0); \
        return TRUE;
gboolean sampler_layer_param_entry_unset(const struct sampler_layer_param_entry *e, struct sampler_layer *l, const uint32_t *args, GError **error)
{
    void *p = ((uint8_t *)&l->data) + e->offset;
    void *pp = l->parent_group ? ((uint8_t *)&l->parent_group->data) + e->offset : NULL;
    uint32_t cc;

    switch(e->type)
    {
        COPY_NUM_FROM_PARENT(slpt_midi_note_t, midi_note_t)
        COPY_NUM_FROM_PARENT(slpt_int, int)
        COPY_NUM_FROM_PARENT(slpt_enum, uint32_t) // XXXKF that's a kludge, enums are not guaranteed to be uint32_t (but they should be on common platforms)
        COPY_NUM_FROM_PARENT(slpt_uint32_t, uint32_t)
        COPY_NUM_FROM_PARENT(slpt_float, float)
        COPY_NUM_FROM_PARENT(slpt_dBamp, float)
        case slpt_string:
            {
                char **pc = p;
                free(*pc);
                *pc = pp ? g_strdup(*(const char **)pp) : NULL;
                e->set_has_value(&l->data, 0);
                gboolean *changed_ptr = (gboolean *)(((uint8_t *)&l->data) + e->extra_int);
                *changed_ptr = 1;
            }
            return TRUE;
        case slpt_midicurve:
            if (args[0] >= 0 && args[0] <= 127)
            {
                ((float *)p)[args[0]] = pp ? ((float *)pp)[args[0]] : -1;
                void (*sethasfunc)(struct sampler_layer_data *, uint32_t, gboolean value) = e->extra_ptr;
                sethasfunc(&l->data, args[0], 0);
                return TRUE;
            }
            else
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Curve entry index (%u) is out of range for %s", (unsigned)args[0], e->name);
                return FALSE;
            }
        case slpt_ccrange:
        {
            struct sampler_cc_range *range = p, *prange = pp;
            cc = args[0];
            if ((range->has_locc || range->has_hicc) && range->cc_number != cc)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Conflicting controller number for %s (%d vs previously used %d)", e->name, cc, (int)range->cc_number);
                return FALSE;
            }

            switch(e->extra_int) {
                case 0: range->locc = prange ? prange->locc : (uint8_t)e->def_value; range->is_active = range->has_hicc || (prange ? prange->is_active : 0); break;
                case 1: range->hicc = prange ? prange->hicc : (uint8_t)e->def_value; range->is_active = range->has_locc || (prange ? prange->is_active : 0); break;
                default: assert(0);
            }
            e->set_has_value(&l->data, 0);
            return TRUE;
        }
        case slpt_depthcc:
            cc = args[0];
            sampler_layer_unset_modulation(l, (e->extra_int >> 16), cc, (e->extra_int & 0xFFFF), FALSE);
            return TRUE;
        case slpt_amountcc:
            cc = args[0];
            sampler_layer_unset_modulation(l, cc, smsrc_none, (e->extra_int & 0xFFFF), FALSE);
            return TRUE;
        case slpt_amount:
            sampler_layer_unset_modulation(l, (e->extra_int >> 16), smsrc_none, (e->extra_int & 0xFFFF), FALSE);
            return TRUE;
        case slpt_modulation:
            sampler_layer_unset_modulation(l, (e->extra_int >> 8) & 0xFFF, (e->extra_int >> 20), (e->extra_int & 0xFF), FALSE);
            return TRUE;
        case slpt_voice_nif:
            sampler_layer_remove_nif(l, e->extra_ptr, NULL, e->extra_int, FALSE);
            return TRUE;
        case slpt_prevoice_nif:
            sampler_layer_remove_nif(l, NULL, e->extra_ptr, e->extra_int, FALSE);
            return TRUE;
        case slpt_voice_cc_nif:
            cc = args[0];
            sampler_layer_remove_nif(l, e->extra_ptr, NULL, cc, FALSE);
            return TRUE;
        case slpt_prevoice_cc_nif:
            cc = args[0];
            sampler_layer_remove_nif(l, NULL, e->extra_ptr, cc, FALSE);
            return TRUE;
        case slpt_generic_modulation:
            sampler_layer_unset_modulation(l, args[0], args[1], args[2], FALSE);
            return TRUE;
        case slpt_invalid:
        case slpt_reserved:
        case slpt_alias:
            break;
    }
    printf("Unhandled parameter type of parameter %s\n", e->name);
    assert(0);
    return FALSE;
}
#undef COPY_NUM_FROM_PARENT

// Compare against a template that uses # to represent a number, extract
// any such numbers.
static int templcmp(const char *key, const char *templ, uint32_t *numbers)
{
    while(*key && *templ)
    {
        if (*templ == '#')
        {
            if (isdigit(*key)) {
                uint32_t num = 0;
                do {
                    num = num * 10 + (unsigned char)(*key - '0');
                    key++;
                } while (isdigit(*key));
                *numbers++ = num;
                templ++;
                continue;
            }
        }
        else if (*key == *templ)
        {
            templ++, key++;
            continue;
        }
        if (*key < *templ)
            return -1;
        else
            return +1;
    }
    if (*key)
        return +1;
    if (*templ)
        return -1;
    return 0;
}

const struct sampler_layer_param_entry *sampler_layer_param_find(const char *key, uint32_t *args)
{
    static int prepared = 0;
    if (!prepared)
    {
        sampler_layer_prepare_params();
        prepared = 1;
    }
    int niter = 0;
    uint32_t lo = 0, hi = NPARAMS;
    while(lo < hi) {
        ++niter;
        uint32_t mid = (lo + hi) >> 1;
        const struct sampler_layer_param_entry *e = &sampler_layer_params[mid];
        int res = templcmp(key, e->name, args);
        if (res == 0)
        {
            // printf("%s found in %d iterations\n", key, niter);
            if (e->type == slpt_alias)
                return (const struct sampler_layer_param_entry *)e->extra_ptr;
            return e;
        }
        if (res < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

int sampler_layer_apply_fixed_param(struct sampler_layer *l, const char *key, const char *value, GError **error)
{
    uint32_t args[10];
    const struct sampler_layer_param_entry *e = sampler_layer_param_find(key, args);
    if (e)
        return sampler_layer_param_entry_set_from_string(e, l, value, args, error);
    else
        return -1;
}

int sampler_layer_unapply_fixed_param(struct sampler_layer *l, const char *key, GError **error)
{
    uint32_t args[10];
    const struct sampler_layer_param_entry *e = sampler_layer_param_find(key, args);
    if (e)
        return sampler_layer_param_entry_unset(e, l, args, error);
    else
        return -1;
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
        gboolean result = cbox_execute_on(fb, NULL, "/value", "s", error, res[0] == ' ' ? res + 1 : res);
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
    if (!strcmp(cmd->command, "/unset_param") && !strcmp(cmd->arg_types, "s"))
    {
        const char *key = CBOX_ARG_S(cmd, 0);
        if (sampler_layer_unapply_param(layer, key, error))
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
#define PROC_FIELDS_INITIALISER_midicurve(name) \
    for (uint32_t i = 0; i < 128; ++i) \
        ld->name[i] = SAMPLER_CURVE_GAP; \
    memset(ld->has_##name, 0, 128);
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
#define PROC_FIELDS_INITIALISER_ccrange(name, parname) \
    ld->name.locc = 0; \
    ld->name.hicc = 127; \
    ld->name.cc_number = 0; \
    ld->name.has_locc = 0; \
    ld->name.has_hicc = 0; \
    ld->name.is_active = 0;

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
    ld->modulations = NULL;
    ld->voice_nifs = NULL;
    ld->prevoice_nifs = NULL;

    ld->eff_use_keyswitch = 0;
    l->last_key = -1;
    if (!parent_group)
    {
        sampler_layer_set_modulation(l, 74, smsrc_none, smdest_cutoff, 9600, 2);
        sampler_layer_set_modulation(l, 71, smsrc_none, smdest_resonance, 12, 2);
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
#define PROC_FIELDS_CLONE_midicurve(name) \
    memcpy(dst->name, src->name, sizeof(float) * 128); \
    if(copy_hasattr) \
        memcpy(dst->has_##name, src->has_##name, sizeof(uint8_t) * 128);
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
#define PROC_FIELDS_CLONE_ccrange(name, parname) \
    dst->name.locc = src->name.locc; \
    dst->name.hicc = src->name.hicc; \
    dst->name.cc_number = src->name.cc_number; \
    dst->name.is_active = src->name.is_active; \
    dst->name.has_locc = copy_hasattr ? src->name.has_locc : FALSE; \
    dst->name.has_hicc = copy_hasattr ? src->name.has_hicc : FALSE;

static GSList *clone_nifs(GSList *nifs, gboolean copy_hasattr)
{
    nifs = g_slist_copy(nifs);
    for(GSList *nif = nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *dstn = g_malloc(sizeof(struct sampler_noteinitfunc));
        struct sampler_noteinitfunc *srcn = nif->data;
        memcpy(dstn, srcn, sizeof(struct sampler_noteinitfunc));
        dstn->has_value = copy_hasattr ? srcn->has_value : FALSE;
        nif->data = dstn;
    }
    return nifs;
}

void sampler_layer_data_clone(struct sampler_layer_data *dst, const struct sampler_layer_data *src, gboolean copy_hasattr)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_CLONE)
    dst->modulations = g_slist_copy(src->modulations);
    for(GSList *mod = dst->modulations; mod; mod = mod->next)
    {
        struct sampler_modulation *srcm = mod->data;
        struct sampler_modulation *dstm = g_malloc(sizeof(struct sampler_modulation));
        memcpy(dstm, srcm, sizeof(struct sampler_modulation));
        dstm->has_value = copy_hasattr ? srcm->has_value : FALSE;
        mod->data = dstm;
    }
    dst->voice_nifs = clone_nifs(src->voice_nifs, copy_hasattr);
    dst->prevoice_nifs = clone_nifs(src->prevoice_nifs, copy_hasattr);
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
// XXXKF use a better default
#define PROC_FIELDS_CLONEPARENT_midicurve(name) \
    for (uint32_t i = 0; i < 128; ++i) \
        if (!l->has_##name[i]) \
            l->name[i] = parent ? parent->name[i] : SAMPLER_CURVE_GAP;
#define PROC_FIELDS_CLONEPARENT_dBamp PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_enum PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_dahdsr(name, parname, index) \
        DAHDSR_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_lfo(name, parname, index) \
        LFO_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_eq(name, parname, index) \
        EQ_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_ccrange(name, parname) \
    if (!l->name.has_locc) \
        l->name.locc = parent ? parent->name.locc : 0; \
    if (!l->name.has_hicc) \
        l->name.hicc = parent ? parent->name.hicc : 127; \
    if (!l->name.has_locc && !l->name.has_hicc) {\
        l->name.is_active = parent ? parent->name.is_active : 0; \
        l->name.cc_number = parent ? parent->name.cc_number : -1; \
    }

static void sampler_layer_data_getdefaults(struct sampler_layer_data *l, struct sampler_layer_data *parent)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_CLONEPARENT)
    // XXXKF: add handling for velcurve
    if (parent)
    {
        // set NIFs used by parent
        for(GSList *mod = parent->voice_nifs; mod; mod = mod->next)
        {
            struct sampler_noteinitfunc *nif = mod->data;
            sampler_layer_data_add_nif(l, nif->notefunc_voice, nif->notefunc_prevoice, nif->variant, nif->param, TRUE);
        }
        for(GSList *mod = parent->prevoice_nifs; mod; mod = mod->next)
        {
            struct sampler_noteinitfunc *nif = mod->data;
            sampler_layer_data_add_nif(l, nif->notefunc_voice, nif->notefunc_prevoice, nif->variant, nif->param, TRUE);
        }
        // set modulations used by parent
        for(GSList *mod = parent->modulations; mod; mod = mod->next)
        {
            struct sampler_modulation *srcm = mod->data;
            sampler_layer_data_set_modulation(l, srcm->src, srcm->src2, srcm->dest, srcm->amount, srcm->flags, TRUE);
        }
    }
}

static void interpolate_midicurve(float dest[128], const float src[128], float def_start, float def_end, gboolean is_quadratic)
{
    int start = 0;
    float sv = src[start];
    if (sv == SAMPLER_CURVE_GAP)
        sv = def_start;
    if (is_quadratic && sv >= 0)
        sv = sqrtf(sv);
    for (int i = 1; i < 128; i++)
    {
        float ev = src[i];
        if (ev == SAMPLER_CURVE_GAP)
        {
            if (i < 127)
                continue;
            else
                ev = def_end;
        }
        if (is_quadratic && ev >= 0)
            ev = sqrtf(ev);
        if (is_quadratic)
        {
            for (int j = start; j <= i; j++)
                dest[j] = powf(sv + (ev - sv) * (j - start) / (i - start), 2.f);
        }
        else
        {
            for (int j = start; j <= i; j++)
                dest[j] = sv + (ev - sv) * (j - start) / (i - start);
        }
        start = i;
        sv = ev;
    }
}

// If veltrack > 0, then the default range goes from -84dB to 0dB
// If veltrack == 0, then the default range is all 0dB
// If veltrack < 0, then the default range goes from 0dB to -84dB
#define START_VALUE_amp_velcurve (l->amp_veltrack > 0 ? dB2gain(-l->amp_veltrack * 84.0 / 100.0) : 1)
#define END_VALUE_amp_velcurve (l->amp_veltrack < 0 ? dB2gain(l->amp_veltrack * 84.0 / 100.0) : 1)
#define IS_QUADRATIC_amp_velcurve l->velcurve_quadratic

#define PROC_FIELDS_FINALISER(type, name, def_value) 
#define PROC_FIELDS_FINALISER_string(name)
#define PROC_FIELDS_FINALISER_midicurve(name) \
    interpolate_midicurve(l->eff_##name, l->name, START_VALUE_##name, END_VALUE_##name, IS_QUADRATIC_##name);
#define PROC_FIELDS_FINALISER_enum(type, name, def_value) 
#define PROC_FIELDS_FINALISER_dBamp(type, name, def_value) \
    l->name##_linearized = dB2gain(l->name);
#define PROC_FIELDS_FINALISER_dahdsr(name, parname, index) \
    cbox_envelope_init_dahdsr(&l->name##_shape, &l->name, m->module.srate / CBOX_BLOCK_SIZE, 100.f, &l->name##_shape == &l->amp_env_shape);
#define PROC_FIELDS_FINALISER_lfo(name, parname, index) /* no finaliser required */
#define PROC_FIELDS_FINALISER_eq(name, parname, index) l->name.effective_freq = (l->name.freq ? l->name.freq : 5 * powf(10.f, 1 + (index)));
#define PROC_FIELDS_FINALISER_ccrange(name, parname) /* no finaliser required */

void sampler_layer_data_finalize(struct sampler_layer_data *l, struct sampler_layer_data *parent, struct sampler_program *p)
{
    struct sampler_module *m = p->module;
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_FINALISER)

    sampler_layer_data_getdefaults(l, parent);
    
    // Handle change of sample in the parent group without override on region level
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
    
    l->eff_use_keyswitch = ((l->sw_down != -1) || (l->sw_up != -1) || (l->sw_last != -1) || (l->sw_previous != -1));
    l->eff_use_simple_trigger_logic =
        (l->seq_length == 1 && l->seq_position == 1) &&
        (l->trigger != stm_first && l->trigger != stm_legato) &&
        (l->lochan == 1 && l->hichan == 16) &&
        (l->lorand == 0 && l->hirand == 1) &&
        (l->lobend == -8192 && l->hibend == 8192) &&
        (l->lochanaft == 0 && l->hichanaft == 127) &&
        (l->lopolyaft == 0 && l->hipolyaft == 127) &&
        !l->cc.is_active && !l->eff_use_keyswitch;
    l->eff_use_xfcc = l->xfin_cc.is_active || l->xfout_cc.is_active;
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

    // XXXKF this is dodgy, needs to convert to use 'programmed vs effective' values pattern
    if (l->key >= 0 && l->key <= 127)
        l->lokey = l->hikey = l->pitch_keycenter = l->key;

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

    l->use_prevoice = (l->delay || l->prevoice_nifs);
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

static void sampler_layer_apply_unknown(struct sampler_layer *l, const char *key, const char *value)
{
    if (!l->unknown_keys)
        l->unknown_keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    g_hash_table_insert(l->unknown_keys, g_strdup(key), g_strdup(value));
}

gboolean sampler_layer_apply_param(struct sampler_layer *l, const char *key, const char *value, GError **error)
{
    int res = sampler_layer_apply_fixed_param(l, key, value, error);
    if (res >= 0)
        return res;
    sampler_layer_apply_unknown(l, key, value);
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown SFZ property key: '%s'", key);
    return FALSE;
}

gboolean sampler_layer_unapply_param(struct sampler_layer *layer, const char *key, GError **error)
{
    int res = sampler_layer_unapply_fixed_param(layer, key, error);
    if (res >= 0)
        return res;
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown SFZ property key: '%s'", key);
    return FALSE;
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
#define PROC_FIELDS_TO_FILEPTR_midicurve(name) \
    for (uint32_t i = 0; i < 128; ++i) { \
        if ((show_inherited || l->has_##name[i]) && l->name[i] != SAMPLER_CURVE_GAP) \
            g_string_append_printf(outstr, " %s_%u=%s", #name, (unsigned)i, g_ascii_dtostr(floatbuf, floatbufsize, l->name[i])); \
        }
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
#define PROC_FIELDS_TO_FILEPTR_ccrange(name, parname) \
    if (show_inherited || l->name.has_locc) \
        g_string_append_printf(outstr, " " #parname "locc%d=%d", l->name.cc_number, l->name.locc); \
    if (show_inherited || l->name.has_hicc) \
        g_string_append_printf(outstr, " " #parname "hicc%d=%d", l->name.cc_number, l->name.hicc);

gchar *sampler_layer_to_string(struct sampler_layer *lr, gboolean show_inherited)
{
    struct sampler_layer_data *l = &lr->data;
    GString *outstr = g_string_sized_new(200);
    const char *tmpstr;
    char floatbuf[G_ASCII_DTOSTR_BUF_SIZE];
    int floatbufsize = G_ASCII_DTOSTR_BUF_SIZE;
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_FILEPTR)
    
    static const char *addrandom_variants[] = { "amp", "fil", "pitch" };
    static const char *modsrc_names[] = { "chanaft", "lastpolyaft", "vel", "polyaft", "pitch", "pitcheg", "fileg", "ampeg", "pitchlfo", "fillfo", "amplfo", "" };
    static const char *moddest_names[] = { "gain", "pitch", "cutoff", "resonance", "tonectl", "pitchlfo_freq", "fillfo_freq", "amplfo_freq",
        "eq1_freq", "eq1_bw", "eq1_gain",
        "eq2_freq", "eq2_bw", "eq2_gain",
        "eq3_freq", "eq3_bw", "eq3_gain",
        };
    for(GSList *nif = l->voice_nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *nd = nif->data;
        if (!nd->has_value && !show_inherited)
            continue;
        #define PROC_ENVSTAGE_NAME(name, index, def_value) #name, 
        static const char *env_stages[] = { DAHDSR_FIELDS(PROC_ENVSTAGE_NAME) "start" };
        int v = nd->variant;
        g_ascii_dtostr(floatbuf, floatbufsize, nd->param);
        
        if (nd->notefunc_voice == sampler_nif_addrandom && v >= 0 && v <= 2)
            g_string_append_printf(outstr, " %s_random=%s", addrandom_variants[nd->variant], floatbuf);
        else if (nd->notefunc_voice == sampler_nif_vel2pitch)
            g_string_append_printf(outstr, " pitch_veltrack=%s", floatbuf);
        else if (nd->notefunc_voice == sampler_nif_vel2reloffset)
            g_string_append_printf(outstr, " reloffset_veltrack=%s", floatbuf);
        else if (nd->notefunc_voice == sampler_nif_cc2reloffset && v >= 0 && v < 120)
            g_string_append_printf(outstr, " reloffset_cc%d=%s", nd->variant, floatbuf);
        else if (nd->notefunc_voice == sampler_nif_vel2offset)
            g_string_append_printf(outstr, " offset_veltrack=%s", floatbuf);
        else if (nd->notefunc_voice == sampler_nif_cc2offset && v >= 0 && v < 120)
            g_string_append_printf(outstr, " offset_cc%d=%s", nd->variant, floatbuf);
        else if (nd->notefunc_voice == sampler_nif_vel2env && v >= snif_env_delay && (v & 15) <= snif_env_start && (v >> 4) < 3)
            g_string_append_printf(outstr, " %seg_vel2%s=%s", addrandom_variants[nd->variant >> 4], env_stages[1 + (v & 15)], floatbuf);
        else
            assert(0); // unknown NIF
    }
    for(GSList *nif = l->prevoice_nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *nd = nif->data;
        if (!nd->has_value && !show_inherited)
            continue;
        int v = nd->variant;
        g_ascii_dtostr(floatbuf, floatbufsize, nd->param);

        if (nd->notefunc_prevoice == sampler_nif_cc2delay && v >= 0 && v < 120)
            g_string_append_printf(outstr, " delay_cc%d=%s", nd->variant, floatbuf);
        else if (nd->notefunc_prevoice == sampler_nif_addrandomdelay)
            g_string_append_printf(outstr, " delay_random=%s", floatbuf);
        else
            assert(0); // unknown NIF
    }
    for(GSList *mod = l->modulations; mod; mod = mod->next)
    {
        struct sampler_modulation *md = mod->data;
        if (!md->has_value && !show_inherited)
            continue;
        g_ascii_dtostr(floatbuf, floatbufsize, md->amount);

        if (md->src2 == smsrc_none)
        {
            gboolean is_lfofreq = md->dest >= smdest_pitchlfo_freq && md->dest <= smdest_eq3_gain;
            if (md->src < 120)
            {
                // Inconsistency: cutoff_cc5 but amplfo_freqcc5
                if (is_lfofreq)
                    g_string_append_printf(outstr, " %scc%d=%s", moddest_names[md->dest], md->src, floatbuf);
                else
                    g_string_append_printf(outstr, " %s_cc%d=%s", moddest_names[md->dest], md->src, floatbuf);
                continue;
            }
            if (md->src < 120 + sizeof(modsrc_names) / sizeof(modsrc_names[0]))
            {
                if ((md->src == smsrc_filenv && md->dest == smdest_cutoff) ||
                    (md->src == smsrc_pitchenv && md->dest == smdest_pitch) ||
                    (md->src == smsrc_amplfo && md->dest == smdest_gain) ||
                    (md->src == smsrc_fillfo && md->dest == smdest_cutoff) ||
                    (md->src == smsrc_pitchlfo && md->dest == smdest_pitch))
                    g_string_append_printf(outstr, " %s_depth=%s", modsrc_names[md->src - 120], floatbuf);
                else if (is_lfofreq)
                    g_string_append_printf(outstr, " %s%s=%s", moddest_names[md->dest], modsrc_names[md->src - 120], floatbuf);
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
                g_string_append_printf(outstr, " %s_depth%s=%s", modsrc_names[md->src - 120], modsrc_names[md->src2 - 120], floatbuf);
                continue;
            case smsrc_none:
                g_string_append_printf(outstr, " %s_depth=%s", modsrc_names[md->src - 120], floatbuf);
                continue;
            default:
                if (md->src2 < 120)
                {
                    g_string_append_printf(outstr, " %s_depthcc%d=%s", modsrc_names[md->src - 120], md->src2, floatbuf);
                    continue;
                }
                break;
            }
        }
        if ((md->src == smsrc_ampenv && md->dest == smdest_gain) ||
            (md->src == smsrc_filenv && md->dest == smdest_cutoff) ||
            (md->src == smsrc_pitchenv && md->dest == smdest_pitch))
        {
            if (md->src2 == smsrc_vel)
            {
                g_string_append_printf(outstr, " %s_vel2depth=%s", modsrc_names[md->src - 120], floatbuf);
                continue;
            }
            if (md->src2 == smsrc_none)
            {
                g_string_append_printf(outstr, " %s_depth=%s", modsrc_names[md->src - 120], floatbuf);
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
    g_slist_free_full(l->voice_nifs, g_free);
    g_slist_free_full(l->prevoice_nifs, g_free);
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
    FOREACH_PREVOICE(cmd->module->prevoices_running, pv)
    {
        if (pv->layer_data == cmd->layer->runtime)
        {
            pv->layer_data = cmd->new_data;
            // XXXKF when need arises
            // pv->layer_changed = TRUE;
            // sampler_prevoice_update_params_from_layer(v);
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

