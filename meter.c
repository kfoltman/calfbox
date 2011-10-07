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

#include "errors.h"
#include "meter.h"
#include <math.h>
#include <stdlib.h>

static void clear_meter(struct cbox_meter *m)
{
    for (int i = 0; i < 2; i++)
    {
        m->volume[i] = 0.f;
        m->peak[i] = 0.f;
    }
}

void cbox_meter_attach(struct cbox_recorder *handler, struct cbox_recording_source *src)
{
    struct cbox_meter *m = handler->user_data;
    m->channels = src->channels;
    clear_meter(m);
}

void cbox_meter_record_block(struct cbox_recorder *handler, const float **buffers, uint32_t numsamples)
{
    struct cbox_meter *m = handler->user_data;
    for (int c = 0; c < m->channels; c++)
    {
        float peak = m->peak[c];
        float volume = m->volume[c];
        for (int i = 0; i < numsamples; i++)
        {
            float s = buffers[c][i];
            if (fabs(s) > peak)
                peak = fabs(s);
            volume += (s * s - volume) * 0.01; // XXXKF this is too simplistic, needs sample rate and proper time constant
        }
        m->peak[c] = peak;
        m->volume[c] = volume;
    }
}

void cbox_meter_detach(struct cbox_recorder *handler)
{
    struct cbox_meter *m = handler->user_data;
    m->channels = 0;
    clear_meter(m);
}

void cbox_meter_destroy(struct cbox_recorder *handler)
{
    struct cbox_meter *m = handler->user_data;
    free(m);
}

static gboolean cbox_meter_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_meter *m = ct->user_data;
    if (!strcmp(cmd->command, "/get_peak") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        return cbox_execute_on(fb, NULL, "/peak", "ff", error, m->peak[0], m->peak[1]);
    }
    else
    if (!strcmp(cmd->command, "/get_rms") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        return cbox_execute_on(fb, NULL, "/rms", "ff", error, sqrt(m->volume[0]), sqrt(m->volume[1]));
    }
    else
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return FALSE;
    }
}

struct cbox_meter *cbox_meter_new()
{
    struct cbox_meter *m = malloc(sizeof(struct cbox_meter));
    m->recorder.user_data = m;
    cbox_command_target_init(&m->recorder.cmd_target, cbox_meter_process_cmd, m);
    m->recorder.attach = cbox_meter_attach;
    m->recorder.detach = cbox_meter_detach;
    m->recorder.record_block = cbox_meter_record_block;
    m->recorder.destroy = cbox_meter_destroy;
    clear_meter(m);
}