/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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

/* 

Note: this is a silly experimental driver for a number of USB MIDI devices.

It only supports audio output and MIDI input, as those are my immediate
needs, to be able to use a machine running CalfBox as a standalone MIDI
instrument. Plug-and-play is supported, as long as current user running
calfbox has write access to the USB devices involved. This can be done by
running calfbox as root, or by setting right permissions in udev scripts
- this may be considered a safer method.

Devices supported:
* Class-compliant audio output devices (tested with Lexicon Omega and some 
  cheap no-brand C-Media USB soundcard dongle)
* Alesis Multimix 8 USB 2.0 (audio output only)
* Class-compliant MIDI input devices (tested with several devices)

Yes, code quality is pretty awful, especially in areas involving clock
sync. I'm going to clean it up iteratively later.

*/

#include "config.h"
#include "config-api.h"
#include "errors.h"
#include "hwcfg.h"
#include "io.h"
#include "meter.h"
#include "midi.h"
#include "recsrc.h"
#include "usbio_impl.h"

#include <assert.h>
#include <errno.h>
#include <libusb.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>

///////////////////////////////////////////////////////////////////////////////

int cbox_usbio_get_sample_rate(struct cbox_io_impl *impl)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;
    
    return uii->sample_rate;
}

gboolean cbox_usbio_get_status(struct cbox_io_impl *impl, GError **error)
{
    // XXXKF: needs a flag that would indicate whether device is present
    // XXXKF: needs to return that flag with appropriate message
    return TRUE;
}

static void run_audio_loop(struct cbox_usb_io_impl *uii)
{
    while(!uii->stop_engine && !uii->device_removed) {
        struct cbox_io *io = uii->ioi.pio;
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 1000
        };
        libusb_handle_events_timeout(uii->usbctx, &tv);
        for (GSList *p = io->midi_outputs; p; p = p->next)
        {
            struct cbox_usb_midi_output *umo = p->data;
            usbio_send_midi_to_output(umo);
        }
    }
}

void usbio_run_idle_loop(struct cbox_usb_io_impl *uii)
{
    while(!uii->stop_engine)
    {
        struct cbox_io *io = uii->ioi.pio;
        for (int b = 0; b < uii->output_channels; b++)
            memset(io->output_buffers[b], 0, io->io_env.buffer_size * sizeof(float));
        io->cb->process(io->cb->user_data, io, io->io_env.buffer_size);
        for (GList *p = uii->rt_midi_ports; p; p = p->next)
        {
            struct cbox_usb_midi_interface *umi = p->data;
            cbox_midi_buffer_clear(&umi->input_port->hdr.buffer);
        }
        for (GSList *p = io->midi_outputs; p; p = p->next)
        {
            struct cbox_usb_midi_output *umo = p->data;
            usbio_send_midi_to_output(umo);
        }
        
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 1
        };
        libusb_handle_events_timeout(uii->usbctx, &tv);
        usleep((int)(io->io_env.buffer_size * 1000000.0 / uii->sample_rate));
    }
}

static void *engine_thread(void *user_data)
{
    struct cbox_usb_io_impl *uii = user_data;
    
    usbio_start_midi_capture(uii);

    if (uii->handle_audiodev)
    {
        uii->no_resubmit = FALSE;
        struct sched_param p;
        memset(&p, 0, sizeof(p));
        p.sched_priority = cbox_config_get_int("io", "rtpriority", 10);
        pid_t tid = syscall(SYS_gettid);
        if (0 != sched_setscheduler(tid, SCHED_FIFO, &p))
            g_warning("Cannot set realtime priority for the processing thread: %s.", strerror(errno));
        
        usbio_start_audio_playback(uii);
        if (!uii->setup_error)
        {
            run_audio_loop(uii);
        }
        uii->no_resubmit = TRUE;
        memset(&p, 0, sizeof(p));
        p.sched_priority = 0;
        if (0 != sched_setscheduler(tid, SCHED_OTHER, &p))
            g_warning("Cannot unset realtime priority for the processing thread: %s.", strerror(errno));
        usbio_stop_audio_playback(uii);
    }
    else
    {
        uii->no_resubmit = TRUE;
        g_message("No audio device found - running idle loop.");
        // notify the UI thread that the (fake) audio loop is running
        uii->playback_counter = uii->playback_buffers;
        usbio_run_idle_loop(uii);
    }
    
    usbio_stop_midi_capture(uii);
    return NULL;
}

