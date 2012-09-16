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

#include "blob.h"
#include "config-api.h"
#include "pattern.h"
#include "pattern-maker.h"

#include <glib.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_midi_pattern)

struct cbox_midi_pattern *cbox_midi_pattern_new_metronome(struct cbox_document *doc, int ts)
{
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new(doc);
    
    int length = PPQN;
    int channel = cbox_config_get_int("metronome", "channel", 10);
    int accnote = cbox_config_get_note("metronome", "note_accent", 37);
    int note = cbox_config_get_note("metronome", "note", 37);
    
    for (int i = 0; i < ts; i++)
    {
        int e = 2 * i;
        int accent = !i && ts != 1;
        cbox_midi_pattern_maker_add(m, length * i, 0x90 + channel - 1, accent ? accnote : note, accent ? 127 : 100);
        cbox_midi_pattern_maker_add(m, length * i + 1, 0x80 + channel - 1, accent ? accnote : note, 0);
    }

    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m, doc, g_strdup_printf("click-%d", ts));
    p->loop_end = length * ts;

    cbox_midi_pattern_maker_destroy(m);

    return p;
}

void cbox_midi_pattern_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_midi_pattern *pattern = (struct cbox_midi_pattern *)objhdr;
    g_free(pattern->name);
    if (pattern->events != NULL)
        free(pattern->events);
    free(pattern);
}

static int cbox_midi_pattern_load_smf_into(struct cbox_midi_pattern_maker *m, const char *smf)
{
    int length = 0;
    if (!cbox_midi_pattern_maker_load_smf(m, smf, &length, NULL))
    {
        g_error("Cannot load SMF file %s", smf);
        return -1;
    }
    return length;
}

static int cbox_midi_pattern_load_melodic_into(struct cbox_midi_pattern_maker *m, const char *name, int start_pos, int transpose, int transpose_to_note)
{
    gchar *cfg_section = g_strdup_printf("pattern:%s", name);
    
    if (!cbox_config_has_section(cfg_section))
    {
        g_error("Melodic pattern '%s' not found", name);
        g_free(cfg_section);
        return -1;
    }

    gchar *smf = cbox_config_get_string(cfg_section, "smf");
    if (smf)
        return cbox_midi_pattern_load_smf_into(m, smf);

    int length = PPQN * cbox_config_get_int(cfg_section, "beats", 4);
    int gchannel = cbox_config_get_int(cfg_section, "channel", 1);
    int gswing = cbox_config_get_int(cfg_section, "swing", 0);
    int gres = cbox_config_get_int(cfg_section, "resolution", 4);
    int orignote = cbox_config_get_note(cfg_section, "base_note", 24);
    if (transpose_to_note != -1)
        transpose += transpose_to_note - orignote;
    
    for (int t = 1; ; t++)
    {
        gchar *tname = g_strdup_printf("track%d", t);
        char *trkname = cbox_config_get_string(cfg_section, tname);
        g_free(tname);
        if (trkname)
        {
            tname = g_strdup_printf("%s_vel", trkname);
            int vel = cbox_config_get_note(cfg_section, tname, 100);
            g_free(tname);
            tname = g_strdup_printf("%s_res", trkname);
            int res = cbox_config_get_note(cfg_section, tname, gres);
            g_free(tname);
            tname = g_strdup_printf("%s_channel", trkname);
            int channel = cbox_config_get_note(cfg_section, tname, gchannel);
            g_free(tname);
            tname = g_strdup_printf("%s_swing", trkname);
            int swing = cbox_config_get_int(cfg_section, tname, gswing);
            g_free(tname);
            tname = g_strdup_printf("%s_notes", trkname);
            const char *notes = cbox_config_get_string(cfg_section, tname);
            g_free(tname);
            if (!notes)
            {
                g_error("Invalid track %s", trkname);
            }
            const char *s = notes;
            int i = 0, t = 0;
            while(1)
            {
                if (!*s)
                    break;
                
                gchar *note;
                const char *comma = strchr(s, ',');
                if (comma)
                {
                    note = g_strndup(s, comma - s);
                    s = comma + 1;
                }
                else
                {
                    note = g_strdup(s);
                    s += strlen(s);
                }
                
                if (*note)
                {
                    int pitch = note_from_string(note);
                    
                    int pos = t * PPQN / res + start_pos;
                    if (t & 1)
                        pos += PPQN * swing / (res * 24);
                    
                    int pos2 = (t + 1) * PPQN / res + start_pos;
                    if (t & 1)
                        pos2 += PPQN * swing / (res * 24);
                    
                    pitch += transpose;

                    cbox_midi_pattern_maker_add(m, pos, 0x90 + channel - 1, pitch, vel);
                    cbox_midi_pattern_maker_add(m, pos2 - 1, 0x80 + channel - 1, pitch, 0);                
                }
                t++;
            }
        }
        else
            break;
    }
    
    g_free(cfg_section);
    
    return length;
}

