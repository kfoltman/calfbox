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
#include "pattern.h"
#include "pattern-maker.h"

#include <glib.h>

int ppqn_to_samples(struct cbox_master *master, int time)
{
    return (int)(master->srate * 60.0 * time / (master->tempo * PPQN));
}

void cbox_read_pattern(struct cbox_midi_pattern_playback *pb, struct cbox_midi_buffer *buf, int nsamples)
{
    cbox_midi_buffer_clear(buf);
    int loop_end = ppqn_to_samples(pb->master, pb->pattern->loop_end);
    while(1)
    {
        if (pb->pos >= pb->pattern->event_count)
        {
            if (loop_end == -1)
                break;
            if (loop_end >= pb->time + nsamples)
                break;
            pb->pos = 0;
            pb->time -= loop_end; // may be negative, but that's OK
        }
        const struct cbox_midi_event *src = &pb->pattern->events[pb->pos];
        int srctime = ppqn_to_samples(pb->master, src->time);
        if (srctime >= pb->time + nsamples)
            break;
        int32_t time = 0;
        if (srctime >= pb->time) // convert negative relative time to 0 time
            time = srctime - pb->time;
        
        cbox_midi_buffer_copy_event(buf, src, time);
        pb->pos++;
    }
    pb->time += nsamples;
}

struct cbox_midi_pattern *cbox_midi_pattern_new_metronome(int ts)
{
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new();
    
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
    
    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m);
    
    p->loop_end = length * ts;
    
    cbox_midi_pattern_maker_destroy(m);

    return p;
}

void cbox_midi_pattern_playback_seek(struct cbox_midi_pattern_playback *pb, int time_ppqn)
{
    int pos = 0;
    while (pos < pb->pattern->event_count && time_ppqn > pb->pattern->events[pos].time)
        pos++;
    pb->time = ppqn_to_samples(pb->master, time_ppqn);
    pb->pos = pos;
}

void cbox_midi_pattern_destroy(struct cbox_midi_pattern *pattern)
{
    if (pattern->event_count)
        free(pattern->events);
    free(pattern);
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
                    int amt = 110;
                    int dflam = 4;
                    cbox_midi_pattern_maker_add(m, pos - dflam, 0x90 + channel - 1, note, 60);
                    cbox_midi_pattern_maker_add(m, pos - dflam + 1, 0x80 + channel - 1, note, 0);
                    cbox_midi_pattern_maker_add(m, pos , 0x90 + channel - 1, note, 127);
                    cbox_midi_pattern_maker_add(m, pos + 1, 0x80 + channel - 1, note, 0);
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

struct cbox_midi_pattern *cbox_midi_pattern_load_drum(const char *name)
{
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new();
    
    int length = cbox_midi_pattern_load_drum_into(m, name, 0);
    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m);    
    p->loop_end = length;
    
    cbox_midi_pattern_maker_destroy(m);
    
    return p;
}

struct cbox_midi_pattern *cbox_midi_pattern_load_drum_track(const char *name)
{
    int length = 0;
    struct cbox_midi_pattern_maker *m = cbox_midi_pattern_maker_new();
    
    gchar *cfg_section = g_strdup_printf("drumtrack:%s", name);
    
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
            while(comma)
            {
                char *v = g_strndup(patname, comma - patname);
                patname = comma + 1;
                comma = strchr(patname, ',');
                int plen = cbox_midi_pattern_load_drum_into(m, v, length); 
                g_free(v);
                if (plen < 0)
                {
                    cbox_midi_pattern_maker_destroy(m);
                    return NULL;
                }
                if (plen > tplen)
                    tplen = plen;
            }
            int plen = cbox_midi_pattern_load_drum_into(m, patname, length); 
            if (plen < 0)
            {
                cbox_midi_pattern_maker_destroy(m);
                return NULL;
            }
            if (plen > tplen)
                tplen = plen;
            length += tplen;
        }
        else
            break;
    }
    
    g_free(cfg_section);
            
    struct cbox_midi_pattern *p = cbox_midi_pattern_maker_create_pattern(m);        
    p->loop_end = length;
    
    cbox_midi_pattern_maker_destroy(m);
    
    return p;
}
