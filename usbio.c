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
* Lexicon Omega (audio output and MIDI input)
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

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <libusb.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>

#define OMEGA_TIMEOUT 2000

#define OMEGA_EP_PLAYBACK 0x01
//#define OMEGA_EP_CAPTURE 0x83

#define MULTIMIX_EP_PLAYBACK 0x02
//#define MULTIMIX_EP_CAPTURE 0x86
#define MULTIMIX_EP_SYNC 0x81

#define NUM_SYNC_PACKETS 10

struct cbox_usb_io_impl
{
    struct cbox_io_impl ioi;

    struct libusb_context *usbctx;
    struct libusb_context *usbctx_probe;
    
    GHashTable *device_table;
    
    struct libusb_device_handle *handle_audiodev;
    int sample_rate, buffer_size, output_resolution;
    int output_channels; // always 2 for now

    unsigned int playback_buffers;
    unsigned int sync_buffers;
    int playback_counter;
    int sync_counter;

    unsigned int iso_packets_omega, iso_packets_multimix;

    pthread_t thr_engine;
    volatile gboolean stop_engine, setup_error;
    
    int desync;
    uint64_t samples_played;
    int cancel_confirm;
    int device_removed;
    struct libusb_transfer **playback_transfers;
    struct libusb_transfer **sync_transfers;
    int read_ptr;
    
    GList *midi_input_ports;
    GList *rt_midi_input_ports;
    struct cbox_midi_buffer **midi_input_port_buffers;
    int *midi_input_port_pos;
    int midi_input_port_count;
    struct libusb_transfer *(*play_function)(struct cbox_usb_io_impl *uii, int index);
    int8_t audio_output_endpoint;
    int8_t audio_sync_endpoint;
    uint32_t sync_freq;
    uint32_t audio_output_pktsize;
    int debug_sync;
};

enum cbox_usb_device_status
{
    CBOX_DEVICE_STATUS_PROBING,
    CBOX_DEVICE_STATUS_UNSUPPORTED,
    CBOX_DEVICE_STATUS_OPENED,
};

struct cbox_usb_device_info
{
    struct libusb_device *dev;
    struct libusb_device_handle *handle;
    enum cbox_usb_device_status status;
    uint8_t bus;
    uint8_t devadr;
    uint16_t busdevadr;
    struct {
        uint16_t vid;
        uint16_t pid;
    };
    int active_config;
    gboolean is_midi, is_audio;
    uint32_t configs_with_midi;
    uint32_t configs_with_audio;
    time_t last_probe_time;
    int failures;
};

struct cbox_usb_midi_input
{
    struct cbox_usb_io_impl *uii;
    struct libusb_device_handle *handle;
    int busdevadr;
    int endpoint;
    int max_packet_size;
    struct libusb_transfer *transfer;
    struct cbox_midi_buffer midi_buffer;
    uint8_t midi_recv_data[256];
};

static gboolean scan_devices(struct cbox_usb_io_impl *uii, gboolean probe_only);
static void forget_device(struct cbox_usb_io_impl *uii, struct cbox_usb_device_info *devinfo);
static void run_idle_loop(struct cbox_usb_io_impl *uii);

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

static struct libusb_transfer *play_stuff_adaptive(struct cbox_usb_io_impl *uii, int index);
static struct libusb_transfer *play_stuff_asynchronous(struct cbox_usb_io_impl *uii, int index);

static void fill_playback_buffer(struct cbox_usb_io_impl *uii, struct libusb_transfer *transfer)
{
    static double phase = 0;
    static int phase2 = 0;
    struct cbox_io *io = uii->ioi.pio;
    uint8_t *data8 = (uint8_t*)transfer->buffer;
    int16_t *data = (int16_t*)transfer->buffer;
    int resolution = uii->output_resolution;
    int oc = uii->output_channels;
    int rptr = uii->read_ptr;
    int nframes = transfer->length / (resolution * oc);
    int i, j, b;

    for (i = 0; i < nframes; )
    {
        if (rptr == io->buffer_size)
        {
            for (b = 0; b < oc; b++)
                memset(io->output_buffers[b], 0, io->buffer_size * sizeof(float));
            io->cb->process(io->cb->user_data, io, io->buffer_size);
            for (GList *p = uii->rt_midi_input_ports; p; p = p->next)
            {
                struct cbox_usb_midi_input *umi = p->data;
                cbox_midi_buffer_clear(&umi->midi_buffer);
            }
            rptr = 0;
        }
        int left1 = nframes - i;
        int left2 = io->buffer_size - rptr;
        if (left1 > left2)
            left1 = left2;

        for (b = 0; b < oc; b++)
        {
            float *obuf = io->output_buffers[b] + rptr;
            if (resolution == 2)
            {
                int16_t *tbuf = data + oc * i + b;
                for (j = 0; j < left1; j++)
                {
                    float v = 32767 * obuf[j];
                    if (v < -32768)
                        v = -32768;
                    if (v > +32767)
                        v = +32767;
                    *tbuf = (int16_t)v;
                    tbuf += oc;
                }
            }
            if (resolution == 3)
            {
                uint8_t *tbuf = data8 + (oc * i + b) * 3;
                for (j = 0; j < left1; j++)
                {
                    float v = 0x7FFFFF * obuf[j];
                    if (v < -0x800000)
                        v = -0x800000;
                    if (v > +0x7FFFFF)
                        v = +0x7FFFFF;
                    int vi = (int)v;
                    tbuf[0] = vi & 255;
                    tbuf[1] = (vi >> 8) & 255;
                    tbuf[2] = (vi >> 16) & 255;
                    tbuf += oc * 3;
                }
            }
        }
        i += left1;
        rptr += left1;
    }
    uii->read_ptr = rptr;
}

