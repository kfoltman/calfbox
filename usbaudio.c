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

#include "usbio_impl.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#define NUM_SYNC_PACKETS 10

#define MULTIMIX_EP_PLAYBACK 0x02
//#define MULTIMIX_EP_CAPTURE 0x86
#define MULTIMIX_EP_SYNC 0x81

static struct libusb_transfer *usbio_play_buffer_adaptive(struct cbox_usb_io_impl *uii, int index);

///////////////////////////////////////////////////////////////////////////////

static gboolean set_endpoint_sample_rate(struct libusb_device_handle *h, int sample_rate, int ep)
{
    uint8_t freq_data[3];
    freq_data[0] = sample_rate & 0xFF;
    freq_data[1] = (sample_rate & 0xFF00) >> 8;
    freq_data[2] = (sample_rate & 0xFF0000) >> 16;
    if (libusb_control_transfer(h, 0x22, 0x01, 256, ep, freq_data, 3, USB_DEVICE_SETUP_TIMEOUT) != 3)
        return FALSE;
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

gboolean usbio_open_audio_interface(struct cbox_usb_io_impl *uii, struct cbox_usb_audio_info *uainf, struct libusb_device_handle *handle, GError **error)
{
    if (uii->output_resolution != 2 && uii->output_resolution != 3)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Only 16-bit or 24-bit output resolution is supported.");
        return FALSE;
    }
    if (!configure_usb_interface(handle, uainf->udi->bus, uainf->udi->devadr, uainf->intf, uainf->alt_setting, error))
        return FALSE;
    if (!set_endpoint_sample_rate(handle, uii->sample_rate, uainf->ep->bEndpointAddress))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set sample rate on class-compliant USB audio device.");
        return FALSE;
    }
    uii->play_function = usbio_play_buffer_adaptive;
    uii->handle_audiodev = handle;
    uii->audio_output_endpoint = uainf->ep->bEndpointAddress;
    uii->audio_output_pktsize = uainf->ep->wMaxPacketSize; // 48 * 2 * uii->output_resolution;
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

gboolean usbio_open_audio_interface_multimix(struct cbox_usb_io_impl *uii, int bus, int devadr, struct libusb_device_handle *handle, GError **error)
{
    if (uii->output_resolution != 3)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Only 24-bit output resolution is supported.");
        return FALSE;
    }
    if (!claim_multimix_interfaces(uii, handle, bus, devadr, error))
        return FALSE;
    if (!set_endpoint_sample_rate(handle, uii->sample_rate, MULTIMIX_EP_PLAYBACK))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set sample rate on Alesis Multimix.");
        return FALSE;
    }

    uii->play_function = usbio_play_buffer_asynchronous;
    uii->handle_audiodev = handle;
    uii->audio_output_endpoint = MULTIMIX_EP_PLAYBACK;
    uii->audio_output_pktsize = 156;
    uii->audio_sync_endpoint = MULTIMIX_EP_SYNC;
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

static void calc_output_buffer(struct cbox_usb_io_impl *uii)
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
}

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
            calc_output_buffer(uii);
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
        uii->playback_transfers[uii->playback_counter] = usbio_play_buffer_adaptive(uii, uii->playback_counter);
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
    {
        if (err == LIBUSB_ERROR_NO_DEVICE)
        {
            uii->device_removed++;
            transfer->status = LIBUSB_TRANSFER_NO_DEVICE;
        }
        g_warning("Cannot submit isochronous transfer, error = %s", libusb_error_name(err));
    }
}

struct libusb_transfer *usbio_play_buffer_adaptive(struct cbox_usb_io_impl *uii, int index)
{
    struct libusb_transfer *t;
    int i, err;
    int packets = uii->iso_packets;
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
        uii->playback_transfers[uii->playback_counter] = usbio_play_buffer_asynchronous(uii, uii->playback_counter);
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
    {
        if (err == LIBUSB_ERROR_NO_DEVICE)
        {
            transfer->status = LIBUSB_TRANSFER_NO_DEVICE;
            uii->device_removed++;
        }
        g_warning("Cannot submit isochronous transfer, error = %s", libusb_error_name(err));
    }
}

static struct libusb_transfer *sync_stuff_asynchronous(struct cbox_usb_io_impl *uii, int index);

struct libusb_transfer *usbio_play_buffer_asynchronous(struct cbox_usb_io_impl *uii, int index)
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
            assert(transfer->iso_packet_desc[i].actual_length == 3);
            size += data[ofs];
            if (i && transfer->iso_packet_desc[i - 1].actual_length)
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
    int err = libusb_submit_transfer(transfer);
    if (err)
    {
        if (err == LIBUSB_ERROR_NO_DEVICE)
            transfer->status = LIBUSB_TRANSFER_NO_DEVICE;
        g_warning("Cannot submit isochronous sync transfer, error = %s", libusb_error_name(err));
    }
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

///////////////////////////////////////////////////////////////////////////

void cbox_usb_audio_info_init(struct cbox_usb_audio_info *uai, struct cbox_usb_device_info *udi)
{
    uai->udi = udi;
    uai->intf = -1;
    uai->alt_setting = -1;
    uai->ep = NULL;
}

void usbio_start_audio_playback(struct cbox_usb_io_impl *uii)
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

void usbio_stop_audio_playback(struct cbox_usb_io_impl *uii)
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
        usbio_run_idle_loop(uii);            
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

