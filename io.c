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

#include "app.h"
#include "config.h"
#include "config-api.h"
#include "engine.h"
#include "errors.h"
#include "hwcfg.h"
#include "io.h"
#include "meter.h"
#include "midi.h"
#include "mididest.h"
#include "recsrc.h"
#include "seq.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

const char *cbox_io_section = "io";

gboolean cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params, struct cbox_command_target *fb, GError **error)
{
#if USE_JACK
#if USE_LIBUSB
    if (cbox_config_get_int(cbox_io_section, "use_usb", 0))
        return cbox_io_init_usb(io, params, fb, error);
#endif
    return cbox_io_init_jack(io, params, fb, error);
#else
#if USE_LIBUSB
    return cbox_io_init_usb(io, params, fb, error);
#endif
#endif
}

int cbox_io_get_sample_rate(struct cbox_io *io)
{
    return io->impl->getsampleratefunc(io->impl);
}

void cbox_io_poll_ports(struct cbox_io *io, struct cbox_command_target *fb)
{
    io->impl->pollfunc(io->impl, fb);
}

int cbox_io_get_midi_data(struct cbox_io *io, struct cbox_midi_buffer *destination)
{
    return io->impl->getmidifunc(io->impl, destination);
}

int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb, struct cbox_command_target *fb)
{
    io->cb = cb;
    return io->impl->startfunc(io->impl, fb, NULL);
}

gboolean cbox_io_get_disconnect_status(struct cbox_io *io, GError **error)
{
    return io->impl->getstatusfunc(io->impl, error);
}

gboolean cbox_io_cycle(struct cbox_io *io, struct cbox_command_target *fb, GError **error)
{
    return io->impl->cyclefunc(io->impl, fb, error);
}

int cbox_io_stop(struct cbox_io *io)
{
    int result = io->impl->stopfunc(io->impl, NULL);
    if (io->cb && io->cb->on_stopped)
        io->cb->on_stopped(io->cb->user_data);
    io->cb = NULL;
    return result;
}

struct cbox_midi_output *cbox_io_get_midi_output(struct cbox_io *io, const char *name, const struct cbox_uuid *uuid)
{
    if (uuid)
    {
        for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_output *midiout = p->data;
            if (!midiout->removing && cbox_uuid_equal(&midiout->uuid, uuid))
                return midiout;
        }
    }
    if (name)
    {
        for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_output *midiout = p->data;
            if (!midiout->removing && !strcmp(midiout->name, name))
                return midiout;
        }
    }
    return NULL;
}

struct cbox_midi_input *cbox_io_get_midi_input(struct cbox_io *io, const char *name, const struct cbox_uuid *uuid)
{
    if (uuid)
    {
        for (GSList *p = io->midi_inputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_input *midiin = p->data;
            if (!midiin->removing && cbox_uuid_equal(&midiin->uuid, uuid))
                return midiin;
        }
    }
    if (name)
    {
        for (GSList *p = io->midi_inputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_input *midiin = p->data;
            if (!midiin->removing && !strcmp(midiin->name, name))
                return midiin;
        }
    }
    return NULL;
}

struct cbox_audio_output *cbox_io_get_audio_output(struct cbox_io *io, const char *name, const struct cbox_uuid *uuid)
{
    if (uuid)
    {
        for (GSList *p = io->audio_outputs; p; p = g_slist_next(p))
        {
            struct cbox_audio_output *audioout = p->data;
            if (!audioout->removing && cbox_uuid_equal(&audioout->uuid, uuid))
                return audioout;
        }
    }
    if (name)
    {
        for (GSList *p = io->audio_outputs; p; p = g_slist_next(p))
        {
            struct cbox_audio_output *audioout = p->data;
            if (!audioout->removing && !strcmp(audioout->name, name))
                return audioout;
        }
    }
    return NULL;
}