static void play_callback_adaptive(struct libusb_transfer *transfer)
{
    struct cbox_usb_io_impl *uii = transfer->user_data;
    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
    {
        uii->cancel_confirm = 1;
        return;
    }
    if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE)
    {
        uii->device_removed++;
        return;
    }
    
    int resolution = uii->output_resolution;
    int oc = uii->output_channels;
    gboolean init_finished = uii->playback_counter == uii->playback_buffers;
    if (uii->playback_counter < uii->playback_buffers)
    {
        // send another URB for the next transfer before re-submitting
        // this one
        uii->playback_transfers[uii->playback_counter] = (*uii->play_function)(uii, uii->playback_counter);
        uii->playback_counter++;
    }
    // printf("Play Callback! %d %p status %d\n", (int)transfer->length, transfer->buffer, (int)transfer->status);

    int tlen = 0;
    for (int i = 0; i < transfer->num_iso_packets; i++)
    {
        tlen += transfer->iso_packet_desc[i].actual_length;
        if (transfer->iso_packet_desc[i].status)
            printf("ISO error: index = %d i = %d status = %d\n", (int)transfer->user_data, i, transfer->iso_packet_desc[i].status);
    }
    uii->samples_played += transfer->length / (oc * resolution);
    int nsamps = uii->sample_rate / 1000;
    // If time elapsed is greater than 
    int lag = uii->desync / (1000 * transfer->num_iso_packets);
    if (lag > 0 && nsamps < uii->audio_output_pktsize)
    {
        nsamps++;
        lag--;
    }

    transfer->length = nsamps * transfer->num_iso_packets * oc * resolution;
    libusb_set_iso_packet_lengths(transfer, nsamps * oc * resolution);

    if (init_finished)
    {
        fill_playback_buffer(uii, transfer);
    }
    // desync value is expressed in milli-frames, i.e. desync of 1000 means 1 frame of lag
    // It takes 1ms for each iso packet to be transmitted. Each transfer consists of
    // num_iso_packets packets. So, this transfer took uii->sample_rate milli-frames.
    uii->desync += transfer->num_iso_packets * uii->sample_rate;
    // ... but during that time, tlen/4 samples == tlen/4*1000 millisamples have been
    // transmitted.
    uii->desync -= transfer->num_iso_packets * nsamps * 1000;

    int err = libusb_submit_transfer(transfer);
    if (err)
        g_warning("Cannot submit isochronous transfer, error = %s", libusb_error_name(err));
}

struct libusb_transfer *play_stuff_adaptive(struct cbox_usb_io_impl *uii, int index)
{
    struct libusb_transfer *t;
    int i, err;
    int packets = uii->iso_packets_omega;
    t = libusb_alloc_transfer(packets);
    int tsize = uii->sample_rate * 2 * uii->output_resolution / 1000;
    uint8_t *buf = (uint8_t *)calloc(1, uii->audio_output_pktsize);
    
    libusb_fill_iso_transfer(t, uii->handle_audiodev, uii->audio_output_endpoint, buf, tsize * packets, packets, play_callback_adaptive, uii, 20000);
    libusb_set_iso_packet_lengths(t, tsize);
    
    err = libusb_submit_transfer(t);
    if (!err)
        return t;
    libusb_free_transfer(t);
    return NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////

static int calc_packet_lengths(struct cbox_usb_io_impl *uii, struct libusb_transfer *t, int packets)
{
    int tsize = 0;
    int i;
    // printf("sync_freq = %d\n", sync_freq);
    for (i = 0; i < packets; i++)
    {
        int nsamps = (uii->sync_freq - uii->desync) / 80;
        // assert(nsamps > 0);
        if ((uii->sync_freq - uii->desync) % 80)
            nsamps++;
        //printf("%d sfreq=%d desync=%d nsamps=%d\n", i, uii->sync_freq, uii->desync, nsamps);
        
        uii->desync = (uii->desync + nsamps * 80) % uii->sync_freq;
        int v = (nsamps) * 2 * uii->output_resolution;
        t->iso_packet_desc[i].length = v;
        tsize += v;
    }
    return tsize;
}

void play_callback_asynchronous(struct libusb_transfer *transfer)
{
    int i;
    struct cbox_usb_io_impl *uii = transfer->user_data;

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
    {
        uii->cancel_confirm = 1;
        return;
    }
    if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE)
    {
        uii->device_removed++;
        return;
    }

    gboolean init_finished = uii->playback_counter == uii->playback_buffers;
    if (uii->playback_counter < uii->playback_buffers)
    {
        // send another URB for the next transfer before re-submitting
        // this one
        uii->playback_transfers[uii->playback_counter] = (*uii->play_function)(uii, uii->playback_counter);
        uii->playback_counter++;
    }

    /*
    printf("Play Callback! %d status %d\n", (int)transfer->length, (int)transfer->status);
    for (i = 0; i < transfer->num_iso_packets; i++) {
        if (transfer->iso_packet_desc[i].actual_length)
        {
            printf("%d: %d %d\n", i, transfer->iso_packet_desc[i].actual_length, transfer->iso_packet_desc[i].status);
        }
    }
    */
    transfer->length = calc_packet_lengths(uii, transfer, transfer->num_iso_packets);
    if (init_finished)
    {
        fill_playback_buffer(uii, transfer);
    }
    int err = libusb_submit_transfer(transfer);
    if (err)
        g_warning("Cannot submit isochronous transfer, error = %s", libusb_error_name(err));
}

static struct libusb_transfer *sync_stuff_asynchronous(struct cbox_usb_io_impl *uii, int index);

struct libusb_transfer *play_stuff_asynchronous(struct cbox_usb_io_impl *uii, int index)
{
    struct libusb_transfer *t;
    int err;
    int packets = uii->iso_packets_multimix;
    t = libusb_alloc_transfer(packets);
    int tsize = calc_packet_lengths(uii, t, packets);
    int bufsize = uii->audio_output_pktsize * packets;
    uint8_t *buf = (uint8_t *)calloc(1, bufsize);
    
