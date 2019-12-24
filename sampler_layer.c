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

static inline gboolean sampler_modulation_key_equal(const struct sampler_modulation_key *k1, const struct sampler_modulation_key *k2)
{
    return (k1->src == k2->src && k1->src2 == k2->src2 && k1->dest == k2->dest);
}

static inline void sampler_modulation_dump_one(const struct sampler_modulation *sm)
{
    printf("%d x %d -> %d : %f : %d\n", sm->key.src, sm->key.src2, sm->key.dest, sm->value.amount, sm->value.curve_id);
}

//////////////////////////////////////////////////////////////////////////////////////////////////

static inline gboolean sampler_noteinitfunc_key_equal(const struct sampler_noteinitfunc_key *k1, const struct sampler_noteinitfunc_key *k2)
{
    return (k1->notefunc_voice == k2->notefunc_voice && k1->variant == k2->variant);
}

static inline void sampler_noteinitfunc_dump_one(const struct sampler_noteinitfunc *sm)
{
    printf("%p(%d) = %f\n", sm->key.notefunc_voice, sm->key.variant, sm->value.value);
}

//////////////////////////////////////////////////////////////////////////////////////////////////

#define SAMPLER_COLL_FUNC_DUMP(sname) \
    void sname##_dump(const GSList *p) \
    { \
        while(p) \
        { \
            const struct sname *d = p->data; \
            sname##_dump_one(d); \
            p = g_slist_next(p); \
        } \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_DUMP)

#define SAMPLER_COLL_FUNC_FIND(sname) \
    static struct sname *sname##_find(GSList *list, const struct sname##_key *key, GSList **link_ptr) \
    { \
        GSList *p = list; \
        while(p) \
        { \
            struct sname *d = p->data; \
            struct sname##_key *dkey = &d->key; \
            if (sname##_key_equal(dkey, key)) \
            { \
                if (link_ptr) \
                    *link_ptr = p; \
                return d; \
            } \
            p = g_slist_next(p); \
        } \
        return NULL; \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_FIND)

#define SAMPLER_COLL_FIELD_INIT(name, has_name, type, init_value) \
    d->value.name = init_value; \
    d->value.has_name = FALSE;

#define SAMPLER_COLL_FUNC_ADD(sname) \
static struct sname *sname##_add(GSList **list_ptr, const struct sname##_key *key) \
{ \
    struct sname *d = sname##_find(*list_ptr, key, NULL); \
    if (d) \
        return d; \
    d = g_malloc0(sizeof(struct sname)); \
    d->key = *key; \
    SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_INIT)\
    *list_ptr = g_slist_prepend(*list_ptr, d); \
    return d; \
}

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_ADD)

#define SAMPLER_COLL_FIELD_ISNULL(name, has_name, type, init_value) \
    if (d->name != init_value || d->has_name) \
        return FALSE;
#define SAMPLER_COLL_FIELD_ISNULLVALUE(name, has_name, type, init_value) \
    if (d->name != init_value) \
        return FALSE;

#define SAMPLER_COLL_FUNC_ISNULL(sname) \
    static inline gboolean sname##_is_null(const struct sname##_value *d) \
    { \
        SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_ISNULL) \
        return TRUE; \
    } \
    static inline gboolean sname##_is_null_value(const struct sname##_value *d) \
    { \
        SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_ISNULLVALUE) \
        return TRUE; \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_ISNULL)

#define SAMPLER_COLL_FIELD_PROPAGATE(name, has_name, type, init_value) \
    if (!dstm->value.has_name && dstm->value.name != srcm->value.name) \
    { \
        dstm->value.name = srcm->value.name;\
        changed = TRUE; \
    }

// sampler_layer_set_modulation_amount etc.
#define SAMPLER_COLL_FIELD_SETTER(name, has_name, type, init_value, sname, cname, cfname) \
    void sampler_layer_set_##cname##_##name(struct sampler_layer *l, const struct sname##_key *key, type value) \
    { \
        struct sname *dstm = sname##_add(&l->data.cfname, key); \
        dstm->value.has_name = TRUE; \
        dstm->value.name = value; \
        sname##_propagate_set(l, offsetof(struct sampler_layer_data, cfname), dstm, TRUE); \
    }

#define SAMPLER_COLL_CHAIN_SETTERS(cfname, cname, sname) \
    SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_SETTER, sname, cname, cfname)

#define SAMPLER_COLL_FUNC_PROPAGATE_SET(sname) \
    void sname##_propagate_set(struct sampler_layer *l, uint32_t offset, const struct sname *srcm, gboolean starting) \
    { \
        if (!starting) \
        { \
            void *vl = &l->data; \
            GSList **l = (vl + offset); \
            gboolean changed = FALSE; \
            struct sname *dstm = sname##_add(l, &srcm->key); \
            SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_PROPAGATE) \
            if (!changed) \
                return; \
        } \
        if (l->child_layers) { \
            /* Propagate to children */ \
            GHashTableIter iter; \
            g_hash_table_iter_init(&iter, l->child_layers); \
            gpointer key, value; \
            while(g_hash_table_iter_next(&iter, &key, &value)) \
            { \
                struct sampler_layer *child = value; \
                sname##_propagate_set(child, offset, srcm, FALSE); \
            } \
        } \
    } \
    SAMPLER_COLL_CHAIN_LIST_##sname(SAMPLER_COLL_CHAIN_SETTERS, sname)

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_PROPAGATE_SET)

