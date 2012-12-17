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

void sampler_layer_set_modulation1(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_moddest dest, float amount, int flags)
{
    sampler_layer_set_modulation(l, src, smsrc_none, dest, amount, flags);
}

void sampler_layer_set_modulation(struct sampler_layer *l, enum sampler_modsrc src, enum sampler_modsrc src2, enum sampler_moddest dest, float amount, int flags)
{
    GSList *p = l->modulations;
    while(p)
    {
        struct sampler_modulation *sm = p->data;
        if (sm->src == src && sm->src2 == src2 && sm->dest == dest)
        {
            sm->amount = amount;
            sm->flags = flags;
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
    l->modulations = g_slist_prepend(l->modulations, sm);
}

void sampler_layer_add_nif(struct sampler_layer *l, SamplerNoteInitFunc notefunc, int variant, float param)
{
    struct sampler_noteinitfunc *nif = malloc(sizeof(struct sampler_noteinitfunc));
    nif->notefunc = notefunc;
    nif->variant = variant;
    nif->param = param;
    l->nifs = g_slist_prepend(l->nifs, nif);
}

static void lfo_params_clear(struct sampler_lfo_params *lfop)
{
    lfop->freq = 0.f;
    lfop->delay = 0.f;
    lfop->fade = 0.f;
}

#define PROC_FIELDS_INITIALISER(type, name, def_value) \
    l->name = def_value; \
    l->has_##name = 0;
#define PROC_FIELDS_INITIALISER_dBamp(type, name, def_value) \
    l->name = def_value; \
    l->name##_linearized = -1; \
    l->has_##name = 0;
#define PROC_FIELDS_INITIALISER_dahdsr(name, parname, index) \
    cbox_dahdsr_init(&l->name); \
    DAHDSR_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name)
#define PROC_FIELDS_INITIALISER_lfo(name, parname, index) \
    lfo_params_clear(&l->name##_params);

void sampler_layer_init(struct sampler_layer *l)
{
    l->waveform = NULL;
    l->sample_data = NULL;
    
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_INITIALISER)
    
    l->sample_end = 0;
    l->freq = 44100;
    l->mode = spt_mono16;
    l->filter = sft_lp12;
    l->use_keyswitch = 0;
    l->last_key = 0;
    l->loop_mode = slm_unknown;
    l->velcurve[0] = 0;
    l->velcurve[127] = 1;
    for (int i = 1; i < 127; i++)
        l->velcurve[i] = -1;
    l->modulations = NULL;
    l->nifs = NULL;
    sampler_layer_set_modulation1(l, 74, smdest_cutoff, 9600, 2);
    sampler_layer_set_modulation1(l, 71, smdest_resonance, 12, 2);
    sampler_layer_set_modulation(l, smsrc_pitchlfo, 1, smdest_pitch, 100, 0);
}

#define PROC_RESET_HASFIELDS(type, name, def_value) \
    dst->has_##name = 0;
#define PROC_RESET_HASFIELDS_dBamp(type, name, def_value) \
    dst->has_##name = 0;
#define PROC_RESET_HASFIELDS_dahdsr(name, parname, index) {\
        struct sampler_layer *l = dst; \
        DAHDSR_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name) \
    }
#define PROC_RESET_HASFIELDS_lfo(name, parname, index)  {\
        struct sampler_layer *l = dst; \
        LFO_FIELDS(PROC_SUBSTRUCT_RESET_HAS_FIELD, name) \
    }

void sampler_layer_clone(struct sampler_layer *dst, const struct sampler_layer *src, int reset_hasfields)
{
    memcpy(dst, src, sizeof(struct sampler_layer));
    if (dst->waveform)
        cbox_waveform_ref(dst->waveform);
    dst->modulations = g_slist_copy(dst->modulations);
    for(GSList *mod = dst->modulations; mod; mod = mod->next)
    {
        gpointer dst = g_malloc(sizeof(struct sampler_modulation));
        memcpy(dst, mod->data, sizeof(struct sampler_modulation));
        mod->data = dst;
    }
    dst->nifs = g_slist_copy(dst->nifs);
    for(GSList *nif = dst->nifs; nif; nif = nif->next)
    {
        gpointer dst = g_malloc(sizeof(struct sampler_noteinitfunc));
        memcpy(dst, nif->data, sizeof(struct sampler_noteinitfunc));
        nif->data = dst;
    }
    if (reset_hasfields)
    {
        SAMPLER_FIXED_FIELDS(PROC_RESET_HASFIELDS)
    }
}

void sampler_layer_set_waveform(struct sampler_layer *l, struct cbox_waveform *waveform)
{
    l->waveform = waveform;
    l->sample_data = waveform ? waveform->data : NULL;
    l->freq = (waveform && waveform->info.samplerate) ? waveform->info.samplerate : 44100;
    l->loop_end = waveform ? waveform->info.frames : 0;
    l->sample_end = waveform ? waveform->info.frames : 0;
    l->mode = waveform && waveform->info.channels == 2 ? spt_stereo16 : spt_mono16;
}