struct cbox_midi_output *cbox_io_create_midi_output(struct cbox_io *io, const char *name, GError **error)
{
    struct cbox_midi_output *midiout = cbox_io_get_midi_output(io, name, NULL);
    if (midiout)
        return midiout;
    
    midiout = io->impl->createmidioutfunc(io->impl, name, error);
    if (!midiout)
        return NULL;
    
    io->midi_outputs = g_slist_prepend(io->midi_outputs, midiout);

    // Notify client code to connect to new outputs if needed
    if (io->cb->on_midi_outputs_changed)
        io->cb->on_midi_outputs_changed(io->cb->user_data);
    return midiout;
}

void cbox_io_destroy_midi_output(struct cbox_io *io, struct cbox_midi_output *midiout)
{
    midiout->removing = TRUE;
    
    // This is not a very efficient way to do it. However, in this case,
    // the list will rarely contain more than 10 elements, so simplicity
    // and correctness may be more important.
    GSList *copy = g_slist_copy(io->midi_outputs);
    copy = g_slist_remove(copy, midiout);

    GSList *old = io->midi_outputs;
    io->midi_outputs = copy;

    cbox_midi_merger_close(&midiout->merger, app.rt);
    assert(!midiout->merger.inputs);

    // Notify client code to disconnect the output and to make sure the RT code
    // is not using the old list anymore
    if (io->cb->on_midi_outputs_changed)
        io->cb->on_midi_outputs_changed(io->cb->user_data);

    assert(!midiout->merger.inputs);
    
    g_slist_free(old);
    io->impl->destroymidioutfunc(io->impl, midiout);
}

struct cbox_midi_input *cbox_io_create_midi_input(struct cbox_io *io, const char *name, GError **error)
{
    struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, name, NULL);
    if (midiin)
        return midiin;
    
    midiin = io->impl->createmidiinfunc(io->impl, name, error);
    if (!midiin)
        return NULL;
    
    io->midi_inputs = g_slist_prepend(io->midi_inputs, midiin);

    // Notify client code to connect to new inputs if needed
    if (io->cb->on_midi_inputs_changed)
        io->cb->on_midi_inputs_changed(io->cb->user_data);
    return midiin;
}

void cbox_io_destroy_midi_input(struct cbox_io *io, struct cbox_midi_input *midiin)
{
    midiin->removing = TRUE;
    
    // This is not a very efficient way to do it. However, in this case,
    // the list will rarely contain more than 10 elements, so simplicity
    // and correctness may be more important.
    GSList *copy = g_slist_copy(io->midi_inputs);
    copy = g_slist_remove(copy, midiin);

    GSList *old = io->midi_inputs;
    io->midi_inputs = copy;

    // Notify client code to disconnect the input and to make sure the RT code
    // is not using the old list anymore
    if (io->cb->on_midi_inputs_changed)
        io->cb->on_midi_inputs_changed(io->cb->user_data);
    
    g_slist_free(old);
    io->impl->destroymidiinfunc(io->impl, midiin);
}

void cbox_io_destroy_all_midi_ports(struct cbox_io *io)
{
    for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
    {
        struct cbox_midi_output *midiout = p->data;
        midiout->removing = TRUE;
    }
    for (GSList *p = io->midi_inputs; p; p = g_slist_next(p))
    {
        struct cbox_midi_output *midiin = p->data;
        midiin->removing = TRUE;
    }
    
    GSList *old_i = io->midi_inputs, *old_o = io->midi_outputs;
    io->midi_outputs = NULL;
    io->midi_inputs = NULL;
    // Notify client code to disconnect the output and to make sure the RT code
    // is not using the old list anymore
    if (io->cb && io->cb->on_midi_outputs_changed)
        io->cb->on_midi_outputs_changed(io->cb->user_data);
    if (io->cb && io->cb->on_midi_inputs_changed)
        io->cb->on_midi_inputs_changed(io->cb->user_data);
    
    while(old_o)
    {
        struct cbox_midi_output *midiout = old_o->data;
        cbox_midi_merger_close(&midiout->merger, app.rt);
        assert(!midiout->merger.inputs);
        io->impl->destroymidioutfunc(io->impl, midiout);
        old_o = g_slist_remove(old_o, midiout);
    }
    g_slist_free(old_o);

    while(old_i)
    {
        struct cbox_midi_input *midiin = old_i->data;
        io->impl->destroymidiinfunc(io->impl, midiin);
        old_i = g_slist_remove(old_i, midiin);
    }
    g_slist_free(old_i);
}

