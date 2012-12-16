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
    const char *sample_path;
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
    sampler_layer_finalize(l, ls->m);
    ls->layers = g_list_append(ls->layers, ls->region);
    ls->region = NULL;
}

static void load_sfz_group(struct sfz_parser_client *client)
{
    struct sfz_load_state *ls = client->user_data;
    if (ls->group.waveform)
    {
        cbox_waveform_unref(ls->group.waveform);
        ls->group.waveform = NULL;
    }
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
        sampler_layer_clone(ls->region, &ls->group);
    else
        sampler_layer_init(ls->region);
    // g_warning("-- start region");
}

static gboolean parse_envelope_param(struct sampler_layer *layer, int env_type, const char *key, const char *value)
{
    struct cbox_dahdsr *env = NULL;
    enum sampler_modsrc src;
    enum sampler_moddest dest;
    switch(env_type)
    {
        case 0:
            env = &layer->amp_env;
            src = smsrc_ampenv;
            dest = smdest_gain;
            break;
        case 1:
            env = &layer->filter_env;
            src = smsrc_filenv;
            dest = smdest_cutoff;
            break;
        case 2:
            env = &layer->pitch_env;
            src = smsrc_pitchenv;
            dest = smdest_pitch;
            break;
    }
    float fvalue = atof(value);
    if (!strcmp(key, "start"))
        env->start = fvalue;
    else if (!strcmp(key, "delay"))
        env->delay = fvalue;
    else if (!strcmp(key, "attack"))
        env->attack = fvalue;
    else if (!strcmp(key, "hold"))
        env->hold = fvalue;
    else if (!strcmp(key, "decay"))
        env->decay = fvalue;
    else if (!strcmp(key, "sustain"))
        env->sustain = fvalue / 100.0;
    else if (!strcmp(key, "release"))
        env->release = fvalue;
    else if (!strcmp(key, "depth"))
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
        sampler_layer_set_modulation(layer, smsrc_vel, src, dest, atof(value), 0);
    else
        return FALSE;
    return TRUE;
}