#define SAMPLER_COLL_FIELD_UNSET(name, has_name, type, init_value, sname) \
    if ((unset_mask & (1 << sname##_key_field_##name)) && d->value.has_name == remove_local) \
    { \
        d->value.name = parent ? parent->value.name : init_value; \
        if (remove_local) \
            d->value.has_name = FALSE; \
    } \

#define SAMPLER_COLL_FIELD_KEY_ENUM_VALUE(name, has_name, type, init_value, sname) \
    sname##_key_field_##name,

#define SAMPLER_COLL_FIELD_UNSETTER(name, has_name, type, init_value, sname, cname, cfname) \
    void sampler_layer_unset_##cname##_##name(struct sampler_layer *l, const struct sname##_key *key, gboolean remove_local) \
    { \
        sname##_propagate_unset(l, offsetof(struct sampler_layer_data, cfname), key, remove_local, 1 << sname##_key_field_##name); \
    }
#define SAMPLER_COLL_CHAIN_UNSETTERS(cfname, cname, sname) \
    SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_UNSETTER, sname, cname, cfname)

#define SAMPLER_COLL_FUNC_PROPAGATE_UNSET(sname) \
    enum {  \
        SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_KEY_ENUM_VALUE, sname) \
    }; \
    static gboolean sname##_unset(GSList **list_ptr, GSList *parent_list, const struct sname##_key *key, gboolean remove_local, uint32_t unset_mask) \
    { \
        GSList *link = NULL; \
        struct sname *d = sname##_find(*list_ptr, key, &link); \
        if (!d) \
            return FALSE; \
        struct sname *parent = remove_local && parent_list != NULL ? sname##_find(parent_list, key, NULL) : NULL; \
        SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_UNSET, sname) \
        /* Delete if it's all default values and it's not overriding anything */ \
        if (sname##_is_null(&d->value)) \
            *list_ptr = g_slist_delete_link(*list_ptr, link); \
        return TRUE; \
    } \
    static void sname##_propagate_unset(struct sampler_layer *l, uint32_t offset, const struct sname##_key *key, gboolean remove_local, uint32_t unset_mask) \
    { \
        void *vl = &l->data, *vp = l->parent ? &l->parent->data : NULL; \
        GSList **list_ptr = vl + offset; \
        GSList **parent_list_ptr = vp ? vp + offset : NULL; \
        if (!sname##_unset(list_ptr, *parent_list_ptr, key, remove_local, unset_mask)) \
            return; \
        \
        if (l->child_layers) { \
            /* Also recursively remove propagated copies from child layers, if any */ \
            GHashTableIter iter; \
            g_hash_table_iter_init(&iter, l->child_layers); \
            gpointer lkey, lvalue; \
            while(g_hash_table_iter_next(&iter, &lkey, &lvalue)) \
            { \
                struct sampler_layer *child = lvalue; \
                sname##_propagate_unset(child, offset, key, FALSE, unset_mask); \
            } \
        } \
    } \
    SAMPLER_COLL_CHAIN_LIST_##sname(SAMPLER_COLL_CHAIN_UNSETTERS, sname)

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_PROPAGATE_UNSET)

#define SAMPLER_COLL_FIELD_ZEROHASATTR(name, has_name, type, init_value) \
    dstv->value.has_name = FALSE;
#define SAMPLER_COLL_FUNC_CLONE(sname) \
    static GSList *sname##_clone(GSList *src, gboolean copy_hasattr) \
    { \
        GSList *dst = g_slist_copy(src); \
        for(GSList *d = dst; d; d = d->next) \
        { \
            const struct sname *srcv = d->data; \
            struct sname *dstv = g_malloc(sizeof(struct sname)); \
            memcpy(dstv, srcv, sizeof(struct sname)); \
            if (!copy_hasattr) { \
                SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_ZEROHASATTR) \
            } \
            d->data = dstv; \
        } \
        return dst; \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_CLONE)

//////////////////////////////////////////////////////////////////////////////////////////////////

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
    slpt_curvecc,
    slpt_smoothcc,
    slpt_stepcc,
    slpt_depth_curvecc,
    slpt_depth_smoothcc,
    slpt_depth_stepcc,
    // note init functions
    slpt_voice_nif,
    slpt_prevoice_nif,
    slpt_voice_cc_nif,
    slpt_prevoice_cc_nif,
    slpt_voice_curvecc_nif,
    slpt_prevoice_curvecc_nif,
    slpt_voice_stepcc_nif,
    slpt_prevoice_stepcc_nif,
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

struct sampler_cc_range *sampler_cc_range_find_cc(struct sampler_cc_range *range, int cc_no)
{
    while(range && range->cc_number != cc_no)
        range = range->next;
    return range;
}

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
    static void sampler_layer_data_set_has_##name##_lo(struct sampler_layer_data *l, uint32_t cc_no, gboolean value) { struct sampler_cc_range *range = sampler_cc_range_find_cc(l->name, cc_no); if (range) range->has_locc = value; } \
    static void sampler_layer_data_set_has_##name##_hi(struct sampler_layer_data *l, uint32_t cc_no, gboolean value) { struct sampler_cc_range *range = sampler_cc_range_find_cc(l->name, cc_no); if (range) range->has_hicc = value; }
#define PROC_FIELD_SETHASFUNC_midicurve(name) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, uint32_t index, gboolean value) { l->name.has_values[index] = value; }

SAMPLER_FIXED_FIELDS(PROC_FIELD_SETHASFUNC)