    if (!index)
    {
        for(uii->sync_counter = 0; uii->sync_counter < uii->sync_buffers; uii->sync_counter++)
            uii->sync_transfers[uii->sync_counter] = sync_stuff_asynchronous(uii, uii->sync_counter);
    }
    
    libusb_fill_iso_transfer(t, uii->handle_audiodev, uii->audio_output_endpoint, buf, tsize, packets, play_callback_asynchronous, uii, 20000);
    err = libusb_submit_transfer(t);
    if (err)
    {
        g_warning("Cannot submit playback urb: %s, error = %s (index = %d, tsize = %d)", libusb_error_name(err), strerror(errno), index, tsize);
        libusb_free_transfer(t);
        return NULL;
    }
    return t;
}

//////////////////////////////////////////////////////////////////////////////////////////

/*
 * The Multimix device controls the data rate of the playback stream using a
 * device-to-host isochronous endpoint. The incoming packets consist of 3 bytes:
 * a current value of sample rate (kHz) + 2 historical values. I'm only using
 * the first byte, I haven't yet encountered a situation where using the
 * second and third byte would be necessary. This seems to work for all
 * sample rates supported by the Windows driver - 44100, 48000, 88200 and
 * 96000. It is possible to set sample rate to 64000, but it doesn't work
 * correctly, and isn't supported by the Windows driver either - it may
 * require special handling or may be a half-implemented feature in hardware.
 * The isochronous transfer using 10 packets seems to give acceptable resolution
 * and latency to avoid over/underruns with supported sample rates.
 *
 * The non-integer multiples of 1 kHz (like 44.1) are passed as a sequence of
 * values that average to a desired value (9 values of 44 and one value of 45).
 *
 * In order to compensate for clock rate difference
 * between host clock and DAC clock, the sample rate values sent by the device
 * are either larger (to increase data rate from the host) or smaller than
 * the nominal frequency value - the driver uses that to adjust the sample frame
 * counts of individual packets in an isochronous transfer.
 */

static void sync_callback(struct libusb_transfer *transfer)
{
    struct cbox_usb_io_impl *uii = transfer->user_data;
    uint8_t *data = transfer->buffer;
    int i, j, ofs, size, pkts;

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
    {
        uii->cancel_confirm = 1;
        return;
    }
    // XXXKF handle device disconnected error

    if (uii->debug_sync)
        printf("Sync callback! %p %d packets:", transfer, transfer->num_iso_packets);
    ofs = 0;
    size = 0;
    pkts = 0;
    for (i = 0; i < transfer->num_iso_packets; i++) {
        if (transfer->iso_packet_desc[i].status)
        {
            printf("[%02d: actual length is %4d, status is %2d] ", i, transfer->iso_packet_desc[i].actual_length, transfer->iso_packet_desc[i].status);
            continue;
        }
        else if (transfer->iso_packet_desc[i].actual_length)
        {
            size += data[ofs];
            if (i)
                assert(data[ofs + 1] == data[ofs - 64]);
            //printf("%d\n", (int)data[ofs]);
            if (uii->debug_sync)
                printf("%3d ", (int)data[ofs]);
            pkts++;
        }
        else
        if (uii->debug_sync)
            printf("? ");
        ofs += transfer->iso_packet_desc[i].length;
    }
    if (uii->debug_sync)
        printf(" (%d of %d)", pkts, transfer->num_iso_packets);
    if (pkts == transfer->num_iso_packets)
    {
        uii->sync_freq = size * 10 / pkts;
        if (uii->debug_sync)
            printf(" size = %4d sync_freq = %4d", size, uii->sync_freq);
    }
    libusb_submit_transfer(transfer);
    if (uii->debug_sync)
        printf("\n");
}

struct libusb_transfer *sync_stuff_asynchronous(struct cbox_usb_io_impl *uii, int index)
{
    struct libusb_transfer *t;
    int err;
    int syncbufsize = 64;
    int syncbufcount = NUM_SYNC_PACKETS;
    t = libusb_alloc_transfer(syncbufcount);
    uint8_t *sync_buf = (uint8_t *)calloc(syncbufcount, syncbufsize);
    libusb_fill_iso_transfer(t, uii->handle_audiodev, uii->audio_sync_endpoint, sync_buf, syncbufsize * syncbufcount, syncbufcount, sync_callback, uii, 20000);
    libusb_set_iso_packet_lengths(t, syncbufsize);
    
    err = libusb_submit_transfer(t);
    if (err)
    {
        g_warning("Cannot submit sync urb: %s", libusb_error_name(err));
        libusb_free_transfer(t);
        return NULL;
    }
    return t;
}

static void midi_transfer_cb(struct libusb_transfer *transfer)
{
    struct cbox_usb_midi_input *umi = transfer->user_data;

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
    {
        umi->uii->cancel_confirm = 1;
        return;
    }
    if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT || transfer->status == LIBUSB_TRANSFER_ERROR || transfer->status == LIBUSB_TRANSFER_STALL)
    {
        libusb_submit_transfer(transfer);
        return;
    }
    if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE)
    {
        g_debug("No device %03d:%03d, unlinking", umi->busdevadr >> 8, umi->busdevadr & 255);
        umi->uii->rt_midi_input_ports = g_list_remove(umi->uii->rt_midi_input_ports, umi);
        return;
    }
    if (transfer->status != 0)
        g_warning("USB error on device %03d:%03d: transfer status %d", umi->busdevadr >> 8, umi->busdevadr & 255, transfer->status);
    for (int i = 0; i + 3 < transfer->actual_length; i += 4)
    {
        uint8_t *data = &transfer->buffer[i];
        if ((data[0] & 15) >= 0x08)
        {
            // normalise: note on with vel 0 -> note off
            if ((data[1] & 0xF0) == 0x90 && data[3] == 0)
                cbox_midi_buffer_write_inline(&umi->midi_buffer, 0, data[1] - 0x10, data[2], data[3]);
            else
                cbox_midi_buffer_write_event(&umi->midi_buffer, 0, data + 1, midi_cmd_size(data[1]));
        }
    }
    libusb_submit_transfer(transfer);
}

