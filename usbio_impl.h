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

Note:
Those are private structures and functions for the USB driver implementation.
I'm not too happy with the organization of the code yet, it's just a first
try at breaking it up into manageable parts.
*/


#ifndef USBIO_IMPL_H
#define USBIO_IMPL_H

#include <libusb.h>

#include "errors.h"
#include "io.h"
#include "midi.h"

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

struct cbox_usb_audio_info
{
    struct cbox_usb_device_info *udi;
    int intf;
    int alt_setting;
    const struct libusb_endpoint_descriptor *ep;
};

struct cbox_usb_midi_info
{
    struct cbox_usb_device_info *udi;
    int intf;
    int alt_setting;
    const struct libusb_endpoint_descriptor *ep;
};

struct cbox_usb_midi_input
{
    struct cbox_usb_io_impl *uii;
    struct cbox_usb_device_info *devinfo;
    struct libusb_device_handle *handle;
    int busdevadr;
    int endpoint;
    int max_packet_size;
    struct libusb_transfer *transfer;
    struct cbox_midi_buffer midi_buffer;
    uint8_t midi_recv_data[256];
};

extern void cbox_usb_midi_info_init(struct cbox_usb_midi_info *umi, struct cbox_usb_device_info *udi);
extern void usbio_start_midi_capture(struct cbox_usb_io_impl *uii);
extern void usbio_stop_midi_capture(struct cbox_usb_io_impl *uii);

extern void cbox_usb_audio_info_init(struct cbox_usb_audio_info *uai, struct cbox_usb_device_info *udi);
extern void usbio_start_audio_playback(struct cbox_usb_io_impl *uii);
extern void usbio_stop_audio_playback(struct cbox_usb_io_impl *uii);

extern struct libusb_transfer *usbio_play_buffer_adaptive(struct cbox_usb_io_impl *uii, int index);
extern struct libusb_transfer *usbio_play_buffer_asynchronous(struct cbox_usb_io_impl *uii, int index);
extern gboolean usbio_scan_devices(struct cbox_usb_io_impl *uii, gboolean probe_only);

extern void usbio_forget_device(struct cbox_usb_io_impl *uii, struct cbox_usb_device_info *devinfo);
extern struct cbox_usb_midi_input *usbio_open_midi_interface(struct cbox_usb_io_impl *uii,
    struct cbox_usb_device_info *devinfo, struct libusb_device_handle *handle,
    const struct cbox_usb_midi_info *uminf);
extern void usbio_run_idle_loop(struct cbox_usb_io_impl *uii);;

#define USB_DEVICE_SETUP_TIMEOUT 2000

static inline gboolean configure_usb_interface(struct libusb_device_handle *handle, int bus, int devadr, int ifno, int altset, GError **error)
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
    err = altset ? libusb_set_interface_alt_setting(handle, ifno, altset) : 0;
    if (err)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Cannot set alt-setting %d for interface %d on device %03d:%03d for MIDI input: %s", altset, ifno, bus, devadr, libusb_error_name(err));
        return FALSE;
    }
    return TRUE;
}


#endif