struct cbox_audio_output *cbox_io_create_audio_output(struct cbox_io *io, const char *name, GError **error)
{
    struct cbox_audio_output *audioout = cbox_io_get_audio_output(io, name, NULL);
    if (audioout)
        return audioout;

    audioout = io->impl->createaudiooutfunc(io->impl, name, error);
    if (!audioout)
        return NULL;

    io->audio_outputs = g_slist_prepend(io->audio_outputs, audioout);

    // Notify client code to connect to new outputs if needed
    if (io->cb->on_audio_outputs_changed)
        io->cb->on_audio_outputs_changed(io->cb->user_data);
    return audioout;
}

void cbox_io_destroy_audio_output(struct cbox_io *io, struct cbox_audio_output *audioout)
{
    audioout->removing = TRUE;

    // This is not a very efficient way to do it. However, in this case,
    // the list will rarely contain more than 10 elements, so simplicity
    // and correctness may be more important.
    GSList *copy = g_slist_copy(io->audio_outputs);
    copy = g_slist_remove(copy, audioout);

    GSList *old = io->audio_outputs;
    io->audio_outputs = copy;

    // Notify client code to disconnect the output and to make sure the RT code
    // is not using the old list anymore
    if (io->cb->on_audio_outputs_changed)
        io->cb->on_audio_outputs_changed(io->cb->user_data);

    g_slist_free(old);
    io->impl->destroyaudiooutfunc(io->impl, audioout);
}