#define PROC_FIELDS_FINALISER(type, name, def_value) 
#define PROC_FIELDS_FINALISER_dBamp(type, name, def_value) \
    l->name##_linearized = dB2gain(l->name);
#define PROC_FIELDS_FINALISER_dahdsr(name, parname, index) \
    cbox_envelope_init_dahdsr(&l->name##_shape, &l->name, m->module.srate / CBOX_BLOCK_SIZE, 100.f);
#define PROC_FIELDS_FINALISER_lfo(name, parname, index) /* no finaliser required */

void sampler_layer_finalize(struct sampler_layer *l, struct sampler_module *m)
{
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_FINALISER)

    if (l->loop_mode == slm_unknown)
        l->loop_mode = l->loop_start == -1 ? slm_no_loop : slm_loop_continuous;
    
    if (l->loop_mode == slm_one_shot || l->loop_mode == slm_no_loop)
        l->loop_start = -1;

    if ((l->loop_mode == slm_loop_continuous || l->loop_mode == slm_loop_sustain) && l->loop_start == -1)
    {
        l->loop_start = 0;
    }

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
            for (int j = start + 1; j < i; j++)
                l->velcurve[j] = sv + (ev - sv) * (j - start) * (j - start) / ((i - start) * (i - start));
        }
        else
        {
            for (int j = start + 1; j < i; j++)
                l->velcurve[j] = sv + (ev - sv) * (j - start) / (i - start);
        }
        start = i;
    }
    l->use_keyswitch = ((l->sw_down != -1) || (l->sw_up != -1) || (l->sw_last != -1) || (l->sw_previous != -1));
    l->last_key = l->sw_lokey;
}

struct layer_foreach_struct
{
    struct sampler_layer *layer;
    const char *cfg_section;
};

