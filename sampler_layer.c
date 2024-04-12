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

static inline void sampler_noteinitfunc_dump_one(const struct sampler_noteinitfunc *nif)
{
    printf("%p(%d) = %f\n", nif->key.notefunc_voice, nif->key.variant, nif->value.value);
}

//////////////////////////////////////////////////////////////////////////////////////////////////

static inline gboolean sampler_cc_range_key_equal(const struct sampler_cc_range_key *k1, const struct sampler_cc_range_key *k2)
{
    return k1->cc_number == k2->cc_number;
}

static inline void sampler_cc_range_dump_one(const struct sampler_cc_range *ccrange)
{
    printf("CC%d in [%c%d, %c%d]\n", (int)ccrange->key.cc_number, ccrange->value.has_locc ? '!' : '.', (int)ccrange->value.locc, ccrange->value.has_hicc ? '!' : '.', (int)ccrange->value.hicc);
}

//////////////////////////////////////////////////////////////////////////////////////////////////

static inline gboolean sampler_flex_lfo_key_equal(const struct sampler_flex_lfo_key *k1, const struct sampler_flex_lfo_key *k2)
{
    return k1->id == k2->id;
}

static inline void sampler_flex_lfo_dump_one(const struct sampler_flex_lfo *lfo)
{
    printf("LFO%d (freq %s %f, delay %s %f, fade %s %f, wave %s %d)\n",
        (int)lfo->key.id,
        lfo->value.has_freq ? "(local)" : "(inherited)", lfo->value.freq,
        lfo->value.has_delay ? "(local)" : "(inherited)", lfo->value.delay,
        lfo->value.has_fade ? "(local)" : "(inherited)", lfo->value.fade,
        lfo->value.has_wave ? "(local)" : "(inherited)", lfo->value.wave
    );
}

//////////////////////////////////////////////////////////////////////////////////////////////////

#define SAMPLER_COLL_FUNC_DUMP(sname) \
    void sname##_dump(const struct sname *p) \
    { \
        for(; p; p = p->next) \
            sname##_dump_one(p); \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_DUMP)

#define SAMPLER_COLL_FUNC_FIND(sname) \
    static struct sname *sname##_find(struct sname *list, const struct sname##_key *key) \
    { \
        for(struct sname *p = list; p; p = p->next) \
        { \
            struct sname##_key *dkey = &p->key; \
            if (sname##_key_equal(dkey, key)) \
                return p; \
        } \
        return NULL; \
    } \
    static struct sname *sname##_find2(struct sname **list_ptr, const struct sname##_key *key, struct sname ***link_ptr) \
    { \
        for(struct sname **pp = list_ptr; *pp; pp = &(*pp)->next) \
        { \
            struct sname##_key *dkey = &(*pp)->key; \
            if (sname##_key_equal(dkey, key)) \
            { \
                if (link_ptr) \
                    *link_ptr = pp; \
                return *pp; \
            } \
        } \
        return NULL; \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_FIND)

#define SAMPLER_COLL_FIELD_INIT(name, has_name, type, init_value) \
    d->value.name = init_value; \
    d->value.has_name = FALSE;

#define SAMPLER_COLL_FUNC_ADD(sname) \
static struct sname *sname##_add(struct sname **list_ptr, const struct sname##_key *key) \
{ \
    struct sname *d = sname##_find(*list_ptr, key); \
    if (d) \
        return d; \
    d = g_malloc0(sizeof(struct sname)); \
    d->key = *key; \
    SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_INIT)\
    d->next = *list_ptr; \
    *list_ptr = d; \
    return d; \
}

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_ADD)

#define SAMPLER_COLL_FUNC_DESTROY(sname) \
static void sname##s_destroy(struct sname *list_ptr) \
{ \
    while(list_ptr) \
    { \
        struct sname *p = list_ptr->next; \
        g_free(list_ptr); \
        list_ptr = p; \
    } \
}

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_DESTROY)

#define SAMPLER_COLL_FIELD_ISNULL(name, has_name, type, init_value) \
    if (d->name != init_value || d->has_name) \
        return FALSE;

#define SAMPLER_COLL_FUNC_ISNULL(sname) \
    static inline gboolean sname##_is_null(const struct sname##_value *d) \
    { \
        SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_ISNULL) \
        return TRUE; \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_ISNULL)

// sampler_modulation_set_amount etc.
#define SAMPLER_COLL_FIELD_SETTER(name, has_name, type, init_value, sname) \
    struct sname *sname##_set_##name##_by_offset(struct sampler_layer *l, uint32_t offset, const struct sname##_key *key, gboolean set_local_value, type value) \
    { \
        void *vl = &l->data; \
        struct sname **list_ptr = vl + offset; \
        struct sname *dstm = sname##_add(list_ptr, key); \
        if (!set_local_value && dstm->value.has_name) \
            return dstm; \
        dstm->value.has_name = set_local_value; \
        dstm->value.name = value; \
        return dstm; \
    }

#define SAMPLER_COLL_FUNC_SETTERS(sname) \
    SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_SETTER, sname)

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_SETTERS)

