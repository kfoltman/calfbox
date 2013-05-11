/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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

#include "config.h"

#if USE_JACK

#include "config-api.h"
#include "io.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static char *cfg(const char *section, const char *name, char *defvalue)
{
    char *value = cbox_config_get_string(section, name);
    if (value)
        return value;
    return cbox_config_get_string_with_default("autojack", name, defvalue);
}

static void generate_jack_config(const char *section, const char *id)
{
    char *rcfile = cbox_config_get_string("autojack", "jackdrc");
    FILE *f;
    if (!rcfile)
    {
        rcfile = g_strdup_printf("%s/.jackdrc", getenv("HOME"));
        g_message("Generating JACK config: %s\n", rcfile);
        f = fopen(rcfile, "w");
        if (!f)
        {
            g_error("Cannot open file %s", rcfile);
            return;
        }
        g_free(rcfile);
    }
    else
    {
        g_message("Generating JACK config: %s\n", rcfile);
        f = fopen(rcfile, "w");
        if (!f)
        {
            g_error("Cannot open file %s", rcfile);
            return;
        }
    }
    
    fprintf(f, "%s %s -d alsa -d hw:%s -r 44100 %s\n", 
        cfg(section, "jackd", "/usr/bin/jackd"),
        cfg(section, "jack_options", "-R -T"),
        id,
        cfg(section, "alsa_options", ""));
    fclose(f);
}

static int try_soundcard(const char *name)
{
    gchar *id;
    if (!cbox_config_has_section(name))
        return 0;
    
    g_message("Trying section %s", name);
    
    id = cbox_config_get_string(name, "device");
    if (id != NULL)
    {
        struct stat s;
        int result;
        gchar *fn = g_strdup_printf("/proc/asound/%s", id);
        result = stat(fn, &s);
        if (!result)
            generate_jack_config(name, id);
        g_free(fn);
        return !result;
    }
    
    id = cbox_config_get_string(name, "usbid");
    if (id != NULL)
    {
        int vid, pid;
        if (sscanf(id, "%x:%x\n", &vid, &pid) !=2)
        {
            g_error("Invalid VID:PID value: %s", id);
            return 0;
        }
        for (int i = 0; ; i++)
        {
            struct stat s;
            int result;
            FILE *f = NULL;
            int tvid, tpid;
            
            // check if it's not beyond the last soundcard index
            gchar *fn = g_strdup_printf("/proc/asound/card%d", i);
            result = stat(fn, &s);
            g_free(fn);
            if (result)
                break;
            
            // check if it has a USB ID
            fn = g_strdup_printf("/proc/asound/card%d/usbid", i);
            f = fopen(fn, "r");
            g_free(fn);
            
            if (!f)
                continue;
            
            if (fscanf(f, "%x:%x", &tvid, &tpid) == 2)
            {
                if (vid == tvid && pid == tpid)
                {
                    gchar *fn = g_strdup_printf("%d", i);
                    generate_jack_config(name, fn);
                    g_free(fn);
                    fclose(f);
                    return 1;
                }
            }
            fclose(f);
        }
        return 0;
    }
    
    return 0;
}

int cbox_hwcfg_setup_jack(void)
{
    int i;
    if (!cbox_config_has_section("autojack"))
        return 0;
    
    for (i = 0; ; i++)
    {
        int result;
        gchar *cardnum = g_strdup_printf("soundcard%d", i);
        char *secname = cbox_config_get_string("autojack", cardnum);
        g_free(cardnum);
        
        if (!secname)
            break;
        
        secname = g_strdup_printf("soundcard:%s", secname);
        result = try_soundcard(secname);
        g_free(secname);
        
        if (result)
            return 1;
    }
    
    return 0;
}

#endif