static void start_midi_capture(struct cbox_usb_io_impl *uii)
{
    uii->rt_midi_input_ports = g_list_copy(uii->midi_input_ports);
    uii->midi_input_port_count = 0;
    uii->midi_input_port_buffers = calloc(uii->midi_input_port_count, sizeof(struct cbox_midi_buffer *));
    uii->midi_input_port_pos = calloc(uii->midi_input_port_count, sizeof(int));

    for(GList *p = uii->rt_midi_input_ports; p; p = p->next)
    {
        struct cbox_usb_midi_input *umi = p->data;
        cbox_midi_buffer_clear(&umi->midi_buffer);
        umi->transfer = libusb_alloc_transfer(0);
        libusb_fill_bulk_transfer(umi->transfer, umi->handle, umi->endpoint, umi->midi_recv_data, umi->max_packet_size, midi_transfer_cb, umi, 1000);
        uii->midi_input_port_buffers[uii->midi_input_port_count] = &umi->midi_buffer;
        uii->midi_input_port_count++;
    }
    for(GList *p = uii->rt_midi_input_ports; p; p = p->next)
    {
        struct cbox_usb_midi_input *umi = p->data;
        int res = libusb_submit_transfer(umi->transfer);
    }
}

static void stop_midi_capture(struct cbox_usb_io_impl *uii)
{
    for(GList *p = uii->rt_midi_input_ports; p; p = p->next)
    {
        struct cbox_usb_midi_input *umi = p->data;

        uii->cancel_confirm = FALSE;
        libusb_cancel_transfer(umi->transfer);
        while (!uii->cancel_confirm)
            libusb_handle_events(uii->usbctx);
        libusb_free_transfer(umi->transfer);
        umi->transfer = NULL;
        cbox_midi_buffer_clear(&umi->midi_buffer);
    }
    free(uii->midi_input_port_buffers);
    free(uii->midi_input_port_pos);
    g_list_free(uii->rt_midi_input_ports);
}

static void start_audio_playback(struct cbox_usb_io_impl *uii)
{
    uii->desync = 0;
    uii->samples_played = 0;
    uii->read_ptr = uii->ioi.pio->buffer_size;
    
    uii->playback_transfers = malloc(sizeof(struct libusb_transfer *) * uii->playback_buffers);
    uii->sync_transfers = malloc(sizeof(struct libusb_transfer *) * uii->sync_buffers);
    
    uii->playback_counter = 1;
    uii->device_removed = 0;
    uii->sync_freq = uii->sample_rate / 100;
    uii->playback_transfers[0] = uii->play_function(uii, 0);
    uii->setup_error = uii->playback_transfers[0] == NULL;

    if (!uii->setup_error)
    {
        while(uii->playback_counter < uii->playback_buffers && !uii->device_removed)
            libusb_handle_events(uii->usbctx);
    }
}

static void stop_audio_playback(struct cbox_usb_io_impl *uii)
{
    if (uii->device_removed)
    {
        // Wait until all the transfers pending are finished
        while(uii->device_removed < uii->playback_counter)
            libusb_handle_events(uii->usbctx);
    }
    if (uii->device_removed || uii->setup_error)
    {
        // Run the DSP code and send output to bit bucket until engine is stopped.
        // This ensures that the command queue will still be processed.
        // Otherwise the GUI thread would hang waiting for the command from
        // the queue to be completed.
        g_message("USB Audio output device has been disconnected - switching to null output.");
        run_idle_loop(uii);            
    }
    else
    {
        // Cancel all transfers pending, and wait until they get cancelled
        for (int i = 0; i < uii->playback_buffers; i++)
        {
            if (uii->playback_transfers[i]->status != LIBUSB_TRANSFER_NO_DEVICE)
            {
                uii->cancel_confirm = FALSE;
                libusb_cancel_transfer(uii->playback_transfers[i]);
                while (!uii->cancel_confirm && uii->playback_transfers[i]->status != LIBUSB_TRANSFER_NO_DEVICE)
                    libusb_handle_events(uii->usbctx);
            }
        }
    }
    // Free the transfers for the buffers allocated so far. In case of setup
    // failure, some buffers transfers might not have been created yet.
    for (int i = 0; i < uii->playback_counter; i++)
        libusb_free_transfer(uii->playback_transfers[i]);
    if (uii->playback_counter && uii->audio_sync_endpoint)
    {
        for (int i = 0; i < uii->sync_counter; i++)
        {
            uii->cancel_confirm = FALSE;
            libusb_cancel_transfer(uii->sync_transfers[i]);
            while (!uii->cancel_confirm && uii->sync_transfers[i]->status != LIBUSB_TRANSFER_NO_DEVICE)
                libusb_handle_events(uii->usbctx);
        }
        for (int i = 0; i < uii->sync_counter; i++)
            libusb_free_transfer(uii->sync_transfers[i]);
    }
}

static void run_audio_loop(struct cbox_usb_io_impl *uii)
{
    while(!uii->stop_engine && !uii->device_removed) {
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 1000
        };
        libusb_handle_events_timeout(uii->usbctx, &tv);
    }
}