static void cbox_usbio_destroy_midi_out(struct cbox_io_impl *ioi, struct cbox_midi_output *midiout)
{
    g_free(midiout->name);
    free(midiout);
}

static struct cbox_usb_midi_interface *cur_midi_interface = NULL;

struct cbox_midi_input *cbox_usbio_create_midi_in(struct cbox_io_impl *impl, const char *name, GError **error)
{
    // struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;
    struct cbox_usb_midi_input *input = calloc(1, sizeof(struct cbox_usb_midi_input));
    input->hdr.name = g_strdup(name);
    input->hdr.removing = FALSE;
    cbox_uuid_generate(&input->hdr.uuid);
    cbox_midi_buffer_init(&input->hdr.buffer);
    input->ifptr = cur_midi_interface;
    cbox_midi_appsink_init(&input->hdr.appsink, NULL);
    input->hdr.enable_appsink = FALSE;

    return (struct cbox_midi_input *)input;
}

struct cbox_midi_output *cbox_usbio_create_midi_out(struct cbox_io_impl *impl, const char *name, GError **error)
{
    // struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;
    struct cbox_usb_midi_output *output = calloc(1, sizeof(struct cbox_usb_midi_output));
    output->hdr.name = g_strdup(name);
    output->hdr.removing = FALSE;
    cbox_uuid_generate(&output->hdr.uuid);
    cbox_midi_buffer_init(&output->hdr.buffer);
    cbox_midi_merger_init(&output->hdr.merger, &output->hdr.buffer);
    output->ifptr = cur_midi_interface;

    return (struct cbox_midi_output *)output;
}

static void create_midi_ports(struct cbox_usb_io_impl *uii)
{
    uii->ioi.createmidiinfunc = cbox_usbio_create_midi_in;
    uii->ioi.createmidioutfunc = cbox_usbio_create_midi_out;
    for (GList *p = uii->midi_ports; p; p = p->next)
    {
        struct cbox_usb_midi_interface *umi = p->data;
        char buf[80];
        sprintf(buf, "usb:%03d:%03d", umi->devinfo->bus, umi->devinfo->devadr);
        cur_midi_interface = umi;
        if (umi->epdesc_in.found)
            umi->input_port = (struct cbox_usb_midi_input *)cbox_io_create_midi_input(uii->ioi.pio, buf, NULL);
        else
            umi->input_port = NULL;
        if (umi->epdesc_out.found)
            umi->output_port = (struct cbox_usb_midi_output *)cbox_io_create_midi_output(uii->ioi.pio, buf, NULL);
        else
            umi->output_port = NULL;
    }
    uii->ioi.createmidiinfunc = NULL;
    uii->ioi.createmidioutfunc = NULL;
    cur_midi_interface = NULL;
}

gboolean cbox_usbio_start(struct cbox_io_impl *impl, struct cbox_command_target *fb, GError **error)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;

    // XXXKF: needs to queue the playback and capture transfers

    uii->stop_engine = FALSE;
    uii->setup_error = FALSE;
    uii->playback_counter = 0;

    create_midi_ports(uii);

    struct cbox_io *io = uii->ioi.pio;
    if (io->cb->on_started)
        io->cb->on_started(io->cb->user_data);
    
    if (pthread_create(&uii->thr_engine, NULL, engine_thread, uii))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "cannot create engine thread: %s", strerror(errno));
        return FALSE;
    }
    while(!uii->setup_error && uii->playback_counter < uii->playback_buffers)
        usleep(10000);
    usbio_update_port_routing(&uii->ioi);

    return TRUE;
}

gboolean cbox_usbio_stop(struct cbox_io_impl *impl, GError **error)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;

    // XXXKF: needs to kill the playback and capture transfers, and
    // wait for them to be killed

    uii->stop_engine = TRUE;
    pthread_join(uii->thr_engine, NULL);
    return TRUE;
}

void cbox_usbio_poll_ports(struct cbox_io_impl *impl, struct cbox_command_target *fb)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;

    // Dry run, just to detect if anything changed
    if (usbio_scan_devices(uii, TRUE))
    {
        struct cbox_io_callbacks *cb = uii->ioi.pio->cb;
        g_debug("Restarting I/O due to device being connected or disconnected");
        cbox_io_stop(uii->ioi.pio);
        // Re-scan, this time actually create the MIDI inputs
        usbio_scan_devices(uii, FALSE);
        cbox_io_start(uii->ioi.pio, cb, fb);
    }
}