static void layer_foreach_func(void *user_data, const char *key)
{
    // file loading is handled in load_program
    if (!strcmp(key, "file"))
        return;
    // import is handled in sampler_load_layer_overrides
    if (!strcmp(key, "import"))
        return;
    struct layer_foreach_struct *lfs = user_data;
    const char *value = cbox_config_get_string(lfs->cfg_section, key);
    if (!sampler_layer_apply_param(lfs->layer, key, value))
        g_warning("Unknown sample layer parameter: %s in section %s", key, lfs->cfg_section);
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

void sampler_layer_load(struct sampler_layer *l, struct sampler_module *m, const char *cfg_section, struct cbox_waveform *waveform)
{
    sampler_layer_init(l);
    sampler_layer_set_waveform(l, waveform);
    sampler_layer_load_overrides(l, cfg_section);
    sampler_layer_finalize(l, m);
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
    
#define PROC_SET_ENV_FIELD(name, index) \
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

#define PROC_SET_LFO_FIELD(name, index) \
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
    return ((l->field = sfz_note_from_string(value)), (l->has_##field = 1));
#define PARSE_PARAM_int(field, strname, valuestr) \
    return ((l->field = atoi(value)), (l->has_##field = 1));
#define PARSE_PARAM_uint32_t(field, strname, valuestr) \
    return ((l->field = (uint32_t)strtoul(value, NULL, 10)), (l->has_##field = 1));
#define PARSE_PARAM_float(field, strname, valuestr) \
    return ((l->field = atof(value)), (l->has_##field = 1));

#define PROC_APPLY_PARAM(type, name, def_value) \
    if (!strcmp(key, #name)) { \
        PARSE_PARAM_##type(name, #name, value) \
    }
#define PROC_APPLY_PARAM_dBamp(type, name, def_value) \
    if (!strcmp(key, #name)) { \
        return ((l->name = atof(value)), (l->has_##name = 1)); \
    }
// LFO and envelope need special handling now
#define PROC_APPLY_PARAM_dahdsr(name, parname, index) \
    if (!strncmp(key, #parname "_", sizeof(#parname))) \
        return parse_envelope_param(l, &l->name, &l->has_##name, index, key + sizeof(#parname), value);
#define PROC_APPLY_PARAM_lfo(name, parname, index) \
    if (!strncmp(key, #parname "_", sizeof(#parname))) \
        return parse_lfo_param(l, &l->name##_params, &l->has_##name, index, key + sizeof(#parname), value);

gboolean sampler_layer_apply_param(struct sampler_layer *l, const char *key, const char *value)
{
    // XXXKF: this is seriously stupid code, this should use a hash table for
    // fixed strings, or something else that doesn't explode O(N**2) with
    // number of attributes. But premature optimization is a root of all evil.

    SAMPLER_FIXED_FIELDS(PROC_APPLY_PARAM)

    // XXXKF: to make things worse, some attributes have names different from
    // C field names, or have multiple names, or don't map 1:1 to internal model
    
    if (!strcmp(key, "lolev"))
        l->lovel = atoi(value), l->has_lovel = 1;
    else if (!strcmp(key, "hilev"))
        l->hivel = atoi(value), l->has_hivel = 1;
    else if (!strcmp(key, "loopstart"))
        l->loop_start = atoi(value), l->has_loop_start = 1;
    else if (!strcmp(key, "loopend"))
        l->loop_end = atoi(value), l->has_loop_end = 1;
    else if (!strcmp(key, "loop_mode") || !strcmp(key, "loopmode"))
    {
        if (!strcmp(value, "one_shot"))
            l->loop_mode = slm_one_shot;
        else if (!strcmp(value, "no_loop"))
            l->loop_mode = slm_no_loop;
        else if (!strcmp(value, "loop_continuous"))
            l->loop_mode = slm_loop_continuous;
        else if (!strcmp(value, "loop_sustain"))
            l->loop_mode = slm_loop_sustain;
        else
        {
            g_warning("Unhandled loop mode: %s", value);
        }
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
    else if (!strcmp(key, "fil_type"))
    {
        enum sampler_filter_type ft = sampler_filter_type_from_string(value);
        if (ft == sft_unknown)
            g_warning("Unhandled filter type: %s", value);
        else
            l->filter = ft;
    }
    else if (!strncmp(key, "delay_cc", 8))
    {
        int ccno = atoi(key + 8);
        if (ccno > 0 && ccno < 120)
            sampler_layer_add_nif(l, sampler_nif_cc2delay, ccno, atof(value));
        else
            return FALSE;
    }
    else if (!strncmp(key, "cutoff_cc", 9))
    {
        int ccno = atoi(key + 9);
        if (ccno > 0 && ccno < 120)
            sampler_layer_set_modulation1(l, ccno, smdest_cutoff, atof(value), 0);
        else
            return FALSE;
    }
    else if (!strncmp(key, "amp_velcurve_", 13))
    {
        // if not known yet, set to 0, it can always be overriden via velcurve_quadratic setting
        if (l->velcurve_quadratic == -1)
            l->velcurve_quadratic = 0;
        int point = atoi(key + 13);
        if (point >= 0 && point <= 127)
        {
            l->velcurve[point] = atof(value);
            if (l->velcurve[point] < 0)
                l->velcurve[point] = 0;
            if (l->velcurve[point] > 1)
                l->velcurve[point] = 1;
        }
        else
            return FALSE;
    }
    else
        return FALSE;
    
    return TRUE;
}

#define TYPE_PRINTF_uint32_t(name, def_value) \
    if (l->has_##name) \
        g_string_append_printf(outstr, " %s=%u", #name, (unsigned)(l->name));
#define TYPE_PRINTF_int(name, def_value) \
    if (l->has_##name) \
        g_string_append_printf(outstr, " %s=%d", #name, (int)(l->name));
#define TYPE_PRINTF_midi_note_t(name, def_value) \
    if (l->has_##name) { \
        int val = l->name; \
        if (val == -1) \
            g_string_append_printf(outstr, " %s=-1", #name); \
        else \
            g_string_append_printf(outstr, " %s=%c%s%d", #name, "ccddeffggaab"[val%12], "\000#\000#\000\000#\000#\000#\000#\000"+(val%12), (val/12-1)); \
    } else {}
#define TYPE_PRINTF_float(name, def_value) \
    if (l->has_##name) \
        g_string_append_printf(outstr, " %s=%g", #name, (float)(l->name));

#define PROC_FIELDS_TO_FILEPTR(type, name, def_value) \
    TYPE_PRINTF_##type(name, def_value)
#define PROC_FIELDS_TO_FILEPTR_dBamp(type, name, def_value) \
    if (l->has_##name) \
        g_string_append_printf(outstr, " %s=%g", #name, (float)(l->name));

#define ENV_PARAM_OUTPUT(param, index, env, envfield, envname) \
    if (l->has_##envfield.param) \
        g_string_append_printf(outstr, " " #envname "_" #param "=%g", env.param);

#define PROC_FIELDS_TO_FILEPTR_dahdsr(name, parname, index) \
    DAHDSR_FIELDS(ENV_PARAM_OUTPUT, l->name, name, parname)
#define PROC_FIELDS_TO_FILEPTR_lfo(name, parname, index) \
    LFO_FIELDS(ENV_PARAM_OUTPUT, l->name##_params, name, parname)

gchar *sampler_layer_to_string(struct sampler_layer *l)
{
    GString *outstr = g_string_sized_new(200);
    if (l->waveform && l->waveform->display_name)
        g_string_append_printf(outstr, " sample=%s", l->waveform->display_name);
    SAMPLER_FIXED_FIELDS(PROC_FIELDS_TO_FILEPTR)
    
    static const char *addrandom_variants[] = { "amp", "fil", "pitch" };
    static const char *modsrc_names[] = { "chanaft", "vel", "polyaft", "pitch", "pitcheg", "fileg", "ampeg", "pitchlfo", "fillfo", "amplfo", "" };
    static const char *moddest_names[] = { "gain", "pitch", "cutoff", "resonance" };
    for(GSList *nif = l->nifs; nif; nif = nif->next)
    {
        struct sampler_noteinitfunc *nd = nif->data;
        #define PROC_ENVSTAGE_NAME(name, index) #name, 
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
    
    gchar *res = outstr->str;
    g_string_free(outstr, FALSE);
    return res;
}

void sampler_layer_dump(struct sampler_layer *l, FILE *f)
{
    gchar *str = sampler_layer_to_string(l);
    fprintf(f, "%s\n", str);
}