static int cbox_midi_pattern_load_drum_into(struct cbox_midi_pattern_maker *m, const char *name, int start_pos)
{
    gchar *cfg_section = g_strdup_printf("drumpattern:%s", name);
    
    if (!cbox_config_has_section(cfg_section))
    {
        g_error("Drum pattern '%s' not found", name);
        g_free(cfg_section);
        return -1;
    }
    
    gchar *smf = cbox_config_get_string(cfg_section, "smf");
    if (smf)
        return cbox_midi_pattern_load_smf_into(m, smf);

    int length = PPQN * cbox_config_get_int(cfg_section, "beats", 4);
    int channel = cbox_config_get_int(cfg_section, "channel", 10);
    int gswing = cbox_config_get_int(cfg_section, "swing", 0);
    int gres = cbox_config_get_int(cfg_section, "resolution", 4);
    
    for (int t = 1; ; t++)
    {
        gchar *tname = g_strdup_printf("track%d", t);
        char *trkname = cbox_config_get_string(cfg_section, tname);
        g_free(tname);
        if (trkname)
        {
            tname = g_strdup_printf("%s_note", trkname);
            int note = cbox_config_get_note(cfg_section, tname, -1);
            g_free(tname);
            tname = g_strdup_printf("%s_res", trkname);
            int res = cbox_config_get_note(cfg_section, tname, gres);
            g_free(tname);
            tname = g_strdup_printf("%s_swing", trkname);
            int swing = cbox_config_get_int(cfg_section, tname, gswing);
            g_free(tname);
            tname = g_strdup_printf("%s_trigger", trkname);
            const char *trigger = cbox_config_get_string(cfg_section, tname);
            g_free(tname);
            if (!trigger || note == -1)
            {
                g_error("Invalid track %s", trkname);
            }
            int t = 0;
            for (int i = 0; trigger[i]; i++)
            {
                int pos = t * PPQN / res + start_pos;
                if (t & 1)
                    pos += PPQN * swing / (res * 24);
                if (trigger[i] >= '1' && trigger[i] <= '9')
                {
                    int amt = (trigger[i] - '0') * 127 / 9;
                    cbox_midi_pattern_maker_add(m, pos, 0x90 + channel - 1, note, amt);
                    cbox_midi_pattern_maker_add(m, pos + 1, 0x80 + channel - 1, note, 0);
                    t++;
                }
                if (trigger[i] == 'F') // flam
                {
                    int dflam = PPQN / 4;
                    int rnd = rand() & 7;
                    dflam += rnd / 2;
                    cbox_midi_pattern_maker_add(m, pos - dflam, 0x90 + channel - 1, note, 90+rnd);
                    cbox_midi_pattern_maker_add(m, pos - dflam + 1, 0x80 + channel - 1, note, 0);
                    cbox_midi_pattern_maker_add(m, pos , 0x90 + channel - 1, note, 120 + rnd);
                    cbox_midi_pattern_maker_add(m, pos + 1, 0x80 + channel - 1, note, 0);
                    t++;
                }
                if (trigger[i] == 'D') // drag
                {
                    pos = (t + 1) * PPQN / res + start_pos;
                    //if (!(t & 1))
                    //    pos += PPQN * swing / (res * 24);
                    float dflam = PPQN/8.0;
                    int rnd = rand() & 7;
                    cbox_midi_pattern_maker_add(m, pos - dflam*2, 0x90 + channel - 1, note, 70+rnd);
                    cbox_midi_pattern_maker_add(m, pos - dflam*2 + 1, 0x80 + channel - 1, note, 0);
                    cbox_midi_pattern_maker_add(m, pos - dflam, 0x90 + channel - 1, note, 60+rnd);
                    cbox_midi_pattern_maker_add(m, pos - dflam + 1, 0x80 + channel - 1, note, 0);
                    t++;
                }
                else if (trigger[i] == '.')
                    t++;
            }
        }
        else
            break;
    }
    
    g_free(cfg_section);
    
    return length;
}

struct cbox_midi_pattern *cbox_midi_pattern_load(struct cbox_document *doc, const char *name, int is_drum)
{
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new(doc);
    
    int length = 0;
    if (is_drum)
        length = cbox_midi_pattern_load_drum_into(m, name, 0);
    else
        length = cbox_midi_pattern_load_melodic_into(m, name, 0, 0, -1);
    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m, doc, g_strdup(name));
    p->loop_end = length;
    
    cbox_midi_pattern_maker_destroy(m);
    
    return p;
}

