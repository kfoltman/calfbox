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

#include "dspmath.h"
#include "errors.h"
#include "meter.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void clear_meter(struct cbox_meter *m)
{
    for (int i = 0; i < 2; i++)
    {
        m->volume[i] = 0.f;
        m->peak[i] = 0.f;
        m->last_peak[i] = 0.f;
    }
    m->smpcounter = 0;
}

gboolean cbox_meter_attach(struct cbox_recorder *handler, struct cbox_recording_source *src, GError **error)
{
    struct cbox_meter *m = handler->user_data;
    m->channels = src->channels;
    clear_meter(m);
    return TRUE;
}

void cbox_meter_record_block(struct cbox_recorder *handler, const float **buffers, uint32_t numsamples)
{
    struct cbox_meter *m = handler->user_data;
    for (int c = 0; c < m->channels; c++)
    {
        float peak = m->peak[c];
        float volume = m->volume[c];
        for (uint32_t i = 0; i < numsamples; i++)
        {
            float s = buffers[c][i];
            if (fabs(s) > peak)
                peak = fabs(s);
            volume += (s * s - volume) * 0.01; // XXXKF this is too simplistic, needs sample rate and proper time constant
        }
        m->peak[c] = peak;
        m->volume[c] = sanef(volume);
    }
    m->smpcounter += numsamples;
    if (m->smpcounter > m->srate)
    {
        for (int c = 0; c < m->channels; c++)
        {
            m->last_peak[c] = m->peak[c];
            m->peak[c] = 0;
        }
        m->smpcounter = 0;
    }
}

gboolean cbox_meter_detach(struct cbox_recorder *handler, GError **error)
{
    struct cbox_meter *m = handler->user_data;
    m->channels = 0;
    clear_meter(m);
    return TRUE;
}

void cbox_meter_destroy(struct cbox_recorder *handler)
{
    struct cbox_meter *m = handler->user_data;
    free(m);
}

static gboolean cbox_meter_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_meter *m = ct->user_data;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        return CBOX_OBJECT_DEFAULT_STATUS(&m->recorder, fb, error);
    }
    if (!strcmp(cmd->command, "/get_peak") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        float peak[2];
        for (int c = 0; c < 2; c++)
        {
            float v = m->peak[c], w = m->last_peak[c];
            if (v < w)
                v = w;
            peak[c] = v;
        }
        
        return cbox_execute_on(fb, NULL, "/peak", "ff", error, peak[0], peak[1]);
    }
    else
    if (!strcmp(cmd->command, "/get_rms") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        return cbox_execute_on(fb, NULL, "/rms", "ff", error, sqrt(m->volume[0]), sqrt(m->volume[1]));
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

struct cbox_meter *cbox_meter_new(struct cbox_document *document, int srate)
{
    struct cbox_meter *m = malloc(sizeof(struct cbox_meter));
    CBOX_OBJECT_HEADER_INIT(&m->recorder, cbox_recorder, document);
    m->recorder.user_data = m;
    cbox_command_target_init(&m->recorder.cmd_target, cbox_meter_process_cmd, m);
    m->recorder.attach = cbox_meter_attach;
    m->recorder.detach = cbox_meter_detach;
    m->recorder.record_block = cbox_meter_record_block;
    m->recorder.destroy = cbox_meter_destroy;
    m->srate = srate;
    clear_meter(m);
    CBOX_OBJECT_REGISTER(&m->recorder);
    return m;
}