#define FIELD_AMOUNT(name, src, dest) \
    { name, -1, slpt_amount, 0, (smsrc_##src << 16) | smdest_##dest, NULL, NULL },
#define FIELD_AMOUNT_CC(name, dest) \
    { name "cc#", -1, slpt_amountcc, 0, smdest_##dest, NULL, NULL }, \
    { name "_oncc#", -1, slpt_amountcc, 0, smdest_##dest, NULL, NULL }, \
    { name "_curvecc#", -1, slpt_curvecc, 0, smdest_##dest, NULL, NULL }, \
    { name "_smoothcc#", -1, slpt_smoothcc, 0, smdest_##dest, NULL, NULL }, \
    { name "_stepcc#", -1, slpt_stepcc, 0, smdest_##dest, NULL, NULL },
#define FIELD_AMOUNT_CC_(name, dest) \
    { name "_cc#", -1, slpt_amountcc, 0, smdest_##dest, NULL, NULL }, \
    { name "_oncc#", -1, slpt_amountcc, 0, smdest_##dest, NULL, NULL }, \
    { name "_curvecc#", -1, slpt_curvecc, 0, smdest_##dest, NULL, NULL }, \
    { name "_smoothcc#", -1, slpt_smoothcc, 0, smdest_##dest, NULL, NULL }, \
    { name "_stepcc#", -1, slpt_stepcc, 0, smdest_##dest, NULL, NULL },
#define FIELD_VOICE_NIF(name, nif, variant) \
    { name, -1, slpt_voice_nif, 0, variant, nif, NULL },
#define FIELD_PREVOICE_NIF(name, nif, variant) \
    { name, -1, slpt_prevoice_nif, 0, variant, nif, NULL },
#define FIELD_VOICE_CC_NIF(name, nif, variant) \
    { name, -1, slpt_voice_cc_nif, 0, variant, nif, NULL },
#define FIELD_PREVOICE_CC_NIF(name, nif, variant) \
    { name, -1, slpt_prevoice_cc_nif, 0, variant, nif, NULL },
#define FIELD_VOICE_CURVECC_NIF(name, nif, variant) \
    { name, -1, slpt_voice_curvecc_nif, 0, variant, nif, NULL },
#define FIELD_PREVOICE_CURVECC_NIF(name, nif, variant) \
    { name, -1, slpt_prevoice_curvecc_nif, 0, variant, nif, NULL },
#define FIELD_VOICE_STEPCC_NIF(name, nif, variant) \
    { name, -1, slpt_voice_stepcc_nif, 0, variant, nif, NULL },
#define FIELD_PREVOICE_STEPCC_NIF(name, nif, variant) \
    { name, -1, slpt_prevoice_stepcc_nif, 0, variant, nif, NULL },
#define FIELD_ALIAS(alias, name) \
    { alias, -1, slpt_alias, 0, 0, name, NULL },

#define LOFS(field) offsetof(struct sampler_layer_data, field)
#define PROC_SUBSTRUCT_FIELD_DESCRIPTOR(name, index, def_value, parent, parent_name, parent_index, parent_struct) \
    { #parent_name "_" #name, offsetof(struct sampler_layer_data, parent) + offsetof(struct parent_struct, name), slpt_float, def_value, parent_index * 100 + index, NULL, sampler_layer_data_##parent##_set_has_##name }, \

#define PROC_SUBSTRUCT_FIELD_DESCRIPTOR_DAHDSR(name, index, def_value, parent, parent_name, parent_index, parent_struct) \
    { #parent_name "_" #name, offsetof(struct sampler_layer_data, parent) + offsetof(struct parent_struct, name), slpt_float, def_value, parent_index * 100 + index, NULL, sampler_layer_data_##parent##_set_has_##name }, \
    FIELD_VOICE_NIF(#parent_name "_vel2" #name, sampler_nif_vel2env, (parent_index << 4) + snif_env_##name) \
    FIELD_AMOUNT_CC(#parent_name "_" #name, ampeg_stage + (parent_index << 4) + snif_env_##name) \

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

#define FIELD_DEPTHCC_SET(name, dest, attrib) \
    { #name attrib "cc#", -1, slpt_depthcc, 0, (smsrc_##name << 16) | (dest), NULL, NULL }, \
    { #name attrib "_oncc#", -1, slpt_depthcc, 0, (smsrc_##name << 16) | (dest), NULL, NULL }, \
    { #name attrib "_curvecc#", -1, slpt_depth_curvecc, 0, (smsrc_##name << 16) | (dest), NULL, NULL }, \
    { #name attrib "_smoothcc#", -1, slpt_depth_smoothcc, 0, (smsrc_##name << 16) | (dest), NULL, NULL }, \
    { #name attrib "_stepcc#", -1, slpt_depth_stepcc, 0, (smsrc_##name << 16) | (dest), NULL, NULL },

#define PROC_FIELD_DESCRIPTOR_dahdsr(field, name, index) \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR_DAHDSR, field, name, index, cbox_dahdsr) \
    FIELD_AMOUNT(#name "_depth", name, from_##name) \
    FIELD_DEPTHCC_SET(name, smdest_from_##name, "_depth") \
    { #name "_vel2depth", -1, slpt_modulation, 0, (smsrc_##name << 8) | (smsrc_vel << 20) | (smdest_from_##name), NULL, NULL },

#define PROC_FIELD_DESCRIPTOR_lfo(field, name, index) \
    LFO_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR, field, name, index, sampler_lfo_params) \
    FIELD_AMOUNT(#name "_depth", name, from_##name) \
    FIELD_AMOUNT(#name "_freqpolyaft", polyaft, name##_freq) \
    FIELD_AMOUNT(#name "_freqchanaft", chanaft, name##_freq) \
    FIELD_AMOUNT_CC(#name "_freq", name##_freq) \
    { #name "_depthpolyaft", -1, slpt_modulation, 0, (smsrc_##name << 8) | (smsrc_polyaft << 20) | (smdest_from_##name), NULL, NULL }, \
    { #name "_depthchanaft", -1, slpt_modulation, 0, (smsrc_##name << 8) | (smsrc_chanaft << 20) | (smdest_from_##name), NULL, NULL }, \
    FIELD_DEPTHCC_SET(name, smdest_from_##name, "_depth")

#define PROC_FIELD_DESCRIPTOR_eq(field, name, index) \
    EQ_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR, field, name, index, sampler_eq_params) \
    FIELD_AMOUNT_CC(#name "_freq", name##_freq) \
    FIELD_AMOUNT_CC(#name "_bw", name##_bw) \
    FIELD_AMOUNT_CC(#name "_gain", name##_gain)

#define PROC_FIELD_DESCRIPTOR_ccrange(field, parname) \
    { #parname "locc#", LOFS(field), slpt_ccrange, 0, 0, NULL, (void *)sampler_layer_data_set_has_##field##_lo }, \
    { #parname "hicc#", LOFS(field), slpt_ccrange, 127, 1, NULL, (void *)sampler_layer_data_set_has_##field##_hi },

struct sampler_layer_param_entry sampler_layer_params[] = {
    SAMPLER_FIXED_FIELDS(PROC_FIELD_DESCRIPTOR)

    FIELD_AMOUNT("cutoff_chanaft", chanaft, cutoff)
    FIELD_AMOUNT("resonance_chanaft", chanaft, resonance)
    FIELD_AMOUNT("cutoff_polyaft", polyaft, cutoff)
    FIELD_AMOUNT("resonance_polyaft", polyaft, resonance)

    FIELD_AMOUNT("fileg_depth2", fileg, cutoff2)
    FIELD_DEPTHCC_SET(fileg, smdest_cutoff2, "_depth2")
    { "fileg_vel2depth2", -1, slpt_modulation, 0, (smsrc_fileg << 8) | (smsrc_vel << 20) | (smdest_cutoff2), NULL, NULL },
    FIELD_AMOUNT("fillfo_depth2", fillfo, cutoff2)
    { "fillfo_depth2polyaft", -1, slpt_modulation, 0, (smsrc_fillfo << 8) | (smsrc_polyaft << 20) | (smdest_cutoff2), NULL, NULL }, \
    { "fillfo_depth2chanaft", -1, slpt_modulation, 0, (smsrc_fillfo << 8) | (smsrc_chanaft << 20) | (smdest_cutoff2), NULL, NULL }, \
    FIELD_DEPTHCC_SET(fillfo, smdest_cutoff2, "_depth2")

    FIELD_AMOUNT("cutoff2_chanaft", chanaft, cutoff2)
    FIELD_AMOUNT("resonance2_chanaft", chanaft, resonance2)
    FIELD_AMOUNT("cutoff2_polyaft", polyaft, cutoff2)
    FIELD_AMOUNT("resonance2_polyaft", polyaft, resonance2)

    FIELD_AMOUNT_CC_("gain", gain)
    FIELD_AMOUNT_CC_("cutoff", cutoff)
    FIELD_AMOUNT_CC_("resonance", resonance)
    FIELD_AMOUNT_CC_("cutoff2", cutoff2)
    FIELD_AMOUNT_CC_("resonance2", resonance2)
    FIELD_AMOUNT_CC_("pitch", pitch)
    FIELD_AMOUNT_CC_("tune", pitch)
    FIELD_AMOUNT_CC_("tonectl", tonectl)
    FIELD_AMOUNT_CC_("pan", pan)
    FIELD_AMOUNT_CC_("amplitude", amplitude)

    FIELD_VOICE_NIF("amp_random", sampler_nif_addrandom, 0)
    FIELD_VOICE_NIF("fil_random", sampler_nif_addrandom, 1)
    FIELD_VOICE_NIF("pitch_random", sampler_nif_addrandom, 2)
    FIELD_VOICE_NIF("pitch_veltrack", sampler_nif_vel2pitch, 0)
    FIELD_VOICE_NIF("offset_veltrack", sampler_nif_vel2offset, 0)
    FIELD_VOICE_NIF("reloffset_veltrack", sampler_nif_vel2reloffset, 0)
    FIELD_PREVOICE_NIF("delay_random", sampler_nif_addrandomdelay, 0)
    FIELD_PREVOICE_NIF("sync_beats", sampler_nif_syncbeats, 0)
    FIELD_PREVOICE_CC_NIF("delay_cc#", sampler_nif_cc2delay, 0)
    FIELD_PREVOICE_CURVECC_NIF("delay_curvecc#", sampler_nif_cc2delay, 0)
    FIELD_PREVOICE_STEPCC_NIF("delay_stepcc#", sampler_nif_cc2delay, 0)
    FIELD_VOICE_CC_NIF("reloffset_cc#", sampler_nif_cc2reloffset, 0)
    FIELD_VOICE_CURVECC_NIF("reloffset_curvecc#", sampler_nif_cc2reloffset, 0)
    FIELD_VOICE_STEPCC_NIF("reloffset_stepcc#", sampler_nif_cc2reloffset, 0)
    FIELD_VOICE_CC_NIF("offset_cc#", sampler_nif_cc2offset, 0)
    FIELD_VOICE_CURVECC_NIF("offset_curvecc#", sampler_nif_cc2offset, 0)
    FIELD_VOICE_STEPCC_NIF("offset_stepcc#", sampler_nif_cc2offset, 0)

    FIELD_ALIAS("hilev", "hivel")
    FIELD_ALIAS("lolev", "lovel")
    FIELD_ALIAS("loopstart", "loop_start")
    FIELD_ALIAS("loopend", "loop_end")
    FIELD_ALIAS("loopmode", "loop_mode")
    FIELD_ALIAS("bendup", "bend_up")
    FIELD_ALIAS("benddown", "bend_down")
    FIELD_ALIAS("offby", "off_by")
    FIELD_ALIAS("offset_oncc#", "offset_cc#")
    FIELD_ALIAS("reloffset_oncc#", "reloffset_cc#")
    FIELD_ALIAS("delay_oncc#", "delay_cc#")

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
                ((struct sampler_midi_curve *)p)->values[args[0]] = fvalue;
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
            struct sampler_cc_range **range_ptr = p;
            struct sampler_cc_range *range = *range_ptr;
            while(range && range->cc_number != cc)
                range = range->next;
            if (!range)
            {
                // XXXKF clone from parent?
                range = g_malloc0(sizeof(struct sampler_cc_range));
                range->next = *range_ptr;
                range->cc_number = cc;
                range->locc = 0;
                range->hicc = 127;
                *range_ptr = range;
            }
            switch(e->extra_int) {
                case 0: range->locc = number; range->has_locc = 1; break;
                case 1: range->hicc = number; range->has_hicc = 1; break;
                default: assert(0);
            }
            return TRUE;
        }
        case slpt_depthcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, fvalue);
            return TRUE;
        case slpt_depth_curvecc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_curve_id(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, (int)fvalue);
            return TRUE;
        case slpt_depth_smoothcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_smooth(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, fvalue);
            return TRUE;
        case slpt_depth_stepcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_step(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, fvalue);
            return TRUE;
        case slpt_amountcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, fvalue);
            return TRUE;
        case slpt_curvecc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_curve_id(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, (int)fvalue);
            return TRUE;
        case slpt_smoothcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_smooth(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, fvalue);
            return TRUE;
        case slpt_stepcc:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_modulation_step(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, fvalue);
            return TRUE;
        case slpt_amount:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){(e->extra_int >> 16), smsrc_none, (e->extra_int & 0xFFFF)}, fvalue);
            return TRUE;
        case slpt_modulation:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){(e->extra_int >> 8) & 0xFFF, (e->extra_int >> 20), (e->extra_int & 0xFF)}, fvalue);
            return TRUE;
        case slpt_generic_modulation:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){args[0], args[1], args[2]}, fvalue);
            sampler_layer_set_modulation_curve_id(l, &(struct sampler_modulation_key){args[0], args[1], args[2]}, (int)args[3]);
            return TRUE;
        case slpt_voice_nif:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_voice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = e->extra_int }, fvalue);
            return TRUE;
        case slpt_prevoice_nif:
            VERIFY_FLOAT_VALUE;
            sampler_layer_set_prevoice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = e->extra_int }, fvalue);
            return TRUE;
        case slpt_voice_cc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_voice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = cc + (e->extra_int << 8) }, fvalue);
            return TRUE;
        case slpt_prevoice_cc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_prevoice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = cc + (e->extra_int << 8) }, fvalue);
            return TRUE;
        case slpt_voice_curvecc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_voice_nif_curve_id(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = cc + (e->extra_int << 8) }, (int)fvalue);
            return TRUE;
        case slpt_prevoice_curvecc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_prevoice_nif_curve_id(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = cc + (e->extra_int << 8) }, (int)fvalue);
            return TRUE;
        case slpt_voice_stepcc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_voice_nif_step(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = cc + (e->extra_int << 8) }, fvalue);
            return TRUE;
        case slpt_prevoice_stepcc_nif:
            VERIFY_FLOAT_VALUE;
            cc = args[0];
            sampler_layer_set_prevoice_nif_step(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = cc + (e->extra_int << 8) }, fvalue);
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
    void *pp = l->parent ? ((uint8_t *)&l->parent->data) + e->offset : NULL;
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
                struct sampler_midi_curve *curve = p, *parent_curve = pp;
                curve->values[args[0]] = parent_curve ? parent_curve->values[args[0]] : -1;
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
            struct sampler_cc_range **range_ptr = p, **prange_ptr = pp;
            struct sampler_cc_range *range = *range_ptr, *prange = prange_ptr ? *prange_ptr : NULL;
            cc = args[0];
            while(range && range->cc_number != cc)
                range = range->next;
            while(prange && prange->cc_number != cc)
                prange = prange->next;
            if (!range)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Controller number %d not used for %s", cc, e->name);
                return FALSE;
            }

            switch(e->extra_int) {
                case 0: range->locc = prange ? prange->locc : (uint8_t)e->def_value; range->has_locc = 0; break;
                case 1: range->hicc = prange ? prange->hicc : (uint8_t)e->def_value; range->has_hicc = 0; break;
                default: assert(0);
            }
            if (!range->has_locc && !range->has_hicc && !prange)
            {
                // XXXKF delete (and propagate to children?)
            }
            return TRUE;
        }
        case slpt_depthcc:
            cc = args[0];
            sampler_layer_unset_modulation_amount(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_depth_curvecc:
            cc = args[0];
            sampler_layer_unset_modulation_curve_id(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_depth_smoothcc:
            cc = args[0];
            sampler_layer_unset_modulation_smooth(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_depth_stepcc:
            cc = args[0];
            sampler_layer_unset_modulation_step(l, &(struct sampler_modulation_key){(e->extra_int >> 16), cc, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_amountcc:
            cc = args[0];
            sampler_layer_unset_modulation_amount(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_curvecc:
            cc = args[0];
            sampler_layer_unset_modulation_curve_id(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_smoothcc:
            cc = args[0];
            sampler_layer_unset_modulation_smooth(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_stepcc:
            cc = args[0];
            sampler_layer_unset_modulation_step(l, &(struct sampler_modulation_key){cc, smsrc_none, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_amount:
            sampler_layer_unset_modulation_amount(l, &(struct sampler_modulation_key){(e->extra_int >> 16), smsrc_none, (e->extra_int & 0xFFFF)}, TRUE);
            return TRUE;
        case slpt_modulation:
            sampler_layer_unset_modulation_amount(l, &(struct sampler_modulation_key){(e->extra_int >> 8) & 0xFFF, (e->extra_int >> 20), (e->extra_int & 0xFF)}, TRUE);
            return TRUE;
        case slpt_voice_nif:
            sampler_layer_unset_voice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = e->extra_int }, TRUE);
            return TRUE;
        case slpt_prevoice_nif:
            sampler_layer_unset_prevoice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = e->extra_int }, TRUE);
            return TRUE;
        case slpt_voice_cc_nif:
            cc = args[0];
            sampler_layer_unset_voice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = cc + (e->extra_int << 8)}, TRUE);
            return TRUE;
        case slpt_prevoice_cc_nif:
            cc = args[0];
            sampler_layer_unset_prevoice_nif_value(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = cc + (e->extra_int << 8)}, TRUE);
            return TRUE;
        case slpt_voice_curvecc_nif:
            cc = args[0];
            sampler_layer_unset_voice_nif_curve_id(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = cc + (e->extra_int << 8)}, TRUE);
            return TRUE;
        case slpt_prevoice_curvecc_nif:
            cc = args[0];
            sampler_layer_unset_prevoice_nif_curve_id(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = cc + (e->extra_int << 8)}, TRUE);
            return TRUE;
        case slpt_voice_stepcc_nif:
            cc = args[0];
            sampler_layer_unset_voice_nif_step(l, &(struct sampler_noteinitfunc_key){ .notefunc_voice = e->extra_ptr, .variant = cc + (e->extra_int << 8)}, TRUE);
            return TRUE;
        case slpt_prevoice_stepcc_nif:
            cc = args[0];
            sampler_layer_unset_prevoice_nif_step(l, &(struct sampler_noteinitfunc_key){ .notefunc_prevoice = e->extra_ptr, .variant = cc + (e->extra_int << 8)}, TRUE);
            return TRUE;
        case slpt_generic_modulation:
            sampler_layer_unset_modulation_amount(l, &(struct sampler_modulation_key){args[0], args[1], args[2]}, TRUE);
            sampler_layer_unset_modulation_curve_id(l, &(struct sampler_modulation_key){args[0], args[1], args[2]}, TRUE);
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
            (!layer->parent || cbox_execute_on(fb, NULL, "/parent", "o", error, layer->parent)) && 
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
    if (!strcmp(cmd->command, "/new_child") && !strcmp(cmd->arg_types, ""))
    {
        // XXXKF needs a string argument perhaps
        if (layer->parent && layer->parent->parent && layer->parent->parent->parent)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot create a region within a region");
            return FALSE;
        }
        struct sampler_layer *l = sampler_layer_new(layer->module, layer->parent_program, layer);
        sampler_layer_data_finalize(&l->data, l->parent ? &l->parent->data : NULL, layer->parent_program);
        sampler_layer_reset_switches(l, l->module);
        sampler_layer_update(l);

        if (l->parent && l->parent->parent && l->parent->parent->parent)
        {
            sampler_program_add_layer(layer->parent_program, l);
            sampler_program_update_layers(layer->parent_program);
        }

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
            if (!cbox_execute_on(fb, NULL, "/child", "o", error, key))
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
    sampler_midi_curve_init(&ld->name);
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
    ld->name = NULL;

CBOX_CLASS_DEFINITION_ROOT(sampler_layer)

struct sampler_layer *sampler_layer_new(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent)
{
    struct sampler_layer *l = calloc(1, sizeof(struct sampler_layer));
    struct cbox_document *doc = CBOX_GET_DOCUMENT(parent_program);
    memset(l, 0, sizeof(struct sampler_layer));
    CBOX_OBJECT_HEADER_INIT(l, sampler_layer, doc);
    cbox_command_target_init(&l->cmd_target, sampler_layer_process_cmd, l);
    
    l->module = m;
    l->child_layers = g_hash_table_new(NULL, NULL);
    if (parent)
    {
        sampler_layer_data_clone(&l->data, &parent->data, FALSE);
        l->parent_program = parent_program;
        l->parent = parent;
        g_hash_table_replace(parent->child_layers, l, l);
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
    if (!parent)
    {
        // Systemwide default instead?
        sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){74, smsrc_none, smdest_cutoff}, 9600);
        sampler_layer_set_modulation_curve_id(l, &(struct sampler_modulation_key){74, smsrc_none, smdest_cutoff}, 1);
        sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){71, smsrc_none, smdest_resonance}, 12);
        sampler_layer_set_modulation_curve_id(l, &(struct sampler_modulation_key){71, smsrc_none, smdest_resonance}, 1);
        sampler_layer_set_modulation_amount(l, &(struct sampler_modulation_key){smsrc_pitchlfo, 1, smdest_pitch}, 100);
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
    memcpy(dst->name.values, src->name.values, sizeof(float) * 128); \
    if(copy_hasattr) \
        memcpy(dst->name.has_values, src->name.has_values, sizeof(src->name.has_values));
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
    sampler_cc_range_clone(&dst->name, src->name, copy_hasattr);

void sampler_cc_range_clone(struct sampler_cc_range **dest, struct sampler_cc_range *src, gboolean copy_hasattr)
{
    while(src)
    {
        struct sampler_cc_range *range = g_malloc0(sizeof(struct sampler_cc_range));
        memcpy(range, src, sizeof(struct sampler_cc_range));
        range->has_locc = copy_hasattr ? src->has_locc : FALSE;
        range->has_hicc = copy_hasattr ? src->has_hicc : FALSE;
        range->next = *dest;
        *dest = range;
        src = src->next;
    }
}

void sampler_layer_data_clone(struct sampler_layer_data *dst, const struct sampler_layer_data *src, gboolean copy_hasattr)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_CLONE)
    dst->modulations = sampler_modulation_clone(src->modulations, copy_hasattr);
    dst->voice_nifs = sampler_noteinitfunc_clone(src->voice_nifs, copy_hasattr);
    dst->prevoice_nifs = sampler_noteinitfunc_clone(src->prevoice_nifs, copy_hasattr);
    dst->eff_waveform = src->eff_waveform;
    if (dst->eff_waveform)
        cbox_waveform_ref(dst->eff_waveform);
}

void sampler_cc_range_cloneparent(struct sampler_cc_range **dest, struct sampler_cc_range *src)
{
    while(src)
    {
        struct sampler_cc_range *iter = *dest;
        while(iter && iter->cc_number != src->cc_number)
            iter = iter->next;
        if (iter)
        {
            if (!iter->has_locc)
                iter->locc = src->locc;
            if (!iter->has_hicc)
                iter->hicc = src->hicc;
        }
        else
        {
            struct sampler_cc_range *range = g_malloc0(sizeof(struct sampler_cc_range));
            memcpy(range, src, sizeof(struct sampler_cc_range));
            range->has_locc = 0;
            range->has_hicc = 0;
            range->next = *dest;
            *dest = range;
        }
        src = src->next;
    }
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
        if (!l->name.has_values[i]) \
            l->name.values[i] = parent ? parent->name.values[i] : SAMPLER_CURVE_GAP;
#define PROC_FIELDS_CLONEPARENT_dBamp PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_enum PROC_FIELDS_CLONEPARENT
#define PROC_FIELDS_CLONEPARENT_dahdsr(name, parname, index) \
        DAHDSR_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_lfo(name, parname, index) \
        LFO_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_eq(name, parname, index) \
        EQ_FIELDS(PROC_SUBSTRUCT_CLONEPARENT, name, l)
#define PROC_FIELDS_CLONEPARENT_ccrange(name, parname) \
    sampler_cc_range_cloneparent(&l->name, parent->name);

static void sampler_layer_data_getdefaults(struct sampler_layer_data *l, struct sampler_layer_data *parent)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_CLONEPARENT)
    // XXXKF: add handling for velcurve (XXXKF comment out of date - this is
    // already done in PROC_FIELDS_CLONEPARENT_midicurve, I believe)
}

void sampler_midi_curve_init(struct sampler_midi_curve *curve)
{
    for (uint32_t i = 0; i < 128; ++i)
        curve->values[i] = SAMPLER_CURVE_GAP;
    memset(curve->has_values, 0, 128);
}

void sampler_midi_curve_interpolate(const struct sampler_midi_curve *curve, float dest[128], float def_start, float def_end, gboolean is_quadratic)
{
    const float *src = curve->values;
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

static inline int sampler_filter_num_stages(float cutoff, enum sampler_filter_type fil_type)
{
    if (cutoff == -1)
        return 0;
    if (fil_type == sft_lp24hybrid || fil_type == sft_lp24 || fil_type == sft_lp24nr || fil_type == sft_hp24 || fil_type == sft_hp24nr || fil_type == sft_bp12)
        return 2;
    if (fil_type == sft_lp36)
        return 3;
    return 1;
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
    sampler_midi_curve_interpolate(&l->name, l->eff_##name, START_VALUE_##name, END_VALUE_##name, IS_QUADRATIC_##name);
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
        l->eff_is_silent = !l->sample || !strcmp(l->sample, "*silence");
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
        (l->lobpm == 0 && l->hibpm == NO_HI_BPM_VALUE) &&
        !l->cc && !l->eff_use_keyswitch;
    l->eff_use_xfcc = l->xfin_cc || l->xfout_cc;
    l->eff_use_channel_mixer = l->position != 0 || l->width != 100;
    l->eff_freq = (l->eff_waveform && l->eff_waveform->info.samplerate) ? l->eff_waveform->info.samplerate : 44100;
    l->eff_loop_mode = l->loop_mode;
    l->eff_use_filter_mods = l->cutoff != -1 || l->cutoff2 != -1;
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
    if (l->cutoff < 20)
        l->logcutoff = -1;
    else
        l->logcutoff = 1200.0 * log(l->cutoff / 440.0) / log(2) + 5700.0;

    if (l->cutoff2 < 20)
        l->logcutoff2 = -1;
    else
        l->logcutoff2 = 1200.0 * log(l->cutoff2 / 440.0) / log(2) + 5700.0;

    l->eq_bitmask = ((l->eq1.gain != 0 || l->eq1.vel2gain != 0) ? 1 : 0)
        | ((l->eq2.gain != 0 || l->eq2.vel2gain != 0) ? 2 : 0)
        | ((l->eq3.gain != 0 || l->eq3.vel2gain != 0) ? 4 : 0);
    l->mod_bitmask = 0;
    for(GSList *mod = l->modulations; mod; mod = g_slist_next(mod))
    {
        const struct sampler_modulation_key *mk = &((const struct sampler_modulation *)mod->data)->key;
        if (mk->dest >= smdest_eg_stage_start && mk->dest <= smdest_eg_stage_end)
            l->mod_bitmask |= slmb_ampeg_cc << ((mk->dest >> 4) & 3);
    }

    l->eff_use_prevoice = (l->delay || l->prevoice_nifs);
    l->eff_num_stages = sampler_filter_num_stages(l->cutoff, l->fil_type);
    l->eff_num_stages2 = sampler_filter_num_stages(l->cutoff2, l->fil2_type);

    l->resonance_scaled = pow(l->resonance_linearized, 1.f / l->eff_num_stages);
    l->resonance2_scaled = pow(l->resonance2_linearized, 1.f / l->eff_num_stages2);
}

void sampler_layer_reset_switches(struct sampler_layer *l, struct sampler_module *m)
{
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

struct sampler_layer *sampler_layer_new_from_section(struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent, const char *cfg_section)
{
    struct sampler_layer *l = sampler_layer_new(m, parent_program, parent ? parent : parent_program->global->default_child->default_child);
    sampler_layer_load_overrides(l, cfg_section);
    sampler_layer_data_finalize(&l->data, l->parent ? &l->parent->data : NULL, parent_program);
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
    //g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown SFZ property key: '%s'", key);
    //return FALSE;
    g_warning("Unknown SFZ property key: '%s'", key);
    return TRUE;
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
        if ((show_inherited || l->name.has_values[i]) && l->name.values[i] != SAMPLER_CURVE_GAP) \
            g_string_append_printf(outstr, " %s_%u=%s", #name, (unsigned)i, g_ascii_dtostr(floatbuf, floatbufsize, l->name.values[i])); \
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
    { \
        struct sampler_cc_range *range = l->name; \
        while (range) { \
            if (show_inherited || range->has_locc) \
                g_string_append_printf(outstr, " " #parname "locc%d=%d", range->cc_number, range->locc); \
            if (show_inherited || range->has_hicc) \
                g_string_append_printf(outstr, " " #parname "hicc%d=%d", range->cc_number, range->hicc); \
            range = range->next; \
        } \
    }

static const char *addrandom_variants[] = { "amp", "fil", "pitch" };
static const char *env_stages[] = { "delay", "attack", "hold", "decay", "sustain", "release", "start" };
static const char *modsrc_names[] = { "vel", "chanaft", "polyaft", "pitch", "pitcheg", "fileg", "ampeg", "pitchlfo", "fillfo", "amplfo", "" };
static const char *moddest_names[] = { "gain", "pitch", "cutoff", "resonance", "tonectl", "pan", "amplitude", "cutoff2", "resonance2", "pitchlfo_freq", "fillfo_freq", "amplfo_freq",
    "eq1_freq", "eq1_bw", "eq1_gain",
    "eq2_freq", "eq2_bw", "eq2_gain",
    "eq3_freq", "eq3_bw", "eq3_gain",
    };

static void mod_cc_attrib_to_string(GString *outstr, const char *attrib, const struct sampler_modulation_key *md, const char *floatbuf)
{
    if (md->dest >= smdest_eg_stage_start && md->dest <= smdest_eg_stage_end)
    {
        uint32_t param = md->dest - smdest_eg_stage_start;
        g_string_append_printf(outstr, " %seg_%s%s%d=%s", addrandom_variants[(param >> 4) & 3], env_stages[param & 15], attrib, md->src, floatbuf);
    }
    else if (md->src < smsrc_perchan_count)
    {
        g_string_append_printf(outstr, " %s%s%d=%s", moddest_names[md->dest], attrib, md->src, floatbuf);
    }
    else if ((md->src == smsrc_amplfo && md->dest == smdest_gain) ||
        (md->src == smsrc_fillfo && md->dest == smdest_cutoff) ||
        (md->src == smsrc_pitchlfo && md->dest == smdest_pitch))
    {
        if (md->src2 < EXT_CC_COUNT)
            g_string_append_printf(outstr, " %s_depth%s%d=%s", modsrc_names[md->src - smsrc_perchan_count], attrib, md->src2, floatbuf);
    }
    else if ((md->src == smsrc_ampenv && md->dest == smdest_gain) ||
        (md->src == smsrc_filenv && md->dest == smdest_cutoff) ||
        (md->src == smsrc_pitchenv && md->dest == smdest_pitch))
    {
        if (md->src2 < EXT_CC_COUNT)
            g_string_append_printf(outstr, " %s_depth%s%d=%s", modsrc_names[md->src - smsrc_perchan_count], attrib, md->src2, floatbuf);
    }
    else if ((md->src == smsrc_filenv && md->dest == smdest_cutoff2) ||
        (md->src == smsrc_fillfo && md->dest == smdest_cutoff2))
    {
        if (md->src2 < EXT_CC_COUNT)
            g_string_append_printf(outstr, " %s_depth2%s%d=%s", modsrc_names[md->src - smsrc_perchan_count], attrib, md->src2, floatbuf);
    }
    else
        assert(md->src2 >= EXT_CC_COUNT);
}

static void nif_attrib_to_string(GString *outstr, const char *attrib, const struct sampler_noteinitfunc *nd, const char *floatbuf)
{
    int v = nd->key.variant;
    if (nd->value.value)
        g_string_append_printf(outstr, " %s_cc%d=%s", attrib, v, floatbuf);
    if (nd->value.curve_id)
        g_string_append_printf(outstr, " %s_curvecc%d=%d", attrib, v, nd->value.curve_id);
    if (nd->value.step)
    {
        char floatbuf2[G_ASCII_DTOSTR_BUF_SIZE];
        int floatbufsize = G_ASCII_DTOSTR_BUF_SIZE;
        g_ascii_dtostr(floatbuf2, floatbufsize, nd->value.step);
        g_string_append_printf(outstr, " %s_stepcc%d=%s", attrib, v, floatbuf2);
    }
}

gchar *sampler_layer_to_string(struct sampler_layer *lr, gboolean show_inherited)
{
    struct sampler_layer_data *l = &lr->data;
    GString *outstr = g_string_sized_new(200);
    const char *tmpstr;
    char floatbuf[G_ASCII_DTOSTR_BUF_SIZE];
    int floatbufsize = G_ASCII_DTOSTR_BUF_SIZE;
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_FILEPTR)
    
    for(GSList *nif = l->voice_nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *nd = nif->data;
        if (!nd->value.has_value && !nd->value.has_curve && !nd->value.has_step && !show_inherited)
            continue;
        #define PROC_ENVSTAGE_NAME(name, index, def_value) #name, 
        static const char *env_stages[] = { DAHDSR_FIELDS(PROC_ENVSTAGE_NAME) "start" };
        uint32_t v = nd->key.variant;
        g_ascii_dtostr(floatbuf, floatbufsize, nd->value.value);
        
        if (nd->key.notefunc_voice == sampler_nif_addrandom && v >= 0 && v <= 2)
            g_string_append_printf(outstr, " %s_random=%s", addrandom_variants[v], floatbuf);
        else if (nd->key.notefunc_voice == sampler_nif_vel2pitch)
            g_string_append_printf(outstr, " pitch_veltrack=%s", floatbuf);
        else if (nd->key.notefunc_voice == sampler_nif_vel2reloffset)
            g_string_append_printf(outstr, " reloffset_veltrack=%s", floatbuf);
        else if (nd->key.notefunc_voice == sampler_nif_cc2reloffset)
            nif_attrib_to_string(outstr, "reloffset", nd, floatbuf);
        else if (nd->key.notefunc_voice == sampler_nif_vel2offset)
            g_string_append_printf(outstr, " offset_veltrack=%s", floatbuf);
        else if (nd->key.notefunc_voice == sampler_nif_cc2offset)
            nif_attrib_to_string(outstr, "offset", nd, floatbuf);
        else if (nd->key.notefunc_voice == sampler_nif_vel2env && (v & 15) >= snif_env_delay && (v & 15) <= snif_env_start && ((v >> 4) & 3) < 3)
            g_string_append_printf(outstr, " %seg_vel2%s=%s", addrandom_variants[v >> 4], env_stages[1 + (v & 15)], floatbuf);
        else
            assert(0); // unknown NIF
    }
    for(GSList *nif = l->prevoice_nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *nd = nif->data;
        if (!nd->value.has_value && !nd->value.has_curve && !nd->value.has_step && !show_inherited)
            continue;
        g_ascii_dtostr(floatbuf, floatbufsize, nd->value.value);

        if (nd->key.notefunc_prevoice == sampler_nif_cc2delay)
            nif_attrib_to_string(outstr, "delay", nd, floatbuf);
        else if (nd->key.notefunc_prevoice == sampler_nif_addrandomdelay)
            g_string_append_printf(outstr, " delay_random=%s", floatbuf);
        else
            assert(0); // unknown NIF
    }
    for(GSList *mod = l->modulations; mod; mod = mod->next)
    {
        struct sampler_modulation *md = mod->data;
        const struct sampler_modulation_key *mk = &md->key;
        const struct sampler_modulation_value *mv = &md->value;
        if (mv->has_curve || show_inherited)
        {
            g_ascii_dtostr(floatbuf, floatbufsize, mv->curve_id);
            mod_cc_attrib_to_string(outstr, "_curvecc", mk, floatbuf);
        }
        if (mv->has_smooth || show_inherited)
        {
            g_ascii_dtostr(floatbuf, floatbufsize, mv->smooth);
            mod_cc_attrib_to_string(outstr, "_smoothcc", mk, floatbuf);
        }
        if (mv->has_step || show_inherited)
        {
            g_ascii_dtostr(floatbuf, floatbufsize, mv->step);
            mod_cc_attrib_to_string(outstr, "_stepcc", mk, floatbuf);
        }
        if (mv->has_amount || show_inherited)
        {
            gboolean is_egcc = mk->dest >= smdest_eg_stage_start && mk->dest <= smdest_eg_stage_end;
            gboolean is_lfofreq = mk->dest >= smdest_pitchlfo_freq && mk->dest <= smdest_eq3_gain;
            g_ascii_dtostr(floatbuf, floatbufsize, mv->amount);

            if (mk->src2 == smsrc_none)
            {
                if (is_egcc)
                {
                    uint32_t param = mk->dest - smdest_eg_stage_start;
                    g_string_append_printf(outstr, " %seg_%scc%d=%s", addrandom_variants[(param >> 4) & 3], env_stages[param & 15], mk->src, floatbuf);
                    continue;
                }
                if (mk->src < smsrc_perchan_count)
                {
                    // Inconsistency: cutoff_cc5 but amplfo_freqcc5
                    if (is_lfofreq)
                        g_string_append_printf(outstr, " %scc%d=%s", moddest_names[mk->dest], mk->src, floatbuf);
                    else
                        g_string_append_printf(outstr, " %s_cc%d=%s", moddest_names[mk->dest], mk->src, floatbuf);
                    continue;
                }
                if (mk->src < smsrc_perchan_count + sizeof(modsrc_names) / sizeof(modsrc_names[0]))
                {
                    if ((mk->src == smsrc_filenv && mk->dest == smdest_cutoff) ||
                        (mk->src == smsrc_pitchenv && mk->dest == smdest_pitch) ||
                        (mk->src == smsrc_amplfo && mk->dest == smdest_gain) ||
                        (mk->src == smsrc_fillfo && mk->dest == smdest_cutoff) ||
                        (mk->src == smsrc_pitchlfo && mk->dest == smdest_pitch))
                        g_string_append_printf(outstr, " %s_depth=%s", modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    else if ((mk->src == smsrc_filenv && mk->dest == smdest_cutoff2) ||
                        (mk->src == smsrc_fillfo && mk->dest == smdest_cutoff2))
                        g_string_append_printf(outstr, " %s_depth2=%s", modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    else if (is_lfofreq)
                        g_string_append_printf(outstr, " %s%s=%s", moddest_names[mk->dest], modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    else
                        g_string_append_printf(outstr, " %s_%s=%s", moddest_names[mk->dest], modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    continue;
                }
            }
            if ((mk->src == smsrc_amplfo && mk->dest == smdest_gain) ||
                (mk->src == smsrc_fillfo && mk->dest == smdest_cutoff) ||
                (mk->src == smsrc_pitchlfo && mk->dest == smdest_pitch))
            {
                switch(mk->src2)
                {
                case smsrc_chanaft:
                case smsrc_polyaft:
                    g_string_append_printf(outstr, " %s_depth%s=%s", modsrc_names[mk->src - smsrc_perchan_count], modsrc_names[mk->src2 - smsrc_perchan_count], floatbuf);
                    continue;
                case smsrc_none:
                    g_string_append_printf(outstr, " %s_depth=%s", modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    continue;
                default:
                    if (mk->src2 < EXT_CC_COUNT)
                    {
                        g_string_append_printf(outstr, " %s_depthcc%d=%s", modsrc_names[mk->src - smsrc_perchan_count], mk->src2, floatbuf);
                        continue;
                    }
                    break;
                }
            }
            if ((mk->src == smsrc_ampenv && mk->dest == smdest_gain) ||
                (mk->src == smsrc_filenv && mk->dest == smdest_cutoff) ||
                (mk->src == smsrc_pitchenv && mk->dest == smdest_pitch))
            {
                if (mk->src2 == smsrc_vel)
                {
                    g_string_append_printf(outstr, " %s_vel2depth=%s", modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    continue;
                }
                if (mk->src2 == smsrc_none)
                {
                    g_string_append_printf(outstr, " %s_depth=%s", modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    continue;
                }
                if (mk->src2 < EXT_CC_COUNT)
                {
                    g_string_append_printf(outstr, " %s_depthcc%d=%s", modsrc_names[mk->src - smsrc_perchan_count], mk->src2, floatbuf);
                    continue;
                }
            }
            if (mk->src == smsrc_filenv && mk->dest == smdest_cutoff2)
            {
                if (mk->src2 == smsrc_vel)
                {
                    g_string_append_printf(outstr, " %s_vel2depth2=%s", modsrc_names[mk->src - smsrc_perchan_count], floatbuf);
                    continue;
                }
                assert(mk->src2 != smsrc_none);
                if (mk->src2 < EXT_CC_COUNT)
                {
                    g_string_append_printf(outstr, " %s_depth2cc%d=%s", modsrc_names[mk->src - smsrc_perchan_count], mk->src2, floatbuf);
                    continue;
                }
            }
            if (mk->src == smsrc_fillfo && mk->dest == smdest_cutoff2)
            {
                assert(mk->src2 != smsrc_none);
                if (mk->src2 < EXT_CC_COUNT)
                {
                    g_string_append_printf(outstr, " %s_depth2cc%d=%s", modsrc_names[mk->src - smsrc_perchan_count], mk->src2, floatbuf);
                    continue;
                }
            }
            g_string_append_printf(outstr, " genericmod_%d_%d_%d_%d=%s", mk->src, mk->src2, mk->dest, mv->curve_id, floatbuf);
        }
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

void sampler_cc_range_destroy(struct sampler_cc_range *range)
{
    while(range)
    {
        struct sampler_cc_range *next = range->next;
        g_free(range);
        range = next;
    }
}

void sampler_layer_data_close(struct sampler_layer_data *l)
{
    sampler_cc_range_destroy(l->cc);
    sampler_cc_range_destroy(l->on_cc);
    sampler_cc_range_destroy(l->xfin_cc);
    sampler_cc_range_destroy(l->xfout_cc);
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
    struct sampler_module *m, struct sampler_program *parent_program, struct sampler_layer *parent)
{
    struct sampler_layer *l = sampler_layer_new(m, parent_program, parent);
    sampler_layer_data_clone(&l->data, &layer->data, TRUE);
    sampler_layer_reset_switches(l, m);
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
    gboolean is_child_a_region = layer->parent && layer->parent->parent;
    while(g_hash_table_iter_next(&iter, &key, &value))
    {
        struct sampler_layer *chl = sampler_layer_new_clone(key, m, parent_program, l);
        g_hash_table_insert(l->child_layers, chl, NULL);
        if (key == layer->default_child)
            l->default_child = chl;
        if (is_child_a_region)
            sampler_program_add_layer(parent_program, chl);
    }

    return l;
}

void sampler_layer_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct sampler_layer *l = CBOX_H2O(objhdr);
    struct sampler_program *prg = l->parent_program;
    assert(g_hash_table_size(l->child_layers) == 0);

    if (l->parent)
    {
        g_hash_table_remove(l->parent->child_layers, l);
        if (prg && prg->rll)
        {
            sampler_program_delete_layer(prg, l);
            sampler_program_update_layers(l->parent_program);
        }
        l->parent = NULL;
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
    sampler_layer_data_finalize(cmd->new_data, cmd->layer->parent ? &cmd->layer->parent->data : NULL, cmd->layer->parent_program);
    if (cmd->layer->runtime == NULL)
    {
        // initial update of the layer, so none of the voices need updating yet
        // because the layer hasn't been allocated to any voice
        cmd->layer->runtime = cmd->new_data;
        free(cmd);
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