#define SAMPLER_COLL_FIELD_UNSET(name, has_name, type, init_value, sname) \
    if ((unset_mask & (1 << sname##_value_field_##name)) && d->value.has_name == remove_local) \
    { \
        d->value.name = parent ? parent->value.name : init_value; \
        d->value.has_name = FALSE; \
    } \

#define SAMPLER_COLL_FIELD_KEY_ENUM_VALUE(name, has_name, type, init_value, sname) \
    sname##_value_field_##name,

#define SAMPLER_COLL_FUNC_UNSET(sname) \
    enum {  \
        SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_KEY_ENUM_VALUE, sname) \
    }; \
    static gboolean sname##_unset_by_offset(struct sampler_layer *l, uint32_t offset, const struct sname##_key *key, gboolean remove_local, uint32_t unset_mask) \
    { \
        void *vl = &l->data, *vp = l->parent ? &l->parent->data : NULL; \
        struct sname **list_ptr = vl + offset; \
        struct sname **parent_list_ptr = vp ? vp + offset : NULL; \
        struct sname **link_ptr = NULL; \
        struct sname *d = sname##_find2(list_ptr, key, &link_ptr); \
        if (!d) \
            return FALSE; \
        struct sname *parent = remove_local && *parent_list_ptr != NULL ? sname##_find(*parent_list_ptr, key) : NULL; \
        SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_UNSET, sname) \
        /* Delete if it's all default values and it's not overriding anything */ \
        if (sname##_is_null(&d->value)) {\
            *link_ptr = d->next; \
            g_free(d); \
        } \
        return TRUE; \
    }

SAMPLER_COLL_LIST(SAMPLER_COLL_FUNC_UNSET)

#define SAMPLER_COLL_FIELD_ZEROHASATTR(name, has_name, type, init_value) \
    dstv->value.has_name = FALSE;
#define SAMPLER_COLL_FUNC_CLONE(sname) \
    static struct sname *sname##_clone(struct sname *src, gboolean copy_hasattr) \
    { \
        struct sname *dst = NULL, **last = &dst;\
        for(const struct sname *srcv = src; srcv; srcv = srcv->next) \
        { \
            struct sname *dstv = g_malloc(sizeof(struct sname)); \
            memcpy(dstv, srcv, sizeof(struct sname)); \
            if (!copy_hasattr) \
            { \
                SAMPLER_COLL_FIELD_LIST_##sname(SAMPLER_COLL_FIELD_ZEROHASATTR) \
            } \
            *last = dstv; \
            dstv->next = NULL; \
            last = &dstv->next; \
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
    slpt_mod_amount, // src (or CC) * src2 (or CC) -> dest
    slpt_mod_curveid,
    slpt_mod_smooth,
    slpt_mod_step,
    slpt_generic_modulation,
    // note init functions
    slpt_voice_nif,
    slpt_prevoice_nif,
    slpt_flex_lfo,
    slpt_flex_lfo_dest,
    slpt_nonfunctional,
    slpt_reserved,
};

struct sampler_layer_param_entry
{
    const char *name;
    size_t offset;
    enum sampler_layer_param_type type;
    double def_value;
    uint64_t extra_int;
    void *extra_ptr;
    void (*set_has_value)(struct sampler_layer_data *, gboolean);
    gboolean (*get_has_value)(struct sampler_layer_data *);
};

#define MODSRC_CC 0xFFF
#define smsrc_CC MODSRC_CC

#define ENCODE_MOD(src, src2, dst) ((((uint32_t)(src) & 0xFFFU) | (((uint32_t)(src2) & 0xFFFU) << 12U) | ((uint32_t)(dst) << 24U)))

#define PROC_SUBSTRUCT_FIELD_SETHASFUNC(name, index, def_value, parent) \
    static void sampler_layer_data_##parent##_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##parent.name = value; } \
    static gboolean sampler_layer_data_##parent##_get_has_##name(struct sampler_layer_data *l) { return l->has_##parent.name; }

#define PROC_FIELD_SETHASFUNC(type, name, default_value) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##name = value; } \
    static gboolean sampler_layer_data_get_has_##name(struct sampler_layer_data *l) { return l->has_##name; }
#define PROC_FIELD_SETHASFUNC_string(name) \
    static void sampler_layer_data_set_has_##name(struct sampler_layer_data *l, gboolean value) { l->has_##name = value; } \
    static gboolean sampler_layer_data_get_has_##name(struct sampler_layer_data *l) { return l->has_##name; }
#define PROC_FIELD_SETHASFUNC_dBamp(type, name, default_value) \
    PROC_FIELD_SETHASFUNC(type, name, default_value)
#define PROC_FIELD_SETHASFUNC_enum(type, name, default_value) \
    PROC_FIELD_SETHASFUNC(type, name, default_value)
#define PROC_FIELD_SETHASFUNC_dahdsr(field, name, default_value) \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_FIELD_SETHASFUNC, field)
#define PROC_FIELD_SETHASFUNC_lfo(field, name, default_value) \
    LFO_FIELDS(PROC_SUBSTRUCT_FIELD_SETHASFUNC, field)
#define PROC_FIELD_SETHASFUNC_eq(field, name, default_value) \
    EQ_FIELDS(PROC_SUBSTRUCT_FIELD_SETHASFUNC, field)
#define PROC_FIELD_SETHASFUNC_ccrange(name, parname)
#define PROC_FIELD_SETHASFUNC_midicurve(name) \
    static gboolean sampler_layer_data_set_get_has_##name(struct sampler_layer_data *l, uint32_t index, int value) \
    { \
        if (value != -1) \
            l->name.has_values[index] = value; \
        return l->name.has_values[index]; \
    }

SAMPLER_FIXED_FIELDS(PROC_FIELD_SETHASFUNC)

#define LOFS(field) offsetof(struct sampler_layer_data, field)

#define FIELD_MOD(name, param, src, src2, dest) \
    { name, LOFS(modulations), slpt_mod_##param, 0, ENCODE_MOD(smsrc_##src, smsrc_##src2, smdest_##dest), NULL, NULL, NULL },
#define FIELD_AMOUNT(name, src, dest) \
    FIELD_MOD(name, amount, src, none, dest)
#define FIELD_AMOUNT_CC(name, dest) \
    FIELD_ALIAS(name "cc#", name "_oncc#") \
    FIELD_MOD(name "_oncc#", amount, CC, none, dest) \
    FIELD_MOD(name "_curvecc#", curveid, CC, none, dest) \
    FIELD_MOD(name "_smoothcc#", smooth, CC, none, dest) \
    FIELD_MOD(name "_stepcc#", step, CC, none, dest)
#define FIELD_AMOUNT_CC_(name, dest) \
    FIELD_ALIAS(name "_cc#", name "_oncc#") \
    FIELD_MOD(name "_oncc#", amount, CC, none, dest) \
    FIELD_MOD(name "_curvecc#", curveid, CC, none, dest) \
    FIELD_MOD(name "_smoothcc#", smooth, CC, none, dest) \
    FIELD_MOD(name "_stepcc#", step, CC, none, dest)
#define FIELD_VOICE_NIF(name, nif, variant) \
    { name, LOFS(voice_nifs), slpt_voice_nif, 0, variant, nif, NULL, NULL },
#define FIELD_PREVOICE_NIF(name, nif, variant) \
    { name, LOFS(prevoice_nifs), slpt_prevoice_nif, 0, variant, nif, NULL, NULL },
#define FIELD_ALIAS(alias, name) \
    { alias, -1, slpt_alias, 0, 0, name, NULL, NULL },
#define FIELD_NONFUNCTIONAL(name) \
    { name, -1, slpt_nonfunctional, 0, 0, NULL, NULL, NULL },

#define PROC_SUBSTRUCT_FIELD_DESCRIPTOR(name, index, def_value, parent, parent_name, parent_index, parent_struct) \
    { #parent_name "_" #name, offsetof(struct sampler_layer_data, parent) + offsetof(struct parent_struct, name), slpt_float, def_value, parent_index * 100 + index, NULL, sampler_layer_data_##parent##_set_has_##name, sampler_layer_data_##parent##_get_has_##name }, \

#define PROC_SUBSTRUCT_FIELD_DESCRIPTOR_DAHDSR(name, index, def_value, parent, parent_name, parent_index, parent_struct) \
    { #parent_name "_" #name, offsetof(struct sampler_layer_data, parent) + offsetof(struct parent_struct, name), slpt_float, def_value, parent_index * 100 + index, NULL, sampler_layer_data_##parent##_set_has_##name, sampler_layer_data_##parent##_get_has_##name }, \
    FIELD_VOICE_NIF(#parent_name "_vel2" #name, sampler_nif_vel2env, (parent_index << 4) + snif_env_##name) \
    FIELD_AMOUNT_CC(#parent_name "_" #name, ampeg_stage + (parent_index << 4) + snif_env_##name) \

#define PROC_FIELD_DESCRIPTOR(type, name, default_value) \
    { #name, LOFS(name), slpt_##type, default_value, 0, NULL, sampler_layer_data_set_has_##name, sampler_layer_data_get_has_##name },
#define PROC_FIELD_DESCRIPTOR_dBamp(type, name, default_value) \
    { #name, LOFS(name), slpt_##type, default_value, 0, NULL, sampler_layer_data_set_has_##name, sampler_layer_data_get_has_##name },
#define PROC_FIELD_DESCRIPTOR_string(name) \
    { #name, LOFS(name), slpt_string, 0, LOFS(name##_changed), NULL, sampler_layer_data_set_has_##name, sampler_layer_data_get_has_##name },
#define PROC_FIELD_DESCRIPTOR_enum(enumtype, name, default_value) \
    { #name, LOFS(name), slpt_enum, (double)(enum enumtype)default_value, 0, enumtype##_from_string, sampler_layer_data_set_has_##name, sampler_layer_data_get_has_##name },
#define PROC_FIELD_DESCRIPTOR_midicurve(name) \
    { #name "_#", LOFS(name), slpt_midicurve, 0, 0, (void *)sampler_layer_data_set_get_has_##name, NULL, NULL },

#define FIELD_DEPTH_SET(name, dest, attrib) \
    FIELD_ALIAS(#name attrib "cc#", #name attrib "_oncc#") \
    FIELD_MOD(#name attrib "_oncc#", amount, name, CC, dest) \
    FIELD_MOD(#name attrib "_curvecc#", curveid, name, CC, dest) \
    FIELD_MOD(#name attrib "_smoothcc#", smooth, name, CC, dest) \
    FIELD_MOD(#name attrib "_stepcc#", step, name, CC, dest) \
    FIELD_MOD(#name attrib "polyaft", amount, name, polyaft, dest) \
    FIELD_MOD(#name attrib "chanaft", amount, name, chanaft, dest) \
    FIELD_MOD(#name attrib, amount, name, none, dest) \

#define PROC_FIELD_DESCRIPTOR_dahdsr(field, name, index) \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR_DAHDSR, field, name, index, cbox_dahdsr) \
    FIELD_DEPTH_SET(name, from_##name, "_depth") \
    FIELD_MOD(#name "_vel2depth", amount, name, vel, from_##name)

#define PROC_FIELD_DESCRIPTOR_lfo(field, name, index) \
    LFO_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR, field, name, index, sampler_lfo_params) \
    FIELD_AMOUNT(#name "_freqpolyaft", polyaft, name##_freq) \
    FIELD_AMOUNT(#name "_freqchanaft", chanaft, name##_freq) \
    FIELD_AMOUNT_CC(#name "_freq", name##_freq) \
    FIELD_DEPTH_SET(name, from_##name, "_depth")

#define PROC_FIELD_DESCRIPTOR_eq(field, name, index) \
    EQ_FIELDS(PROC_SUBSTRUCT_FIELD_DESCRIPTOR, field, name, index, sampler_eq_params) \
    FIELD_AMOUNT_CC(#name "_freq", name##_freq) \
    FIELD_AMOUNT_CC(#name "_bw", name##_bw) \
    FIELD_AMOUNT_CC(#name "_gain", name##_gain)

#define PROC_FIELD_DESCRIPTOR_ccrange(field, parname) \
    { #parname "locc#", LOFS(field), slpt_ccrange, 0, 0, NULL, NULL, NULL }, \
    { #parname "hicc#", LOFS(field), slpt_ccrange, 127, 1, NULL, NULL, NULL },

#define FIELD_FLEX_LFO(name, field) \
    { name, LOFS(flex_lfos), slpt_flex_lfo, 0, sampler_flex_lfo_value_field_##field, NULL, NULL, NULL },

#define FIELD_FLEX_LFO_DEST(name, dest) \
    { name, LOFS(modulations), slpt_flex_lfo_dest, 0, smdest_##dest, NULL, NULL, NULL },

#define NIF_VARIANT_CC 0x01000000
#define NIF_VARIANT_CURVECC 0x02000000
#define NIF_VARIANT_STEPCC 0x03000000
#define NIF_VARIANT_MASK 0xFF000000

struct sampler_layer_param_entry sampler_layer_params[] = {
    SAMPLER_FIXED_FIELDS(PROC_FIELD_DESCRIPTOR)

    FIELD_AMOUNT("cutoff_chanaft", chanaft, cutoff)
    FIELD_AMOUNT("resonance_chanaft", chanaft, resonance)
    FIELD_AMOUNT("cutoff_polyaft", polyaft, cutoff)
    FIELD_AMOUNT("resonance_polyaft", polyaft, resonance)

    FIELD_DEPTH_SET(fileg, cutoff2, "_depth2")
    FIELD_MOD("fileg_vel2depth2", amount, fileg, vel, cutoff2)

    FIELD_DEPTH_SET(fillfo, cutoff2, "_depth2")

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
    FIELD_PREVOICE_NIF("delay_cc#", sampler_nif_cc2delay, NIF_VARIANT_CC)
    FIELD_PREVOICE_NIF("delay_curvecc#", sampler_nif_cc2delay, NIF_VARIANT_CURVECC)
    FIELD_PREVOICE_NIF("delay_stepcc#", sampler_nif_cc2delay, NIF_VARIANT_STEPCC)
    FIELD_VOICE_NIF("reloffset_cc#", sampler_nif_cc2reloffset, NIF_VARIANT_CC)
    FIELD_VOICE_NIF("reloffset_curvecc#", sampler_nif_cc2reloffset, NIF_VARIANT_CURVECC)
    FIELD_VOICE_NIF("reloffset_stepcc#", sampler_nif_cc2reloffset, NIF_VARIANT_STEPCC)
    FIELD_VOICE_NIF("offset_cc#", sampler_nif_cc2offset, NIF_VARIANT_CC)
    FIELD_VOICE_NIF("offset_curvecc#", sampler_nif_cc2offset, NIF_VARIANT_CURVECC)
    FIELD_VOICE_NIF("offset_stepcc#", sampler_nif_cc2offset, NIF_VARIANT_STEPCC)

    FIELD_FLEX_LFO("lfo#_freq", freq)
    FIELD_FLEX_LFO("lfo#_delay", delay)
    FIELD_FLEX_LFO("lfo#_fade", fade)
    FIELD_FLEX_LFO("lfo#_wave", wave)
    FIELD_FLEX_LFO("lfo#_phase", phase)
    FIELD_FLEX_LFO("lfo#_count", count)

    FIELD_FLEX_LFO_DEST("lfo#_cutoff", cutoff)
    FIELD_FLEX_LFO_DEST("lfo#_resonance", resonance)
    FIELD_FLEX_LFO_DEST("lfo#_cutoff2", cutoff2)
    FIELD_FLEX_LFO_DEST("lfo#_resonance2", resonance2)
    FIELD_FLEX_LFO_DEST("lfo#_pitch", pitch)
    FIELD_FLEX_LFO_DEST("lfo#_tune", pitch)
    FIELD_FLEX_LFO_DEST("lfo#_tonectl", tonectl)
    FIELD_FLEX_LFO_DEST("lfo#_pan", pan)
    FIELD_FLEX_LFO_DEST("lfo#_amplitude", amplitude)
    FIELD_FLEX_LFO_DEST("lfo#_gain", gain)

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

    //NONFUNCTIONAL Opcodes can still be looked up and used as strings in a GUI
    FIELD_NONFUNCTIONAL("region_label") //ARIA
    FIELD_NONFUNCTIONAL("group_label") //ARIA
    FIELD_NONFUNCTIONAL("master_label") //ARIA
    FIELD_NONFUNCTIONAL("global_label") //ARIA
    FIELD_NONFUNCTIONAL("sw_label") //Keyswitch. ARIA
    //label_cc and label_key are in sfzloader.c because they are under <control>

    //Ignore List  #TODO
    FIELD_NONFUNCTIONAL("ampeg_dynamic") //ARIA

    { "genericmod_#_#_#_#", -1, slpt_generic_modulation, 0, 0, NULL, NULL, NULL },
};
#define NPARAMS (sizeof(sampler_layer_params) / sizeof(sampler_layer_params[0]))

static void sampler_layer_apply_unknown(struct sampler_layer *l, const char *key, const char *value);

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
        if (i)
        {
            struct sampler_layer_param_entry *prev_e = &sampler_layer_params[i - 1];
            if (!strcmp(e->name, prev_e->name))
            {
                printf("Duplicate parameter %s\n", e->name);
                assert(FALSE);
            }
        }
    }
}

// This only works for setting. Unsetting is slightly different.
static gboolean override_logic(gboolean is_equal, gboolean has_value, gboolean set_local_value)
{
    if (!set_local_value && has_value) // Do not override locally set values
        return FALSE;
    // Override if a value or a inherited value is replaced with a local setting.
    return (!is_equal) || (set_local_value != has_value);
}

static inline void mod_key_decode(uint64_t extra_int, const uint32_t *args, struct sampler_modulation_key *mod_key)
{
    uint32_t modsrc = (extra_int & 0xFFF);
    uint32_t modsrc2 = ((extra_int >> 12) & 0xFFF);
    if (modsrc == MODSRC_CC)
        modsrc = args[0];
    if (modsrc2 == MODSRC_CC)
        modsrc2 = args[0];
    mod_key->src = modsrc;
    mod_key->src2 = modsrc2;
    mod_key->dest = ((extra_int >> 24) & 0xFF);
}

static inline void nif_key_decode(uint64_t extra_int, void *extra_ptr, const uint32_t *args, struct sampler_noteinitfunc_key *nif_key)
{
    uint32_t variant = extra_int &~ NIF_VARIANT_MASK;
    nif_key->notefunc_voice = extra_ptr;
    if (extra_int & NIF_VARIANT_MASK)
    {
        int cc = args[0] & 255;
        variant = cc + (variant << 8);
    }
    nif_key->variant = variant;
}

static inline void flex_lfo_key_decode(const uint32_t *args, struct sampler_flex_lfo_key *flex_lfo_key)
{
    flex_lfo_key->id = args[0];
}

#define OVERRIDE_LOGIC(type) override_logic(!memcmp(p, data_ptr, sizeof(type)), e->get_has_value(&l->data), set_local_value)

#define CAST_FLOAT_VALUE fvalue = *(double *)data_ptr

gboolean sampler_layer_param_entry_set_from_ptr(const struct sampler_layer_param_entry *e, struct sampler_layer *l, gboolean set_local_value, const void *data_ptr, const uint32_t *args, GError **error)
{
    void *p = ((uint8_t *)&l->data) + e->offset;
    uint32_t cc = 0;
    double fvalue = 0;
    struct sampler_modulation_key mod_key = {0, 0, 0};
    struct sampler_noteinitfunc_key nif_key = {{NULL}, 0};
    struct sampler_flex_lfo_key flex_lfo_key = {0};

    switch(e->type)
    {
        case slpt_midi_note_t:
            if (!OVERRIDE_LOGIC(midi_note_t))
                return TRUE;
            memcpy(p, data_ptr, sizeof(midi_note_t));
            break;
        case slpt_int:
            if (!OVERRIDE_LOGIC(int))
                return TRUE;
            memcpy(p, data_ptr, sizeof(int));
            break;
        case slpt_enum:
        case slpt_uint32_t:
            if (!OVERRIDE_LOGIC(uint32_t))
                return TRUE;
            memcpy(p, data_ptr, sizeof(uint32_t));
            break;
        case slpt_string:
            {
                char **pc = p;
                gboolean str_differs = (!*pc != !data_ptr) || strcmp(*pc, data_ptr);
                if (!override_logic(!str_differs, e->get_has_value(&l->data), set_local_value))
                    return TRUE;
                if (str_differs)
                {
                    free(*pc);
                    *pc = g_strdup(data_ptr);
                    gboolean *changed_ptr = (gboolean *)(((uint8_t *)&l->data) + e->extra_int);
                    *changed_ptr = 1;
                }
            }
            break;
        case slpt_float:
        case slpt_dBamp:
            fvalue = *(double *)data_ptr;
            if (!override_logic((float)fvalue == *(float *)p, e->get_has_value(&l->data), set_local_value))
                return TRUE;
            *(float *)p = fvalue;
            break;
        case slpt_midicurve:
            CAST_FLOAT_VALUE;
            if (args[0] >= 0 && args[0] <= 127)
            {
                gboolean (*setgethasfunc)(struct sampler_layer_data *, uint32_t, int) = e->extra_ptr;
                float *dst = &((struct sampler_midi_curve *)p)->values[args[0]];
                if (!override_logic(*dst == fvalue, setgethasfunc(&l->data, args[0], -1), set_local_value))
                    return TRUE;
                *dst = fvalue;
                setgethasfunc(&l->data, args[0], set_local_value);
            }
            else
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Curve entry index (%u) is out of range for %s", (unsigned)args[0], e->name);
                return FALSE;
            }
            break;
        case slpt_ccrange:
        {
            int number = *(int *)data_ptr;
            cc = args[0];
            switch(e->extra_int) {
                case 0:
                    sampler_cc_range_set_locc_by_offset(l, e->offset, &(struct sampler_cc_range_key){cc}, set_local_value, number);
                    break;
                case 1:
                    sampler_cc_range_set_hicc_by_offset(l, e->offset, &(struct sampler_cc_range_key){cc}, set_local_value, number);
                    break;
                default: assert(0);
            }
            break;
        }
        case slpt_mod_amount:
            CAST_FLOAT_VALUE;
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_set_amount_by_offset(l, e->offset, &mod_key, set_local_value, fvalue);
            break;
        case slpt_mod_curveid:
            CAST_FLOAT_VALUE;
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_set_curve_id_by_offset(l, e->offset, &mod_key, set_local_value, (int)fvalue);
            break;
        case slpt_mod_smooth:
            CAST_FLOAT_VALUE;
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_set_smooth_by_offset(l, e->offset, &mod_key, set_local_value, fvalue);
            break;
        case slpt_mod_step:
            CAST_FLOAT_VALUE;
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_set_step_by_offset(l, e->offset, &mod_key, set_local_value, fvalue);
            break;
        case slpt_generic_modulation:
            CAST_FLOAT_VALUE;
            sampler_modulation_set_amount_by_offset(l, e->offset, &(struct sampler_modulation_key){args[0], args[1], args[2]}, set_local_value, fvalue);
            sampler_modulation_set_curve_id_by_offset(l, e->offset, &(struct sampler_modulation_key){args[0], args[1], args[2]}, set_local_value, (int)args[3]);
            break;
        case slpt_voice_nif:
        case slpt_prevoice_nif:
            CAST_FLOAT_VALUE;
            nif_key_decode(e->extra_int, e->extra_ptr, args, &nif_key);
            switch(e->extra_int & NIF_VARIANT_MASK)
            {
            case 0:
            case NIF_VARIANT_CC:
                sampler_noteinitfunc_set_value_by_offset(l, e->offset, &nif_key, set_local_value, fvalue);
                break;
            case NIF_VARIANT_CURVECC:
                sampler_noteinitfunc_set_curve_id_by_offset(l, e->offset, &nif_key, set_local_value, (int)fvalue);
                break;
            case NIF_VARIANT_STEPCC:
                sampler_noteinitfunc_set_step_by_offset(l, e->offset, &nif_key, set_local_value, fvalue);
                break;
            }
            break;
        case slpt_flex_lfo:
            CAST_FLOAT_VALUE;
            flex_lfo_key_decode(args, &flex_lfo_key);
            switch(e->extra_int)
            {
            case sampler_flex_lfo_value_field_freq:
                sampler_flex_lfo_set_freq_by_offset(l, e->offset, &flex_lfo_key, set_local_value, fvalue);
                break;
            case sampler_flex_lfo_value_field_delay:
                sampler_flex_lfo_set_delay_by_offset(l, e->offset, &flex_lfo_key, set_local_value, fvalue);
                break;
            case sampler_flex_lfo_value_field_fade:
                sampler_flex_lfo_set_fade_by_offset(l, e->offset, &flex_lfo_key, set_local_value, fvalue);
                break;
            case sampler_flex_lfo_value_field_wave:
                sampler_flex_lfo_set_wave_by_offset(l, e->offset, &flex_lfo_key, set_local_value, (int)fvalue);
                break;
            case sampler_flex_lfo_value_field_phase:
                sampler_flex_lfo_set_phase_by_offset(l, e->offset, &flex_lfo_key, set_local_value, fvalue);
                break;
            case sampler_flex_lfo_value_field_count:
                sampler_flex_lfo_set_count_by_offset(l, e->offset, &flex_lfo_key, set_local_value, (int)fvalue);
                break;
            }
            break;
        case slpt_flex_lfo_dest:
            CAST_FLOAT_VALUE;
            flex_lfo_key_decode(args, &flex_lfo_key);
            mod_key.src = SMSRC_FLEXLFO_BY_NUM(flex_lfo_key.id);
            mod_key.src2 = smsrc_none;
            mod_key.dest = e->extra_int;
            sampler_modulation_set_amount_by_offset(l, e->offset, &mod_key, set_local_value, fvalue);
            break;
        case slpt_reserved:
        case slpt_invalid:
        case slpt_alias:
        case slpt_nonfunctional:
            printf("Unhandled parameter type of parameter %s\n", e->name);
            assert(0);
            return FALSE;
    }
    if (e->set_has_value)
        e->set_has_value(&l->data, set_local_value);
    if (l->child_layers) {
        /* Propagate to children */
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, l->child_layers);
        gpointer key, value;
        while(g_hash_table_iter_next(&iter, &key, &value))
        {
            struct sampler_layer *child = value;
            if (!sampler_layer_param_entry_set_from_ptr(e, child, FALSE, data_ptr, args, error))
                return FALSE;
        }
    }
    return TRUE;
}

#define VERIFY_FLOAT_VALUE do { if (!atof_C_verify(e->name, value, &fvalue, error)) return FALSE; } while(0)

gboolean sampler_layer_param_entry_set_from_string(const struct sampler_layer_param_entry *e, struct sampler_layer *l, gboolean set_local_value, const char *value, const uint32_t *args, GError **error)
{
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
            return sampler_layer_param_entry_set_from_ptr(e, l, set_local_value, &note, args, error);
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
            return sampler_layer_param_entry_set_from_ptr(e, l, set_local_value, &number, args, error);
        }
        case slpt_enum:
        {
            gboolean (*func)(const char *, uint32_t *value);
            func = e->extra_ptr;
            uint32_t data = 0;
            if (!func(value, &data))
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a correct value for %s", value, e->name);
                return FALSE;
            }
            return sampler_layer_param_entry_set_from_ptr(e, l, set_local_value, &data, args, error);
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
            return sampler_layer_param_entry_set_from_ptr(e, l, set_local_value, &number, args, error);
        }
        case slpt_string:
            return sampler_layer_param_entry_set_from_ptr(e, l, set_local_value, value, args, error);
        case slpt_ccrange:
        {
            char *endptr;
            errno = 0;
            int number = strtol(value, &endptr, 10);
            if (errno || *endptr || endptr == value || number < 0 || number > 127)
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "'%s' is not a correct control change value for %s", value, e->name);
                return FALSE;
            }
            return sampler_layer_param_entry_set_from_ptr(e, l, set_local_value, &number, args, error);
        }
        case slpt_nonfunctional:
        {
            char *argptr = strchr(e->name, '#');
            if (argptr)
            {
                int rootlen = argptr - e->name;
                char buf[128];
                strncpy(buf, e->name, rootlen);
                sprintf(buf + rootlen, "%d", args[0]);
                sampler_layer_apply_unknown(l, buf, value);
            }
            else
                sampler_layer_apply_unknown(l, e->name, value);
            return TRUE;
        }
        case slpt_float:
        case slpt_dBamp:
        default:
            VERIFY_FLOAT_VALUE;
            return sampler_layer_param_entry_set_from_ptr(e, l, set_local_value, &fvalue, args, error);
    }
}

#define COPY_NUM_FROM_PARENT(case_value, type) \
    case case_value: \
        if (!unset_local_value && e->get_has_value(&l->data)) \
            return TRUE; \
        *(type *)p = pp ? *(type *)pp : (type)e->def_value; \
        e->set_has_value(&l->data, 0); \
        break;
gboolean sampler_layer_param_entry_unset(const struct sampler_layer_param_entry *e, struct sampler_layer *l, gboolean unset_local_value, const uint32_t *args, GError **error)
{
    void *p = ((uint8_t *)&l->data) + e->offset;
    void *pp = l->parent ? ((uint8_t *)&l->parent->data) + e->offset : NULL;
    uint32_t cc;
    struct sampler_modulation_key mod_key = {0, 0, 0};
    struct sampler_noteinitfunc_key nif_key = {{NULL}, 0};
    struct sampler_flex_lfo_key flex_lfo_key = {0};

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
                if (!unset_local_value && e->get_has_value(&l->data))
                    return TRUE;
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
                gboolean (*setgethasfunc)(struct sampler_layer_data *, uint32_t, gboolean value) = e->extra_ptr;
                if (setgethasfunc(&l->data, args[0], -1) && !unset_local_value)
                    return TRUE;
                curve->values[args[0]] = parent_curve ? parent_curve->values[args[0]] : SAMPLER_CURVE_GAP;
                setgethasfunc(&l->data, args[0], 0);
                break;
            }
            else
            {
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Curve entry index (%u) is out of range for %s", (unsigned)args[0], e->name);
                return FALSE;
            }
        case slpt_ccrange:
            cc = args[0];
            if (!sampler_cc_range_unset_by_offset(l, e->offset, &(struct sampler_cc_range_key){cc}, unset_local_value, 1 << e->extra_int))
            {
                if (!unset_local_value)
                    return TRUE;
                g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Controller number %d not used for %s", cc, e->name);
                return FALSE;
            }
            break;
        case slpt_mod_amount:
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_unset_by_offset(l, e->offset, &mod_key, unset_local_value, 1 << sampler_modulation_value_field_amount);
            break;
        case slpt_mod_curveid:
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_unset_by_offset(l, e->offset, &mod_key, unset_local_value, 1 << sampler_modulation_value_field_curve_id);
            break;
        case slpt_mod_smooth:
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_unset_by_offset(l, e->offset, &mod_key, unset_local_value, 1 << sampler_modulation_value_field_smooth);
            break;
        case slpt_mod_step:
            mod_key_decode(e->extra_int, args, &mod_key);
            sampler_modulation_unset_by_offset(l, e->offset, &mod_key, unset_local_value, 1 << sampler_modulation_value_field_step);
            break;
        case slpt_generic_modulation:
            mod_key = (struct sampler_modulation_key){args[0], args[1], args[2]};
            sampler_modulation_unset_by_offset(l, e->offset, &mod_key, unset_local_value, (1 << sampler_modulation_value_field_amount) | (1 << sampler_modulation_value_field_curve_id));
            break;
        case slpt_voice_nif:
        case slpt_prevoice_nif:
        {
            nif_key_decode(e->extra_int, e->extra_ptr, args, &nif_key);
            static const uint32_t value_fields[] = {
                sampler_noteinitfunc_value_field_value, sampler_noteinitfunc_value_field_value,
                sampler_noteinitfunc_value_field_curve_id, sampler_noteinitfunc_value_field_step,
            };
            if (!sampler_noteinitfunc_unset_by_offset(l, e->offset, &nif_key, unset_local_value, 1 << value_fields[e->extra_int >> 24]))
            {
                if (!unset_local_value)
                    return TRUE;
                if (e->extra_int & NIF_VARIANT_MASK)
                    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Controller number %d not used for %s", args[0], e->name);
                else
                    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "%s not set", e->name);
                return FALSE;
            }
            break;
        }
        case slpt_flex_lfo_dest:
            flex_lfo_key_decode(args, &flex_lfo_key);
            mod_key.src = SMSRC_FLEXLFO_BY_NUM(flex_lfo_key.id);
            mod_key.src2 = smsrc_none;
            mod_key.dest = e->extra_int;
            sampler_modulation_unset_by_offset(l, e->offset, &mod_key, unset_local_value, 1 << sampler_modulation_value_field_amount);
            break;
        case slpt_flex_lfo:
            flex_lfo_key_decode(args, &flex_lfo_key);
            switch(e->extra_int)
            {
            case sampler_flex_lfo_value_field_freq:
                sampler_flex_lfo_unset_by_offset(l, e->offset, &flex_lfo_key, unset_local_value, 1 << sampler_flex_lfo_value_field_freq);
                break;
            case sampler_flex_lfo_value_field_delay:
                sampler_flex_lfo_unset_by_offset(l, e->offset, &flex_lfo_key, unset_local_value, 1 << sampler_flex_lfo_value_field_delay);
                break;
            case sampler_flex_lfo_value_field_fade:
                sampler_flex_lfo_unset_by_offset(l, e->offset, &flex_lfo_key, unset_local_value, 1 << sampler_flex_lfo_value_field_fade);
                break;
            case sampler_flex_lfo_value_field_wave:
                sampler_flex_lfo_unset_by_offset(l, e->offset, &flex_lfo_key, unset_local_value, 1 << sampler_flex_lfo_value_field_wave);
                break;
            }
            break;
        case slpt_invalid:
        case slpt_reserved:
        case slpt_alias:
        default:
            printf("Unhandled parameter type of parameter %s\n", e->name);
            assert(0);
            return FALSE;
    }
    if (l->child_layers) {
        /* Propagate to children */
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, l->child_layers);
        gpointer key, value;
        while(g_hash_table_iter_next(&iter, &key, &value))
        {
            struct sampler_layer *child = value;
            if (!sampler_layer_param_entry_unset(e, child, FALSE, args, error))
                return FALSE;
        }
    }
    return TRUE;
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
        return sampler_layer_param_entry_set_from_string(e, l, TRUE, value, args, error);
    else
        return -1;
}

int sampler_layer_unapply_fixed_param(struct sampler_layer *l, const char *key, GError **error)
{
    uint32_t args[10];
    const struct sampler_layer_param_entry *e = sampler_layer_param_find(key, args);
    if (e)
        return sampler_layer_param_entry_unset(e, l, TRUE, args, error);
    else
        return -1;
}

static int count_ancestors(struct sampler_layer *l)
{
    int ancestors = 0;
    for (; l->parent; l = l->parent)
        ++ancestors;
    assert(ancestors < 4);
    return ancestors;
}

static gboolean sampler_layer_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct sampler_layer *layer = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        static const char *layer_types[] = { "global", "master", "group", "region" };
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!((!layer->parent_program || cbox_execute_on(fb, NULL, "/parent_program", "o", error, layer->parent_program)) &&
            (!layer->parent || cbox_execute_on(fb, NULL, "/parent", "o", error, layer->parent)) &&
            cbox_execute_on(fb, NULL, "/level", "s", error, layer_types[count_ancestors(layer)]) &&
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
    if ((!strcmp(cmd->command, "/as_list") || !strcmp(cmd->command, "/as_list_full")) && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        // Temporary, kludgy implementation
        gchar *res = sampler_layer_to_string(layer, !strcmp(cmd->command, "/as_list_full"));
        const gchar *iter = res;
        gboolean result = TRUE;
        while(iter) {
            while(isspace(*iter))
                ++iter;
            const gchar *pkey = iter;
            const gchar *eq = iter;
            while(*eq && *eq != '=')
                ++eq;
            if (*eq == '=')
            {
                gchar *key = g_strndup(pkey, eq - pkey);
                const gchar *pvalue = eq + 1;
                while(*pvalue && isspace(*pvalue))
                    ++pvalue;
                const gchar *pend = pvalue;
                while(*pend && *pend != '=')
                    ++pend;
                if (*pend == '=' )
                {
                    while(pend > pvalue && (isalnum(pend[-1]) || pend[-1] == '_'))
                        pend--;
                    iter = pend;
                }
                else
                    iter = NULL;
                while(pend > pvalue && isspace(pend[-1]))
                    pend--;
                gchar *value = g_strndup(pvalue, pend - pvalue);
                result = cbox_execute_on(fb, NULL, "/value", "ss", error, key, value);
                g_free(value);
                g_free(key);
                if (!result)
                    break;
            }
            else
                break;
        }
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
            if (layer->parent_program->auto_update_layers)
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
            if (layer->parent_program->auto_update_layers)
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

    ld->computed.eff_waveform = NULL;
    ld->computed.eff_freq = 44100;
    ld->modulations = NULL;
    ld->voice_nifs = NULL;
    ld->prevoice_nifs = NULL;

    ld->computed.eff_use_keyswitch = 0;
    if (!parent)
    {
        // Systemwide default instead?
        uint32_t mod_offset = LOFS(modulations);
        sampler_modulation_set_amount_by_offset(l, mod_offset, &(struct sampler_modulation_key){74, smsrc_none, smdest_cutoff}, TRUE, 9600);
        sampler_modulation_set_curve_id_by_offset(l, mod_offset, &(struct sampler_modulation_key){74, smsrc_none, smdest_cutoff}, TRUE, 1);
        sampler_modulation_set_amount_by_offset(l, mod_offset, &(struct sampler_modulation_key){71, smsrc_none, smdest_resonance}, TRUE, 12);
        sampler_modulation_set_curve_id_by_offset(l, mod_offset, &(struct sampler_modulation_key){71, smsrc_none, smdest_resonance}, TRUE, 1);
        sampler_modulation_set_amount_by_offset(l, mod_offset, &(struct sampler_modulation_key){smsrc_pitchlfo, 1, smdest_pitch}, TRUE, 100);
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
    dst->name = sampler_cc_range_clone(src->name, copy_hasattr);

void sampler_layer_data_clone(struct sampler_layer_data *dst, const struct sampler_layer_data *src, gboolean copy_hasattr)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_CLONE)
    dst->modulations = sampler_modulation_clone(src->modulations, copy_hasattr);
    dst->voice_nifs = sampler_noteinitfunc_clone(src->voice_nifs, copy_hasattr);
    dst->prevoice_nifs = sampler_noteinitfunc_clone(src->prevoice_nifs, copy_hasattr);
    dst->flex_lfos = sampler_flex_lfo_clone(src->flex_lfos, copy_hasattr);
    dst->computed.eff_waveform = src->computed.eff_waveform;
    if (dst->computed.eff_waveform)
        cbox_waveform_ref(dst->computed.eff_waveform);
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
    sampler_midi_curve_interpolate(&l->name, l->computed.eff_##name, START_VALUE_##name, END_VALUE_##name, IS_QUADRATIC_##name);
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

    // Handle change of sample in the parent group without override on region level
    if (parent && (l->sample_changed || parent->sample_changed))
    {
        struct cbox_waveform *oldwf = l->computed.eff_waveform;
        if (l->sample && *l->sample)
        {
            GError *error = NULL;
            l->computed.eff_waveform = cbox_wavebank_get_waveform(p->name, p->tarfile, p->sample_dir, l->sample, &error);
            if (!l->computed.eff_waveform)
            {
                g_warning("Cannot load waveform \"%s\" in sample_dir \"%s\" : \"%s\"", l->sample, p->sample_dir, error ? error->message : "unknown error");
                g_error_free(error);
            }
        }
        else
            l->computed.eff_waveform = NULL;
        if (oldwf)
            cbox_waveform_unref(oldwf);
        l->computed.eff_is_silent = !l->sample || !strcmp(l->sample, "*silence");
        l->sample_changed = FALSE;
    }

    l->computed.eff_use_keyswitch = ((l->sw_down != -1) || (l->sw_up != -1) || (l->sw_last != -1) || (l->sw_previous != -1));
    l->computed.eff_use_simple_trigger_logic =
        (l->seq_length == 1 && l->seq_position == 1) &&
        (l->trigger != stm_first && l->trigger != stm_legato) &&
        (l->lochan == 1 && l->hichan == 16) &&
        (l->lorand == 0 && l->hirand == 1) &&
        (l->lobend == -8192 && l->hibend == 8192) &&
        (l->lochanaft == 0 && l->hichanaft == 127) &&
        (l->lopolyaft == 0 && l->hipolyaft == 127) &&
        (l->lobpm == 0 && l->hibpm == NO_HI_BPM_VALUE) &&
        !l->cc && !l->computed.eff_use_keyswitch;
    l->computed.eff_use_xfcc = l->xfin_cc || l->xfout_cc;
    l->computed.eff_use_channel_mixer = l->position != 0 || l->width != 100;
    l->computed.eff_freq = (l->computed.eff_waveform && l->computed.eff_waveform->info.samplerate) ? l->computed.eff_waveform->info.samplerate : 44100;
    l->computed.eff_loop_mode = l->loop_mode;
    l->computed.eff_use_filter_mods = l->cutoff != -1 || l->cutoff2 != -1;
    if (l->loop_mode == slm_unknown)
    {
        if (l->computed.eff_waveform && l->computed.eff_waveform->has_loop)
            l->computed.eff_loop_mode = slm_loop_continuous;
        else
        if (l->computed.eff_waveform)
            l->computed.eff_loop_mode = l->loop_end == 0 ? slm_no_loop : slm_loop_continuous;
    }

    l->computed.eff_loop_start = l->loop_start;
    l->computed.eff_loop_end = l->loop_end;
    if (l->computed.eff_loop_mode == slm_one_shot || l->computed.eff_loop_mode == slm_no_loop || l->computed.eff_loop_mode == slm_one_shot_chokeable)
        l->computed.eff_loop_start = SAMPLER_NO_LOOP;
    if ((l->computed.eff_loop_mode == slm_loop_continuous || l->computed.eff_loop_mode == slm_loop_sustain) && l->computed.eff_loop_start == SAMPLER_NO_LOOP)
        l->computed.eff_loop_start = 0;
    if ((l->computed.eff_loop_mode == slm_loop_continuous || l->computed.eff_loop_mode == slm_loop_sustain) && l->computed.eff_loop_start == 0 && l->computed.eff_waveform && l->computed.eff_waveform->has_loop)
        l->computed.eff_loop_start = l->computed.eff_waveform->loop_start;
    if (l->loop_end == 0 && l->computed.eff_waveform != NULL)
        l->computed.eff_loop_end = l->computed.eff_waveform->has_loop ? l->computed.eff_waveform->loop_end : l->computed.eff_waveform->info.frames;

    if (l->off_mode == som_unknown)
        l->off_mode = l->off_by != 0 ? som_fast : som_normal;

    l->computed.eff_lokey = l->lokey >= 0 && l->lokey <= 127 ? l->lokey : (l->key >= 0 && l->key <= 127 ? l->key : 0);
    l->computed.eff_hikey = l->hikey >= 0 && l->hikey <= 127 ? l->hikey : (l->key >= 0 && l->key <= 127 ? l->key : 127);
    l->computed.eff_pitch_keycenter = (l->pitch_keycenter >= 0 && l->pitch_keycenter <= 127) ? l->pitch_keycenter 
        : ((l->key >= 0 && l->key <= 127) ? l->key : 60);
    // 'linearize' the virtual circular buffer - write 3 (or N) frames before end of the loop
    // and 3 (N) frames at the start of the loop, and play it; in rare cases this will need to be
    // repeated twice if output write pointer is close to CBOX_BLOCK_SIZE or playback rate is very low,
    // but that's OK.
    if (l->computed.eff_waveform && l->computed.eff_waveform->preloaded_frames == (size_t)l->computed.eff_waveform->info.frames)
    {
        int shift = l->computed.eff_waveform->info.channels == 2 ? 1 : 0;
        uint32_t halfscratch = MAX_INTERPOLATION_ORDER << shift;
        memcpy(&l->computed.scratch_loop[0], &l->computed.eff_waveform->data[(l->computed.eff_loop_end - MAX_INTERPOLATION_ORDER) << shift], halfscratch * sizeof(int16_t) );
        memcpy(&l->computed.scratch_end[0], &l->computed.eff_waveform->data[(l->computed.eff_loop_end - MAX_INTERPOLATION_ORDER) << shift], halfscratch * sizeof(int16_t) );
        memset(l->computed.scratch_end + halfscratch, 0, halfscratch * sizeof(int16_t));
        if (l->computed.eff_loop_start != (uint32_t)-1)
            memcpy(l->computed.scratch_loop + halfscratch, &l->computed.eff_waveform->data[l->computed.eff_loop_start << shift], halfscratch * sizeof(int16_t));
        else
            memset(l->computed.scratch_loop + halfscratch, 0, halfscratch * sizeof(int16_t));
    }
    if (l->cutoff < 20)
        l->computed.logcutoff = -1;
    else
        l->computed.logcutoff = 1200.0 * log(l->cutoff / 440.0) / log(2) + 5700.0;

    if (l->cutoff2 < 20)
        l->computed.logcutoff2 = -1;
    else
        l->computed.logcutoff2 = 1200.0 * log(l->cutoff2 / 440.0) / log(2) + 5700.0;

    l->computed.eq_bitmask = ((l->eq1.gain != 0 || l->eq1.vel2gain != 0) ? 1 : 0)
        | ((l->eq2.gain != 0 || l->eq2.vel2gain != 0) ? 2 : 0)
        | ((l->eq3.gain != 0 || l->eq3.vel2gain != 0) ? 4 : 0);
    l->computed.mod_bitmask = 0;
    for(struct sampler_modulation *mod = l->modulations; mod; mod = mod->next)
    {
        const struct sampler_modulation_key *mk = &mod->key;
        if (mk->dest >= smdest_eg_stage_start && mk->dest <= smdest_eg_stage_end)
            l->computed.mod_bitmask |= slmb_ampeg_cc << ((mk->dest >> 4) & 3);
    }

    l->computed.eff_use_prevoice = (l->delay || l->prevoice_nifs);
    l->computed.eff_num_stages = sampler_filter_num_stages(l->cutoff, l->fil_type);
    l->computed.eff_num_stages2 = sampler_filter_num_stages(l->cutoff2, l->fil2_type);

    l->computed.resonance_scaled = pow(l->resonance_linearized, 1.f / l->computed.eff_num_stages);
    l->computed.resonance2_scaled = pow(l->resonance2_linearized, 1.f / l->computed.eff_num_stages2);
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
            if (show_inherited || range->value.has_locc) \
                g_string_append_printf(outstr, " " #parname "locc%d=%d", range->key.cc_number, range->value.locc); \
            if (show_inherited || range->value.has_hicc) \
                g_string_append_printf(outstr, " " #parname "hicc%d=%d", range->key.cc_number, range->value.hicc); \
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

static void mod_cc_attrib_to_string(GString *outstr, const char *attrib, const struct sampler_modulation_key *md, double floatvalue)
{
    char floatbuf[G_ASCII_DTOSTR_BUF_SIZE];
    int floatbufsize = G_ASCII_DTOSTR_BUF_SIZE;
    g_ascii_dtostr(floatbuf, floatbufsize, floatvalue);
    
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

    for(struct sampler_noteinitfunc *nd = l->voice_nifs; nd; nd = nd->next)
    {
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
    for(struct sampler_noteinitfunc *nd = l->prevoice_nifs; nd; nd = nd->next)
    {
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
    for(struct sampler_flex_lfo *flfo = l->flex_lfos; flfo; flfo = flfo->next)
    {
        if (flfo->value.has_freq || show_inherited)
        {
            g_ascii_dtostr(floatbuf, floatbufsize, flfo->value.freq);
            g_string_append_printf(outstr, " lfo%d_freq=%s", (int)flfo->key.id, floatbuf);
        }
        if (flfo->value.has_delay || show_inherited)
        {
            g_ascii_dtostr(floatbuf, floatbufsize, flfo->value.delay);
            g_string_append_printf(outstr, " lfo%d_delay=%s", (int)flfo->key.id, floatbuf);
        }
        if (flfo->value.has_fade || show_inherited)
        {
            g_ascii_dtostr(floatbuf, floatbufsize, flfo->value.fade);
            g_string_append_printf(outstr, " lfo%d_fade=%s", (int)flfo->key.id, floatbuf);
        }
        if (flfo->value.has_wave || show_inherited)
            g_string_append_printf(outstr, " lfo%d_wave=%d", (int)flfo->key.id, flfo->value.wave);
        if (flfo->value.has_phase || show_inherited)
        {
            g_ascii_dtostr(floatbuf, floatbufsize, flfo->value.phase);
            g_string_append_printf(outstr, " lfo%d_phase=%s", (int)flfo->key.id, floatbuf);
        }
        if (flfo->value.has_count || show_inherited)
            g_string_append_printf(outstr, " lfo%d_count=%d", (int)flfo->key.id, flfo->value.count);
    }
    for(struct sampler_modulation *md = l->modulations; md; md = md->next)
    {
        const struct sampler_modulation_key *mk = &md->key;
        const struct sampler_modulation_value *mv = &md->value;
        if (mv->has_curve || show_inherited)
            mod_cc_attrib_to_string(outstr, "_curvecc", mk, mv->curve_id);
        if (mv->has_smooth || show_inherited)
            mod_cc_attrib_to_string(outstr, "_smoothcc", mk, mv->smooth);
        if (mv->has_step || show_inherited)
            mod_cc_attrib_to_string(outstr, "_stepcc", mk, mv->step);
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
                else if (mk->src < smsrc_perchan_count + sizeof(modsrc_names) / sizeof(modsrc_names[0]))
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
                else if (IS_SMSRC_FLEXLFO(mk->src)) {
                    g_string_append_printf(outstr, " lfo%02d_%s=%s", SMSRC_FLEXLFO_NUM(mk->src), moddest_names[mk->dest], floatbuf);
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

void sampler_layer_data_close(struct sampler_layer_data *l)
{
    sampler_flex_lfos_destroy(l->flex_lfos);
    sampler_cc_ranges_destroy(l->cc);
    sampler_cc_ranges_destroy(l->on_cc);
    sampler_cc_ranges_destroy(l->xfin_cc);
    sampler_cc_ranges_destroy(l->xfout_cc);
    sampler_noteinitfuncs_destroy(l->voice_nifs);
    sampler_noteinitfuncs_destroy(l->prevoice_nifs);
    sampler_modulations_destroy(l->modulations);
    if (l->computed.eff_waveform)
    {
        cbox_waveform_unref(l->computed.eff_waveform);
        l->computed.eff_waveform = NULL;
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

    sampler_layer_data_finalize(&l->data, l->parent ? &l->parent->data : NULL, l->parent_program);
    struct sampler_layer_update_cmd *lcmd = malloc(sizeof(struct sampler_layer_update_cmd));
    lcmd->module = l->module;
    lcmd->layer = l;
    lcmd->new_data = NULL;
    lcmd->old_data = NULL;

    cbox_rt_execute_cmd_async(l->module->module.rt, &rtcmd, lcmd);
}