static void run_idle_loop(struct cbox_usb_io_impl *uii)
{
    while(!uii->stop_engine)
    {
        struct cbox_io *io = uii->ioi.pio;
        for (int b = 0; b < uii->output_channels; b++)
            memset(io->output_buffers[b], 0, io->buffer_size * sizeof(float));
        io->cb->process(io->cb->user_data, io, io->buffer_size);
        for (GList *p = uii->rt_midi_input_ports; p; p = p->next)
        {
            struct cbox_usb_midi_input *umi = p->data;
            cbox_midi_buffer_clear(&umi->midi_buffer);
        }
        
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 1
        };
        libusb_handle_events_timeout(uii->usbctx, &tv);
        usleep((int)(io->buffer_size * 1000000.0 / uii->sample_rate));
    }
}

static void *engine_thread(void *user_data)
{
    struct cbox_usb_io_impl *uii = user_data;
    
    start_midi_capture(uii);

    struct sched_param p;
    memset(&p, 0, sizeof(p));
    p.sched_priority = 10;
    pid_t tid = syscall(SYS_gettid);
    if (0 != sched_setscheduler(tid, SCHED_FIFO, &p))
        g_warning("Cannot set realtime priority for the processing thread: %s.", strerror(errno));
    
    if (uii->handle_audiodev)
    {
        start_audio_playback(uii);
        if (!uii->setup_error)
        {
            run_audio_loop(uii);        
        }
        stop_audio_playback(uii);
    }
    else
    {
        g_message("No audio device found - running idle loop.");
        // notify the UI thread that the (fake) audio loop is running
        uii->playback_counter = uii->playback_buffers;
        run_idle_loop(uii);
    }
    
    stop_midi_capture(uii);
}

gboolean cbox_usbio_start(struct cbox_io_impl *impl, GError **error)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;

    // XXXKF: needs to queue the playback and capture transfers

    uii->stop_engine = FALSE;
    uii->setup_error = FALSE;
    uii->playback_counter = 0;
    
    if (pthread_create(&uii->thr_engine, NULL, engine_thread, uii))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "cannot create engine thread: %s", strerror(errno));
        return FALSE;
    }
    while(!uii->setup_error && uii->playback_counter < uii->playback_buffers)
        usleep(10000);

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

void cbox_usbio_poll_ports(struct cbox_io_impl *impl)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;

    // Dry run, just to detect if anything changed
    if (scan_devices(uii, TRUE))
    {
        g_debug("Restarting I/O due to device being connected or disconnected");
        cbox_usbio_stop(&uii->ioi, NULL);
        // Re-scan, this time actually create the MIDI inputs
        scan_devices(uii, FALSE);
        cbox_usbio_start(&uii->ioi, NULL);
    }
}

gboolean cbox_usbio_cycle(struct cbox_io_impl *impl, GError **error)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;
    // XXXKF: this is for restarting the thing; not implemented for now,
    // the implementation will be something like in case of JACK - close and
    // reopen.
    return TRUE;
}

int cbox_usbio_get_midi_data(struct cbox_io_impl *impl, struct cbox_midi_buffer *destination)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)impl;

    cbox_midi_buffer_clear(destination);
    memset(uii->midi_input_port_pos, 0, sizeof(int) * uii->midi_input_port_count);

    cbox_midi_buffer_merge(destination, uii->midi_input_port_buffers, uii->midi_input_port_count, uii->midi_input_port_pos);
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
            forget_device(uii, udi);
    }
    g_list_free(prev_keys);
    g_hash_table_destroy(uii->device_table);
    
    libusb_exit(uii->usbctx_probe);
    libusb_exit(uii->usbctx);
}

///////////////////////////////////////////////////////////////////////////////

static gboolean set_endpoint_sample_rate(struct libusb_device_handle *h, int sample_rate, int playback_ep)
{
    uint8_t freq_data[3];
    freq_data[0] = sample_rate & 0xFF;
    freq_data[1] = (sample_rate & 0xFF00) >> 8;
    freq_data[2] = (sample_rate & 0xFF0000) >> 16;
    if (libusb_control_transfer(h, 0x22, 0x01, 256, playback_ep, freq_data, 3, OMEGA_TIMEOUT) != 3)
        return FALSE;
//    if (libusb_control_transfer(h, 0x22, 0x01, 256, OMEGA_EP_CAPTURE, freq_data, 3, OMEGA_TIMEOUT) != 3)
//        return FALSE;
    return TRUE;
}

static gboolean configure_usb_interface(struct libusb_device_handle *handle, int bus, int devadr, int ifno, int altset, GError **error)
{
    int err = 0;
    if (libusb_kernel_driver_active(handle, ifno))
    {
        err = libusb_detach_kernel_driver(handle, ifno);
        if (err)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot detach kernel driver from device %d: %s. Please rmmod snd-usb-audio as root.", ifno, libusb_error_name(err));
            return FALSE;
        }            
    }
    err = libusb_claim_interface(handle, ifno);
    if (err)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot claim interface %d on device %03d:%03d for MIDI input: %s", ifno, bus, devadr, libusb_error_name(err));
        return FALSE;
    }
    err = libusb_set_interface_alt_setting(handle, ifno, altset);
    if (err)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set alt-setting %d for interface %d on device %03d:%03d for MIDI input: %s", altset, ifno, bus, devadr, libusb_error_name(err));
        return FALSE;
    }
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

static gboolean claim_omega_interfaces(struct cbox_usb_io_impl *uii, struct libusb_device_handle *handle, int bus, int devadr, GError **error)
{
    static int interfaces[] = { 1, 2 };
    int altsets[] = { uii->output_resolution == 3 ? 2 : 1, 1 };
    for (int i = 0; i < sizeof(interfaces) / sizeof(int); i++)
    {
        int ifno = interfaces[i];
        if (!configure_usb_interface(handle, bus, devadr, ifno, altsets[i], error))
            return FALSE;
    }
    return TRUE;
}