gboolean cbox_usbio_cycle(struct cbox_io_impl *impl, struct cbox_command_target *fb, GError **error)
{
    // struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;
    // XXXKF: this is for restarting the thing; not implemented for now,
    // the implementation will be something like in case of JACK - close and
    // reopen.
    return TRUE;
}

int cbox_usbio_get_midi_data(struct cbox_io_impl *impl, struct cbox_midi_buffer *destination)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;

    cbox_midi_merger_render_to(&uii->midi_input_merger, destination);
    return 0;
}

void cbox_usbio_destroy(struct cbox_io_impl *impl)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;
    
    GList *prev_keys = g_hash_table_get_values(uii->device_table);
    for (GList *p = prev_keys; p; p = p->next)
    {
        struct cbox_usb_device_info *udi = p->data;
        if (udi->status == CBOX_DEVICE_STATUS_OPENED)
            usbio_forget_device(uii, udi);
    }
    g_list_free(prev_keys);
    g_hash_table_destroy(uii->device_table);
    
    libusb_exit(uii->usbctx_probe);
    libusb_exit(uii->usbctx);
    cbox_midi_merger_close(&uii->midi_input_merger);
    free(uii);
}

///////////////////////////////////////////////////////////////////////////////

struct usbio_transfer *usbio_transfer_new(struct libusb_context *usbctx, const char *transfer_type, int index, int isopackets, void *user_data)
{
    struct usbio_transfer *p = malloc(sizeof(struct usbio_transfer));
    p->usbctx = usbctx;
    p->transfer = libusb_alloc_transfer(isopackets);
    p->index = index;
    p->cancel_confirm = FALSE;
    p->pending = FALSE;
    p->transfer_type = transfer_type;
    p->user_data = user_data;
    return p;
}

int usbio_transfer_submit(struct usbio_transfer *xfer)
{
    int res = libusb_submit_transfer(xfer->transfer);
    if (res != 0)
    {
        g_warning("usbio_transfer_submit: cannot submit transfer '%s:%d', error = %s", xfer->transfer_type, xfer->index, libusb_error_name(res));
        return res;
    }
    xfer->pending = TRUE;
    return 0;
}

void usbio_transfer_shutdown(struct usbio_transfer *xfer)
{
    if (xfer->pending)
    {
        int res = libusb_cancel_transfer(xfer->transfer);
        if (res != LIBUSB_ERROR_NO_DEVICE)
        {
            int tries = 100;
            while(!xfer->cancel_confirm && tries > 0 && xfer->pending)
            {
                struct timeval tv = {
                    .tv_sec = 0,
                    .tv_usec = 1000
                };
                libusb_handle_events_timeout(xfer->usbctx, &tv);
                tries--;
            }
            if (!tries)
                g_warning("Timed out waiting for transfer '%s:%d' to complete; status = %d", xfer->transfer_type, xfer->index, xfer->transfer->status);
        }
    }
}

void usbio_transfer_destroy(struct usbio_transfer *xfer)
{
    libusb_free_transfer(xfer->transfer);
    free(xfer);
}


///////////////////////////////////////////////////////////////////////////////

static gboolean cbox_usb_io_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)ct->user_data;
    struct cbox_io *io = uii->ioi.pio;
    gboolean handled = FALSE;
    if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;
        
        for (GList *p = uii->midi_ports; p; p = g_list_next(p))
        {
            struct cbox_usb_midi_interface *midi = p->data;
            struct cbox_usb_device_info *di = midi->devinfo;
            if (midi->epdesc_in.found && !cbox_execute_on(fb, NULL, "/usb_midi_input", "iiiiu", error, di->bus, di->devadr, di->vid, di->pid, &midi->input_port->hdr.uuid))
                return FALSE;
            if (midi->epdesc_out.found && !cbox_execute_on(fb, NULL, "/usb_midi_output", "iiiiu", error, di->bus, di->devadr, di->vid, di->pid, &midi->output_port->hdr.uuid))
                return FALSE;
        }
        
        return cbox_execute_on(fb, NULL, "/output_resolution", "i", error, 8 * uii->output_resolution) &&
            cbox_io_process_cmd(io, fb, cmd, error, &handled);
    }
    else
    {
        gboolean result = cbox_io_process_cmd(io, fb, cmd, error, &handled);
        if (!handled)
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Unknown combination of target path and argument: '%s', '%s'", cmd->command, cmd->arg_types);
        return result;
    }
}