struct cbox_midi_pattern *cbox_midi_pattern_load_track(struct cbox_document *doc, const char *name, int is_drum)
{
    int length = 0;
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new(doc);
    
    gchar *cfg_section = g_strdup_printf(is_drum ? "drumtrack:%s" : "track:%s", name);
    
    if (!cbox_config_has_section(cfg_section))
    {
        g_error("Drum track '%s' not found", name);
        g_free(cfg_section);
        return NULL;
    }
    
    for (int p = 1; ; p++)
    {
        gchar *pname = g_strdup_printf("pos%d", p);
        char *patname = cbox_config_get_string(cfg_section, pname);
        g_free(pname);
        if (patname)
        {
            int tplen = 0;
            char *comma = strchr(patname, ',');
            while(*patname)
            {
                char *v = comma ? g_strndup(patname, comma - patname) : g_strdup(patname);
                patname = comma ? comma + 1 : patname + strlen(patname);
                
                int xpval = 0, xpnote = -1;
                if (!is_drum)
                {
                    char *xp = strchr(v, '+');
                    if (xp)
                    {
                        *xp = '\0';
                        xpval = atoi(xp + 1);
                    }
                    else
                    {
                        xp = strchr(v, '=');
                        if (xp)
                        {
                            *xp = '\0';
                            xpnote = note_from_string(xp + 1);
                        }
                    }
                }
                int plen = 0;
                int is_drum_pat = is_drum;
                int nofs = 0;
                if (*v == '@')
                {
                    nofs = 1;
                    is_drum_pat = !is_drum_pat;
                }
                if (is_drum_pat)
                    plen = cbox_midi_pattern_load_drum_into(m, v + nofs, length); 
                else
                    plen = cbox_midi_pattern_load_melodic_into(m, v + nofs, length, xpval, xpnote); 
                g_free(v);
                if (plen < 0)
                {
                    cbox_midi_pattern_maker_destroy(m);
                    return NULL;
                }
                if (plen > tplen)
                    tplen = plen;
                if (*patname)
                    comma = strchr(patname, ',');
            }
            length += tplen;
        }
        else
            break;
    }
    
    g_free(cfg_section);
            
    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m, doc, g_strdup(name));
    p->loop_end = length;
    
    cbox_midi_pattern_maker_destroy(m);
    
    return p;
}

struct cbox_midi_pattern *cbox_midi_pattern_new_from_blob(struct cbox_document *doc, const struct cbox_blob *blob, int length)
{
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new(doc);
    
    struct cbox_blob_serialized_event event;
    for (size_t i = 0; i < blob->size; i += sizeof(event))
    {
        // not sure about alignment guarantees of Python buffers
        memcpy(&event, ((uint8_t *)blob->data) + i, sizeof(event));
        cbox_midi_pattern_maker_add(m, event.time, event.cmd, event.byte1, event.byte2);
    }
    
    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m, doc, g_strdup("unnamed-blob"));
    p->loop_end = length;
    
    cbox_midi_pattern_maker_destroy(m);
    
    return p;
}

struct cbox_blob *cbox_midi_pattern_to_blob(struct cbox_midi_pattern *pat, int *length)
{
    if (length)
        *length = pat->loop_end;
    
    struct cbox_blob_serialized_event event;
    int size = 0;
    for (int i = 0; i < pat->event_count; i++)
    {
        // currently sysex events and the like are not supported
        if (pat->events[i].size < 4)
            size += sizeof(event);
    }
    
    struct cbox_blob *blob = cbox_blob_new(size);
    
    size = 0;
    uint8_t *data = blob->data;
    for (int i = 0; i < pat->event_count; i++)
    {
        // currently sysex events and the like are not supported
        const struct cbox_midi_event *src = &pat->events[i];
        if (src->size < 4)
        {
            event.time = src->time;
            event.len = src->size;
            memcpy(&event.cmd, &src->data_inline[0], event.len);
            memcpy(data + size, &event, sizeof(event));
            size += sizeof(event);
        }
    }
    return blob;
}

gboolean cbox_midi_pattern_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        struct cbox_midi_pattern *p = ct->user_data;
        
        return cbox_execute_on(fb, NULL, "/event_count", "i", error, (int)p->event_count) &&
            cbox_execute_on(fb, NULL, "/loop_end", "i", error, (int)p->loop_end) &&
            cbox_execute_on(fb, NULL, "/name", "s", error, p->name) &&
            CBOX_OBJECT_DEFAULT_STATUS(p, fb, error)
            ;
    }
    return cbox_object_default_process_cmd(ct, fb, cmd, error);
}