static gboolean open_omega(struct cbox_usb_io_impl *uii, int bus, int devadr, struct libusb_device_handle *handle, GError **error)
{
    if (uii->output_resolution != 2 && uii->output_resolution != 3)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Only 16-bit or 24-bit output resolution is supported.");
        return FALSE;
    }
    if (!claim_omega_interfaces(uii, handle, bus, devadr, error))
        return FALSE;
    if (!set_endpoint_sample_rate(handle, uii->sample_rate, OMEGA_EP_PLAYBACK))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set sample rate on Lexicon Omega.");
        return FALSE;
    }
    uii->play_function = play_stuff_adaptive;
    uii->handle_audiodev = handle;
    uii->audio_output_endpoint = OMEGA_EP_PLAYBACK;
    uii->audio_output_pktsize = 48 * 2 * uii->output_resolution;
    uii->audio_sync_endpoint = 0;
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

static gboolean claim_multimix_interfaces(struct cbox_usb_io_impl *uii, struct libusb_device_handle *handle, int bus, int devadr, GError **error)
{
    static int interfaces[] = { 0, 1 };
    for (int i = 0; i < sizeof(interfaces) / sizeof(int); i++)
    {
        int ifno = interfaces[i];
        if (!configure_usb_interface(handle, bus, devadr, ifno, 1, error))
            return FALSE;
    }
    return TRUE;
}

static gboolean open_multimix(struct cbox_usb_io_impl *uii, int bus, int devadr, struct libusb_device_handle *handle, GError **error)
{
    if (uii->output_resolution != 3)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Only 24-bit output resolution is supported.");
        return FALSE;
    }
    if (!claim_multimix_interfaces(uii, handle, bus, devadr, error))
        return FALSE;
#if 0
    uint8_t res_config = uii->output_resolution == 3 ? 0x30 : 0x50;
    // I wasn't able to find out what those URBs do. The 0x49 seems to affect the
    // input bit rate (16-bit or 24-bit) via setting bit 5, but for other bits,
    // I don't know if anything is using them.
    // The 0x41 one - no clue at all, I just probed it using the loop below.
    res_config = 0x0;
    if (libusb_control_transfer(handle, 0x40, 0x41, res_config, 0, NULL, 0, 20000) != 0)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set resolution on Alesis Multimix.");
        return FALSE;
    }
    res_config = 0x20;
    if (libusb_control_transfer(handle, 0x40, 0x49, res_config, 0, NULL, 0, 20000) != 0)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set resolution on Alesis Multimix.");
        return FALSE;
    }
    for (int i = 0; i < 255; i++)
    {
        uint8_t bufsize = 0;
        if (libusb_control_transfer(handle, 0xC0, i, 0, 0, &bufsize, 1, 1000) == 1)
            printf("Cmd %d value = %d\n", i, (int)bufsize);
    }
#endif
    if (!set_endpoint_sample_rate(handle, uii->sample_rate, MULTIMIX_EP_PLAYBACK))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set sample rate on Alesis Multimix.");
        return FALSE;
    }

    uii->play_function = play_stuff_asynchronous;
    uii->handle_audiodev = handle;
    uii->audio_output_endpoint = MULTIMIX_EP_PLAYBACK;
    uii->audio_output_pktsize = 156;
    uii->audio_sync_endpoint = MULTIMIX_EP_SYNC;
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

static void forget_device(struct cbox_usb_io_impl *uii, struct cbox_usb_device_info *devinfo)
{
    g_hash_table_remove(uii->device_table, GINT_TO_POINTER(devinfo->busdevadr));
    for (GList *p = uii->midi_input_ports, *pNext = NULL; p; p = pNext)
    {
        pNext = p->next;
        struct cbox_usb_midi_input *umi = p->data;
        if (umi->busdevadr == devinfo->busdevadr)
        {
            uii->midi_input_ports = g_list_delete_link(uii->midi_input_ports, p);
            free(umi);
        }
    }
    if (uii->handle_audiodev == devinfo->handle)
        uii->handle_audiodev = NULL;
    libusb_close(devinfo->handle);
    free(devinfo);
}

static const struct libusb_endpoint_descriptor *get_midi_input_endpoint(const struct libusb_interface_descriptor *asdescr)
{
    for (int epi = 0; epi < asdescr->bNumEndpoints; epi++)
    {
        const struct libusb_endpoint_descriptor *ep = &asdescr->endpoint[epi];
        if (ep->bEndpointAddress >= 0x80)
            return ep;
    }
    return NULL;
}

static struct cbox_usb_midi_input *open_midi_interface(struct cbox_usb_io_impl *uii, struct cbox_usb_device_info *devinfo, struct libusb_device_handle *handle, int ifno, int altset, const struct libusb_endpoint_descriptor *ep)
{
    int bus = devinfo->bus;
    int devadr = devinfo->devadr;
    GError *error = NULL;
    // printf("Has MIDI port\n");
    // printf("Output endpoint address = %02x\n", ep->bEndpointAddress);
    if (!configure_usb_interface(handle, bus, devadr, ifno, altset, &error))
    {
        g_warning("%s", error->message);
        g_error_free(error);
        return NULL;
    }
    
    struct cbox_usb_midi_input *umi = malloc(sizeof(struct cbox_usb_midi_input));
    umi->uii = uii;
    umi->handle = handle;
    umi->busdevadr = devinfo->busdevadr;
    umi->endpoint = ep->bEndpointAddress;
    cbox_midi_buffer_init(&umi->midi_buffer);
    uii->midi_input_ports = g_list_prepend(uii->midi_input_ports, umi);
    int len = ep->wMaxPacketSize;
    if (len > 256)
        len = 256;
    umi->max_packet_size = len;
    
    // Drain the output buffer of the device - otherwise playing a few notes and running the program will cause
    // those notes to play.
    char flushbuf[256];
    int transferred = 0;
    while(0 == libusb_bulk_transfer(handle, umi->endpoint, flushbuf, umi->max_packet_size, &transferred, 10) && transferred > 0)
        usleep(1000);
    devinfo->status = CBOX_DEVICE_STATUS_OPENED;
    