///////////////////////////////////////////////////////////////////////////////

gboolean cbox_io_init_usb(struct cbox_io *io, struct cbox_open_params *const params, struct cbox_command_target *fb, GError **error)
{
    struct cbox_usb_io_impl *uii = malloc(sizeof(struct cbox_usb_io_impl));
    if (libusb_init(&uii->usbctx))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot initialise libusb.");
        return FALSE;
    }
    if (libusb_init(&uii->usbctx_probe))
    {
        libusb_exit(uii->usbctx);
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot initialise libusb.");
        return FALSE;
    }
    libusb_set_debug(uii->usbctx, 3);
    libusb_set_debug(uii->usbctx_probe, 3);
    uii->device_table = g_hash_table_new(g_direct_hash, NULL);

    uii->sample_rate = cbox_config_get_int(cbox_io_section, "sample_rate", 44100);
    uii->sync_buffers = cbox_config_get_int(cbox_io_section, "sync_buffers", 2);
    uii->debug_sync = cbox_config_get_int(cbox_io_section, "debug_sync", 0);
    uii->playback_buffers = cbox_config_get_int(cbox_io_section, "playback_buffers", 2);
    // shouldn't be more than 4, otherwise it will crackle due to limitations of
    // the packet length adjustment. It might work better if adjustment
    // was per-packet and not per-transfer.
    uii->iso_packets = cbox_config_get_int(cbox_io_section, "iso_packets", 1);
    // The USB 2.0 device uses a higher packet rate (125us I think), so the
    // default number of packets per transfer needs to be different, too -
    // 1ms is a minimum reasonable value
    uii->iso_packets_multimix = cbox_config_get_int(cbox_io_section, "iso_packets_multimix", 16);
    uii->output_resolution = cbox_config_get_int(cbox_io_section, "output_resolution", 16) / 8;
    uii->output_channels = 2;
    uii->handle_audiodev = NULL;
    cbox_midi_merger_init(&uii->midi_input_merger, NULL);
    
    // fixed processing buffer size, as we have to deal with packetisation anyway
    io->io_env.srate = uii->sample_rate;
    io->io_env.buffer_size = 64;
    io->cb = NULL;
    // input and output count is hardcoded for simplicity - in future, it may be
    // necessary to add support for the extra inputs (needs to be figured out)
    io->io_env.input_count = 2; //cbox_config_get_int("io", "inputs", 0);
    io->input_buffers = malloc(sizeof(float *) * io->io_env.input_count);
    for (int i = 0; i < io->io_env.input_count; i++)
        io->input_buffers[i] = calloc(io->io_env.buffer_size, sizeof(float));
    io->io_env.output_count = 2; // cbox_config_get_int("io", "outputs", 2);
    io->output_buffers = malloc(sizeof(float *) * io->io_env.output_count);
    for (int i = 0; i < io->io_env.output_count; i++)
        io->output_buffers[i] = calloc(io->io_env.buffer_size, sizeof(float));
    io->impl = &uii->ioi;
    cbox_command_target_init(&io->cmd_target, cbox_usb_io_process_cmd, uii);

    uii->ioi.pio = io;
    uii->ioi.getsampleratefunc = cbox_usbio_get_sample_rate;
    uii->ioi.startfunc = cbox_usbio_start;
    uii->ioi.stopfunc = cbox_usbio_stop;
    uii->ioi.getstatusfunc = cbox_usbio_get_status;
    uii->ioi.pollfunc = cbox_usbio_poll_ports;
    uii->ioi.cyclefunc = cbox_usbio_cycle;
    uii->ioi.getmidifunc = cbox_usbio_get_midi_data;
    uii->ioi.destroymidioutfunc = cbox_usbio_destroy_midi_out;
    uii->ioi.destroyfunc = cbox_usbio_destroy;
    uii->ioi.updatemidiinroutingfunc = usbio_update_port_routing;
    uii->midi_ports = NULL;
    
    usbio_scan_devices(uii, FALSE);
    
    if (cbox_config_get_int("io", "lockall", 0))
        mlockall(MCL_CURRENT|MCL_FUTURE);

    return TRUE;
    
}