static gboolean parse_lfo_param(struct sampler_layer *layer, int lfo_type, const char *key, const char *value)
{
    struct sampler_lfo_params *params = NULL;
    enum sampler_modsrc src;
    enum sampler_moddest dest;
    switch(lfo_type)
    {
        case 0:
            params = &layer->amp_lfo_params;
            src = smsrc_amplfo;
            dest = smdest_gain;
            break;
        case 1:
            params = &layer->filter_lfo_params;
            src = smsrc_fillfo;
            dest = smdest_cutoff;
            break;
        case 2:
            params = &layer->pitch_lfo_params;
            src = smsrc_pitchlfo;
            dest = smdest_pitch;
            break;
    }
    float fvalue = atof(value);
    if (!strcmp(key, "depth"))
        sampler_layer_set_modulation1(layer, src, dest, fvalue, 0);
    else if (!strcmp(key, "depthchanaft"))
        sampler_layer_set_modulation(layer, src, smsrc_chanaft, dest, fvalue, 0);
    else if (!strcmp(key, "depthpolyaft"))
        sampler_layer_set_modulation(layer, src, smsrc_polyaft, dest, fvalue, 0);
    else if (!strcmp(key, "freq"))
        params->freq = atof(value);
    else if (!strcmp(key, "delay"))
        params->delay = atof(value);
    else if (!strcmp(key, "fade"))
        params->fade = atof(value);
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

#define SFZ_NOTE_ATTRIB(name) \
    if (!strcmp(key, #name)) \
        l->name = sfz_note_from_string(value);

static gboolean load_sfz_key_value(struct sfz_parser_client *client, const char *key, const char *value)
{
    struct sfz_load_state *ls = client->user_data;
    struct sampler_layer *l = ls->region ? ls->region : &ls->group;
    int unhandled = 0;
    if (!ls->region && !ls->in_group)
    {
        g_warning("Cannot use parameter '%s' outside of region or group", key);
        return TRUE;
    }
    
    if (!strcmp(key, "sample"))
    {
        if (l->waveform != NULL)
        {
            cbox_waveform_unref(l->waveform);
            l->waveform = NULL;
        }
        gchar *value_copy = g_strdup(value);
        for (int i = 0; value_copy[i]; i++)
        {
            if (value_copy[i] == '\\')
                value_copy[i] = '/';
        }
        gchar *filename = g_build_filename(ls->sample_path ? ls->sample_path : "", value_copy, NULL);
        g_free(value_copy);
        struct cbox_waveform *wf = cbox_wavebank_get_waveform(ls->filename, filename, ls->error);
        g_free(filename);
        if (!wf)
            return FALSE;
        sampler_layer_set_waveform(l, wf);
        if (l->loop_end == 0)
            l->loop_end = l->sample_end;
    }
    else if (!strcmp(key, "lokey"))
        l->min_note = sfz_note_from_string(value);
    else if (!strcmp(key, "hikey"))
        l->max_note = sfz_note_from_string(value);
    else if (!strcmp(key, "pitch_keycenter"))
        l->pitch_keycenter = sfz_note_from_string(value);
    else if (!strcmp(key, "pitch_keytrack"))
        l->pitch_keytrack = atoi(value);
    else if (!strcmp(key, "key"))
        l->min_note = l->max_note = l->pitch_keycenter = sfz_note_from_string(value);
    else if (!strcmp(key, "seq_position"))
        l->seq_pos = atoi(value) - 1;
    else if (!strcmp(key, "seq_length"))
        l->seq_length = atoi(value);
    else SFZ_NOTE_ATTRIB(sw_lokey)
    else SFZ_NOTE_ATTRIB(sw_hikey)
    else SFZ_NOTE_ATTRIB(sw_down)
    else SFZ_NOTE_ATTRIB(sw_up)
    else SFZ_NOTE_ATTRIB(sw_last)
    else SFZ_NOTE_ATTRIB(sw_previous)
    else if (!strcmp(key, "lovel") || !strcmp(key, "lolev"))
        l->min_vel = atoi(value);
    else if (!strcmp(key, "hivel") || !strcmp(key, "hilev"))
        l->max_vel = atoi(value);
    else if (!strcmp(key, "lochan"))
        l->min_chan = atoi(value);
    else if (!strcmp(key, "hichan"))
        l->max_chan = atoi(value);
    else if (!strcmp(key, "offset"))
        l->sample_offset = atoi(value);
    else if (!strcmp(key, "offset_random"))
        l->sample_offset_random = atoi(value);
    else if (!strcmp(key, "delay"))
        l->delay = atof(value);
    else if (!strcmp(key, "delay_random"))
        l->delay_random = atof(value);
    else if (!strcmp(key, "loop_start") || !strcmp(key, "loopstart"))
        l->loop_start = atoi(value);
    else if (!strcmp(key, "loop_end") || !strcmp(key, "loopend"))
        l->loop_end = atoi(value);
    else if (!strcmp(key, "loop_evolve") || !strcmp(key, "loopevolve"))
        l->loop_evolve = atoi(value);
    else if (!strcmp(key, "loop_overlap") || !strcmp(key, "loopoverlap"))
        l->loop_overlap = atoi(value);
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
    else if (!strcmp(key, "volume"))
    {
        l->volume = atof(value);
        l->volume_linearized = -1;
    }
    else if (!strcmp(key, "pan"))
        l->pan = atof(value);
    else if (!strcmp(key, "cutoff"))
        l->cutoff = atof(value);
    else if (!strcmp(key, "cutoff_chanaft"))
        sampler_layer_set_modulation1(l, smsrc_chanaft, smdest_cutoff, atof(value), 0);
    else if (!strcmp(key, "resonance"))
        l->resonance = dB2gain(atof(value));
    else if (!strcmp(key, "tune"))
        l->tune = atof(value);
    else if (!strcmp(key, "transpose"))
        l->transpose = atoi(value);
    else if (!strcmp(key, "group"))
        l->exclusive_group = atoi(value);
    else if (!strcmp(key, "off_by"))
        l->off_by = atoi(value);
    else if (!strcmp(key, "output"))
        l->output = atoi(value);
    else if (!strcmp(key, "velcurve_quadratic"))
        l->velcurve_quadratic = atoi(value);
    else if (!strcmp(key, "fil_veltrack"))
        l->fil_veltrack = atof(value);
    else if (!strcmp(key, "fil_keytrack"))
        l->fil_keytrack = atof(value);
    else if (!strcmp(key, "fil_keycenter"))
        l->fil_keycenter = atof(value);
    else if (!strcmp(key, "amp_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 0, atof(value));
    else if (!strcmp(key, "fil_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 1, atof(value));
    else if (!strcmp(key, "pitch_random"))
        sampler_layer_add_nif(l, sampler_nif_addrandom, 2, atof(value));
    else if (!strcmp(key, "pitch_veltrack"))
        sampler_layer_add_nif(l, sampler_nif_vel2pitch, 0, atof(value));
    else if (!strcmp(key, "effect1"))
        l->send1gain = atof(value) / 100.0;
    else if (!strcmp(key, "effect2"))
        l->send2gain = atof(value) / 100.0;
    else if (!strcmp(key, "effect1bus"))
        l->send1bus = atoi(value);
    else if (!strcmp(key, "effect2bus"))
        l->send2bus = atoi(value);
    else if (!strncmp(key, "amplfo_", 7))
        unhandled = (!parse_lfo_param(l, 0, key + 7, value));
    else if (!strncmp(key, "fillfo_", 7))
        unhandled = (!parse_lfo_param(l, 1, key + 7, value));
    else if (!strncmp(key, "pitchlfo_", 9))
        unhandled = (!parse_lfo_param(l, 2, key + 9, value));
    else if (!strcmp(key, "fil_type"))
    {
        enum sampler_filter_type ft = sampler_filter_type_from_string(value);
        if (ft == sft_unknown)
            g_warning("Unhandled filter type: %s", value);
        else
            l->filter = ft;
    }
    else if (!strncmp(key, "ampeg_", 6))
        unhandled = (!parse_envelope_param(l, 0, key + 6, value));
    else if (!strncmp(key, "fileg_", 6))
        unhandled = (!parse_envelope_param(l, 1, key + 6, value));
    else if (!strncmp(key, "pitcheg_", 8))
        unhandled = (!parse_envelope_param(l, 2, key + 8, value));
    else if (!strncmp(key, "delay_cc", 8))
    {
        int ccno = atoi(key + 8);
        if (ccno > 0 && ccno < 120)
            sampler_layer_add_nif(l, sampler_nif_cc2delay, ccno, atof(value));
        else
            unhandled = 1;
    }
    else if (!strncmp(key, "cutoff_cc", 9))
    {
        int ccno = atoi(key + 9);
        if (ccno > 0 && ccno < 120)
            sampler_layer_set_modulation1(l, ccno, smdest_cutoff, atof(value), 0);
        else
            unhandled = 1;
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
            unhandled = 1;
    }
    else
        unhandled = 1;
    
    if (unhandled)
        g_warning("Unhandled sfz key: %s", key);
    return TRUE;
}

gboolean sampler_module_load_program_sfz(struct sampler_module *m, struct sampler_program *prg, const char *sfz, const char *sample_path, int is_from_string, GError **error)
{
    struct sfz_load_state ls = { .in_group = 0, .m = m, .filename = sfz, .layers = NULL, .region = NULL, .error = error, .sample_path = sample_path };
    struct sfz_parser_client c = { .user_data = &ls, .region = load_sfz_region, .group = load_sfz_group, .key_value = load_sfz_key_value };
    g_clear_error(error);

    gboolean status = is_from_string ? load_sfz_from_string(sfz, strlen(sfz), &c, error) : load_sfz(sfz, &c, error);
    if (!status)
        return FALSE;

    if (ls.region)
        load_sfz_end_region(&c);
    if (ls.group.waveform)
    {
        cbox_waveform_unref(ls.group.waveform);
        ls.group.waveform = NULL;
    }
    
    prg->layer_count = g_list_length(ls.layers);
    prg->layers = malloc(prg->layer_count * sizeof(struct sampler_layer *));
    GList *p = ls.layers;
    for(int i = 0; p; i++)
    {
        prg->layers[i] = p->data;
        p = g_list_next(p);
    }
    g_list_free(ls.layers);
    return TRUE;
}