    return umi;
error:
    return NULL;
}

static gboolean inspect_device(struct cbox_usb_io_impl *uii, struct libusb_device *dev, uint16_t busdevadr, gboolean probe_only)
{
    struct libusb_device_descriptor dev_descr;
    int bus = busdevadr >> 8;
    int devadr = busdevadr & 255;
    
    if (0 != libusb_get_device_descriptor(dev, &dev_descr))
    {
        g_warning("USB device %03d:%03d - cannot get device descriptor (will retry)", bus, devadr);
        return FALSE;
    }
    
    struct libusb_config_descriptor *cfg_descr = NULL;
    struct cbox_usb_device_info *udi = g_hash_table_lookup(uii->device_table, GINT_TO_POINTER(busdevadr));
    if (!udi)
    {
        if (0 != libusb_get_active_config_descriptor(dev, &cfg_descr))
            return FALSE;
        udi = malloc(sizeof(struct cbox_usb_device_info));
        udi->dev = dev;
        udi->handle = NULL;
        udi->status = CBOX_DEVICE_STATUS_PROBING;
        udi->active_config = cfg_descr->bConfigurationValue;
        udi->bus = bus;
        udi->devadr = devadr;
        udi->busdevadr = busdevadr;
        udi->vid = dev_descr.idVendor;
        udi->pid = dev_descr.idProduct;
        udi->configs_with_midi = 0;
        udi->configs_with_audio = 0;
        udi->is_midi = FALSE;
        udi->is_audio = FALSE;
        udi->last_probe_time = time(NULL);
        udi->failures = 0;
        g_hash_table_insert(uii->device_table, GINT_TO_POINTER(busdevadr), udi);
    }
    else
    if (udi->vid == dev_descr.idVendor && udi->pid == dev_descr.idProduct)
    {
        // device already open or determined to be
        if (udi->status == CBOX_DEVICE_STATUS_OPENED ||
            udi->status == CBOX_DEVICE_STATUS_UNSUPPORTED)
            return FALSE;
        // give up after 10 attempts to query or open the device
        if (udi->failures > 10)
            return FALSE;
        // only do ~1 attempt per second
        if (probe_only && time(NULL) == udi->last_probe_time)
            return FALSE;
        udi->last_probe_time = time(NULL);
    }

    int intf_midi_in = -1, as_midi_in = -1;
    const struct libusb_endpoint_descriptor *ep_midi_in = NULL;
    int active_config = 0, alt_config = -1;
    gboolean is_midi = FALSE, is_audio = FALSE;

    // printf("%03d:%03d Device %04X:%04X\n", bus, devadr, dev_descr.idVendor, dev_descr.idProduct);
    for (int ci = 0; ci < (int)dev_descr.bNumConfigurations; ci++)
    {
        // if this is not the current config, and another config with MIDI input
        // has already been found, do not look any further
        if (0 != libusb_get_config_descriptor(dev, ci, &cfg_descr))
        {
            udi->failures++;
            g_warning("%03d:%03d - cannot get configuration descriptor (try %d)", bus, devadr, udi->failures);
            return FALSE;
        }
            
        int cur_config = cfg_descr->bConfigurationValue;
        uint32_t config_mask = 0;
        // XXXKF not sure about legal range for bConfigurationValue
        if(cfg_descr->bConfigurationValue >= 0 && cfg_descr->bConfigurationValue < 32)
            config_mask = 1 << cfg_descr->bConfigurationValue;
        else
            g_warning("Unexpected configuration value %d", cfg_descr->bConfigurationValue);
        
        for (int ii = 0; ii < cfg_descr->bNumInterfaces; ii++)
        {
            const struct libusb_interface *idescr = &cfg_descr->interface[ii];
            for (int as = 0; as < idescr->num_altsetting; as++)
            {
                const struct libusb_interface_descriptor *asdescr = &idescr->altsetting[as];
                if (asdescr->bInterfaceClass == LIBUSB_CLASS_AUDIO && asdescr->bInterfaceSubClass == 3)
                {
                    const struct libusb_endpoint_descriptor *ep = get_midi_input_endpoint(asdescr);
                    if (!ep)
                        continue;
                    
                    if (cur_config != udi->active_config)
                    {
                        udi->configs_with_midi |= config_mask;
                        continue;
                    }
                    
                    if (!ep_midi_in)
                    {
                        intf_midi_in = asdescr->bInterfaceNumber;
                        as_midi_in = asdescr->bAlternateSetting;
                        ep_midi_in = ep;
                    }
                }
            }
        }
    }
    if (!ep_midi_in && udi->configs_with_midi)
        g_warning("%03d:%03d - MIDI port available on different configs: mask=0x%x", bus, devadr, udi->configs_with_midi);

    if (udi->vid == 0x1210 && udi->pid == 0x0002) // Lexicon Omega
        is_audio = TRUE;
    if (udi->vid == 0x13b2 && udi->pid == 0x0030) // Alesis Multimix 8
        is_audio = TRUE;
    
    // All configs/interfaces/alts scanned, nothing interesting found -> mark as unsupported
    udi->is_midi = ep_midi_in != NULL;
    udi->is_audio = is_audio;
    if (!udi->is_midi && !udi->is_audio)
    {
        udi->status = CBOX_DEVICE_STATUS_UNSUPPORTED;
        return FALSE;
    }
    
    gboolean opened = FALSE;
    struct libusb_device_handle *handle = NULL;
    int err = libusb_open(dev, &handle);
    if (0 != err)
    {
        g_warning("Cannot open device %03d:%03d: %s; errno = %s", bus, devadr, libusb_error_name(err), strerror(errno));
        udi->failures++;
        return FALSE;
    }
    
    if (probe_only)
    {
        libusb_close(handle);
        // Make sure that the reconnection code doesn't bail out due to
        // last_probe_time == now.
        udi->last_probe_time = 0;
        return udi->is_midi || udi->is_audio;
    }
    
    if (ep_midi_in)
    {
        g_debug("Found MIDI device %03d:%03d, trying to open", bus, devadr);
        if (0 != open_midi_interface(uii, udi, handle, intf_midi_in, as_midi_in, ep_midi_in))
            opened = TRUE;
    }
    if (udi->vid == 0x1210 && udi->pid == 0x0002)
    {
        GError *error = NULL;
        if (open_omega(uii, bus, devadr, handle, &error))
        {
            // should have already been marked as opened by the MIDI code, but
            // I might add the ability to disable some MIDI interfaces at some point
            udi->status = CBOX_DEVICE_STATUS_OPENED;
            opened = TRUE;
        }
        else
        {
            g_warning("Cannot open Lexicon Omega audio output: %s", error->message);
            g_error_free(error);
        }
    }
    if (udi->vid == 0x13b2 && udi->pid == 0x0030)
    {
        GError *error = NULL;
        if (open_multimix(uii, bus, devadr, handle, &error))
        {
            // should have already been marked as opened by the MIDI code, but
            // I might add the ability to disable some MIDI interfaces at some point
            udi->status = CBOX_DEVICE_STATUS_OPENED;
            opened = TRUE;
        }
        else
        {
            g_warning("Cannot open Alesis Multimix audio output: %s", error->message);
            g_error_free(error);
        }
    }
    
    if (!opened)
    {
        udi->failures++;
        libusb_close(handle);
    }
    else
        udi->handle = handle;
    
    return opened;
}