gboolean cbox_io_process_cmd(struct cbox_io *io, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error, gboolean *cmd_handled)
{
    *cmd_handled = FALSE;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        *cmd_handled = TRUE;
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        for (GSList *p = io->midi_inputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_input *midiin = p->data;
            if (!midiin->removing)
            {
                if (!cbox_execute_on(fb, NULL, "/midi_input", "su", error, midiin->name, &midiin->uuid))
                    return FALSE;
            }
        }
        for (GSList *p = io->midi_outputs; p; p = g_slist_next(p))
        {
            struct cbox_midi_output *midiout = p->data;
            if (!midiout->removing)
            {
                if (!cbox_execute_on(fb, NULL, "/midi_output", "su", error, midiout->name, &midiout->uuid))
                    return FALSE;
            }
        }
        return cbox_execute_on(fb, NULL, "/client_type", "s", error, "USB") &&
            cbox_execute_on(fb, NULL, "/audio_inputs", "i", error, io->io_env.input_count) &&
            cbox_execute_on(fb, NULL, "/audio_outputs", "i", error, io->io_env.output_count) &&
            cbox_execute_on(fb, NULL, "/sample_rate", "i", error, io->io_env.srate) &&
            cbox_execute_on(fb, NULL, "/buffer_size", "i", error, io->io_env.buffer_size);
    }
    else if (io->impl->createmidiinfunc && !strcmp(cmd->command, "/create_midi_input") && !strcmp(cmd->arg_types, "s"))
    {
        *cmd_handled = TRUE;
        struct cbox_midi_input *midiin;
        midiin = cbox_io_create_midi_input(io, CBOX_ARG_S(cmd, 0), error);
        if (!midiin)
            return FALSE;
        cbox_midi_appsink_init(&midiin->appsink, app.rt, &app.engine->stmap->tmap);
        return cbox_uuid_report(&midiin->uuid, fb, error);
    }
    else if (!strcmp(cmd->command, "/route_midi_input") && !strcmp(cmd->arg_types, "ss"))
    {
        *cmd_handled = TRUE;
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
        if (!midiin)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        if (*CBOX_ARG_S(cmd, 1))
        {
            if (cbox_uuid_fromstring(&midiin->output, CBOX_ARG_S(cmd, 1), error))
                midiin->output_set = TRUE;
        }
        else
            midiin->output_set = FALSE;
        if (io->impl->updatemidiinroutingfunc)
            io->impl->updatemidiinroutingfunc(io->impl);
        if (io->cb->on_midi_inputs_changed)
            io->cb->on_midi_inputs_changed(io->cb->user_data);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/set_appsink_for_midi_input") && !strcmp(cmd->arg_types, "si"))
    {
        *cmd_handled = TRUE;
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
        if (!midiin)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        midiin->enable_appsink = CBOX_ARG_I(cmd, 1);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/get_new_events") && !strcmp(cmd->arg_types, "s"))
    {
        *cmd_handled = TRUE;
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
        if (!midiin)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        if (!midiin->enable_appsink)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "App sink not enabled for port '%s'", uuidstr);
            return FALSE;
        }
        return cbox_midi_appsink_send_to(&midiin->appsink, fb, error);
    }
    else if (io->impl->createmidioutfunc && !strcmp(cmd->command, "/create_midi_output") && !strcmp(cmd->arg_types, "s"))
    {
        *cmd_handled = TRUE;
        struct cbox_midi_output *midiout;
        midiout = cbox_io_create_midi_output(io, CBOX_ARG_S(cmd, 0), error);
        if (!midiout)
            return FALSE;
        return cbox_uuid_report(&midiout->uuid, fb, error);
    }
    else if (io->impl->destroymidiinfunc && !strcmp(cmd->command, "/delete_midi_input") && !strcmp(cmd->arg_types, "s"))
    {
        *cmd_handled = TRUE;
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_input *midiin = cbox_io_get_midi_input(io, NULL, &uuid);
        if (!midiin)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        cbox_io_destroy_midi_input(io, midiin);
        return TRUE;
    }
    else if (io->impl->destroymidioutfunc && !strcmp(cmd->command, "/delete_midi_output") && !strcmp(cmd->arg_types, "s"))
    {
        *cmd_handled = TRUE;
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_midi_output *midiout = cbox_io_get_midi_output(io, NULL, &uuid);
        if (!midiout)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        cbox_io_destroy_midi_output(io, midiout);
        return TRUE;
    }
    else if (io->impl->createaudiooutfunc && !strcmp(cmd->command, "/create_audio_output") && !strcmp(cmd->arg_types, "s"))
    {
        *cmd_handled = TRUE;
        struct cbox_audio_output *audioout;
        audioout = cbox_io_create_audio_output(io, CBOX_ARG_S(cmd, 0), error);
        if (!audioout)
            return FALSE;
        return cbox_uuid_report(&audioout->uuid, fb, error);
    }
    else if (io->impl->destroyaudiooutfunc && !strcmp(cmd->command, "/delete_audio_output") && !strcmp(cmd->arg_types, "s"))
    {
        *cmd_handled = TRUE;
        const char *uuidstr = CBOX_ARG_S(cmd, 0);
        struct cbox_uuid uuid;
        if (!cbox_uuid_fromstring(&uuid, uuidstr, error))
            return FALSE;
        struct cbox_audio_output *audioout = cbox_io_get_audio_output(io, NULL, &uuid);
        if (!audioout)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Port '%s' not found", uuidstr);
            return FALSE;
        }
        cbox_io_destroy_audio_output(io, audioout);
        return TRUE;
    }
    return FALSE;
}

void cbox_io_close(struct cbox_io *io)
{
    io->impl->destroyfunc(io->impl);
    io->impl = NULL;
}