static gboolean scan_devices(struct cbox_usb_io_impl *uii, gboolean probe_only)
{
    struct libusb_device **dev_list;
    size_t i, num_devices;
    gboolean added = FALSE;
    gboolean removed = FALSE;
    
    num_devices = libusb_get_device_list(probe_only ? uii->usbctx_probe : uii->usbctx, &dev_list);

    uint16_t *busdevadrs = malloc(sizeof(uint16_t) * num_devices);
    for (i = 0; i < num_devices; i++)
    {
        struct libusb_device *dev = dev_list[i];
        int bus = libusb_get_bus_number(dev);
        int devadr = libusb_get_device_address(dev);
        busdevadrs[i] = (bus << 8) | devadr;
    }
    
    GList *prev_keys = g_hash_table_get_values(uii->device_table);
    for (GList *p = prev_keys; p; p = p->next)
    {
        gboolean found = FALSE;
        struct cbox_usb_device_info *udi = p->data;
        for (i = 0; !found && i < num_devices; i++)
            found = busdevadrs[i] == udi->busdevadr;
        if (!found)
        {
            // Only specifically trigger removal if the device is ours
            if (udi->status == CBOX_DEVICE_STATUS_OPENED)
            {
                g_message("Disconnected: %03d:%03d (%s)", udi->bus, udi->devadr, probe_only ? "probe" : "reconfigure");
                removed = TRUE;
            }
            if (!probe_only)
                forget_device(uii, udi);
        }
    }
    g_list_free(prev_keys);
    
    for (i = 0; i < num_devices; i++)
        added = inspect_device(uii, dev_list[i], busdevadrs[i], probe_only) || added;
    
    free(busdevadrs);
    libusb_free_device_list(dev_list, 0);
    return added || removed;
}

///////////////////////////////////////////////////////////////////////////////

gboolean cbox_io_init_usb(struct cbox_io *io, struct cbox_open_params *const params, GError **error)
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
    uii->iso_packets_omega = cbox_config_get_int(cbox_io_section, "iso_packets_omega", 1);
    // The USB 2.0 device uses a higher packet rate (125us I think), so the
    // default number of packets per transfer needs to be different, too -
    // 1ms is a minimum reasonable value
    uii->iso_packets_multimix = cbox_config_get_int(cbox_io_section, "iso_packets_multimix", 16);
    uii->output_resolution = cbox_config_get_int(cbox_io_section, "output_resolution", 16) / 8;
    uii->output_channels = 2;
    uii->handle_audiodev = NULL;
    
    // fixed processing buffer size, as we have to deal with packetisation anyway
    io->buffer_size = 64;
    io->cb = NULL;
    // input and output count is hardcoded for simplicity - in future, it may be
    // necessary to add support for the extra inputs (needs to be figured out)
    io->input_count = 2; //cbox_config_get_int("io", "inputs", 0);
    io->input_buffers = malloc(sizeof(float *) * io->input_count);
    for (int i = 0; i < io->input_count; i++)
        io->input_buffers[i] = calloc(io->buffer_size, sizeof(float));
    io->output_count = 2; // cbox_config_get_int("io", "outputs", 2);
    io->output_buffers = malloc(sizeof(float *) * io->output_count);
    for (int i = 0; i < io->output_count; i++)
        io->output_buffers[i] = calloc(io->buffer_size, sizeof(float));
    io->impl = &uii->ioi;

    uii->ioi.pio = io;
    uii->ioi.getsampleratefunc = cbox_usbio_get_sample_rate;
    uii->ioi.startfunc = cbox_usbio_start;
    uii->ioi.stopfunc = cbox_usbio_stop;
    uii->ioi.getstatusfunc = cbox_usbio_get_status;
    uii->ioi.pollfunc = cbox_usbio_poll_ports;
    uii->ioi.cyclefunc = cbox_usbio_cycle;
    uii->ioi.getmidifunc = cbox_usbio_get_midi_data;
    uii->ioi.destroyfunc = cbox_usbio_destroy;
    uii->midi_input_ports = NULL;
    
    scan_devices(uii, FALSE);

    return TRUE;
    
}

