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

#include "app.h"
#include "config.h"
#include "config-api.h"
#include "errors.h"
#include "hwcfg.h"
#include "io.h"
#include "meter.h"
#include "midi.h"
#include "recsrc.h"
#include "usbio_impl.h"

#include <unistd.h>

static void decode_events_mpd16(struct cbox_usb_midi_interface *umi, struct libusb_transfer *transfer)
{
    for (int i = 0; i < transfer->actual_length;)
    {
        uint8_t *data = &transfer->buffer[i];
        uint8_t len = data[0] & 15;
        if (!len || i + len >= transfer->actual_length)
            break;
        cbox_midi_buffer_write_inline(&umi->input_port->hdr.buffer, 0, data[1], len > 1 ? data[2] : 0, len > 2 ? data[3] : 0);
        i += len + 1;
    }
}

static void decode_events_nocturn(struct cbox_usb_midi_interface *umi, struct libusb_transfer *transfer)
{
    uint8_t control = 0;
    for (int i = 0; i < transfer->actual_length;)
    {
        uint8_t *data = &transfer->buffer[i];
        if (*data >= 128)
        {
            control = *data;
            i++;
            continue;
        }
        if (control != 176 || i + 2 > transfer->actual_length)
        {
            g_warning("Unrecognized combination of control, data and length: %02x %02x %02x", control, data[0], transfer->actual_length - i);
            break;
        }
        cbox_midi_buffer_write_inline(&umi->input_port->hdr.buffer, 0, control, data[0], data[1]);
        i += 2;
    }
}

static void decode_events_class(struct cbox_usb_midi_interface *umi, struct libusb_transfer *transfer)
{
    for (int i = 0; i + 3 < transfer->actual_length; i += 4)
    {
        uint8_t *data = &transfer->buffer[i];
        uint8_t etype = data[0] & 15;
        if (etype >= 8)
        {
            // normalise: note on with vel 0 -> note off
            if ((data[1] & 0xF0) == 0x90 && data[3] == 0)
                cbox_midi_buffer_write_inline(&umi->input_port->hdr.buffer, 0, data[1] - 0x10, data[2], data[3]);
            else
                cbox_midi_buffer_write_event(&umi->input_port->hdr.buffer, 0, data + 1, midi_cmd_size(data[1]));
        }
        else
        if (etype == 2 || etype == 3)
        {
            cbox_midi_buffer_write_event(&umi->input_port->hdr.buffer, 0, data + 1, etype);
        }
        else
        if (etype == 4)
        {
            if (umi->current_sysex_length + 3 <= MAX_SYSEX_SIZE)
                memcpy(&umi->sysex_data[umi->current_sysex_length], data + 1, 3);
            umi->current_sysex_length += 3;
        }
        else
        if (etype >= 5 && etype <= 7)
        {
            int len = etype - 4;
            if (umi->current_sysex_length + len <= MAX_SYSEX_SIZE)
                memcpy(&umi->sysex_data[umi->current_sysex_length], data + 1, len);
            umi->current_sysex_length += len;
            if (umi->current_sysex_length <= MAX_SYSEX_SIZE)
                cbox_midi_buffer_write_event(&umi->input_port->hdr.buffer, 0, umi->sysex_data, umi->current_sysex_length);
            else
                g_warning("Dropping oversized SysEx: length %d", umi->current_sysex_length);
            umi->current_sysex_length = 0;
            
        }
        else
            g_warning("Unrecognized USB MIDI initiating byte %02x\n", data[0]);
    }
}

static void midi_transfer_cb(struct libusb_transfer *transfer)
{
    struct usbio_transfer *xf = transfer->user_data;
    struct cbox_usb_midi_interface *umi = xf->user_data;
    xf->pending = FALSE;

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
    {
        xf->cancel_confirm = 1;
        return;
    }
    if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT || transfer->status == LIBUSB_TRANSFER_ERROR || transfer->status == LIBUSB_TRANSFER_STALL)
    {
        if (transfer->status != LIBUSB_TRANSFER_TIMED_OUT)
            g_warning("USB error on device %03d:%03d: transfer status %d", umi->busdevadr >> 8, umi->busdevadr & 255, transfer->status);
        if (umi->uii->no_resubmit)
            return;
        int res = usbio_transfer_submit(xf);
        if (res != 0)
            g_warning("Error submitting URB to MIDI endpoint: error code %d", res);
        return;
    }
    if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE)
    {
        g_debug("No device %03d:%03d, unlinking", umi->busdevadr >> 8, umi->busdevadr & 255);
        umi->uii->rt_midi_ports = g_list_remove(umi->uii->rt_midi_ports, umi);
        return;
    }

    if (umi->protocol == USBMIDI_PROTOCOL_MPD16) // MPD16
        decode_events_mpd16(umi, transfer);
    else
    if (umi->protocol == USBMIDI_PROTOCOL_NOCTURN) // Nocturn
        decode_events_nocturn(umi, transfer);
    else
        decode_events_class(umi, transfer);

    if (umi->uii->no_resubmit)
        return;
    usbio_transfer_submit(xf);
}

static gboolean push_data_to_umo(struct cbox_usb_midi_output *umo, const uint8_t *pdata, uint32_t size, uint8_t first_byte)
{
    if (umo->endpoint_buffer_pos + 4 <= USB_MIDI_OUTPUT_QUEUE)
    {
        umo->endpoint_buffer[umo->endpoint_buffer_pos] = first_byte;
        memcpy(&umo->endpoint_buffer[umo->endpoint_buffer_pos + 1], pdata, size);
        umo->endpoint_buffer_pos += 4;
    }
    else
    {
        g_warning("Class MIDI buffer overflow.");    
        return FALSE;
    }
    return TRUE;
}

static void encode_events_class(struct cbox_usb_midi_output *umo)
{
    for (int i = 0; i < umo->hdr.buffer.count; i++)
    {
        const struct cbox_midi_event *event = cbox_midi_buffer_get_event(&umo->hdr.buffer, i);
        const uint8_t *pdata = cbox_midi_event_get_data(event);
        if (event->size <= 3)
        {
            if (!push_data_to_umo(umo, pdata, event->size, pdata[0] >> 4))
                return;
        }
        else
        {
            int i = 0;
            while(i + 3 < event->size)
            {
                push_data_to_umo(umo, pdata + i, 3, 4);
                i += 3;
            }
            push_data_to_umo(umo, pdata + i, 3, 4 + event->size - i);
        }
    }
}

static void encode_events_nocturn(struct cbox_usb_midi_output *umo)
{
    for (int i = 0; i < umo->hdr.buffer.count; i++)
    {
        const struct cbox_midi_event *event = cbox_midi_buffer_get_event(&umo->hdr.buffer, i);
        const uint8_t *pdata = cbox_midi_event_get_data(event);
        // Only encode control change events on channel 1 - it's the only
        // recognized type of events
        if (event->size == 3 && pdata[0] == 0xB0)
        {
            if (umo->endpoint_buffer_pos + 2 <= 8)
            {
                umo->endpoint_buffer[umo->endpoint_buffer_pos] = pdata[1];
                umo->endpoint_buffer[umo->endpoint_buffer_pos + 1] = pdata[2];
                umo->endpoint_buffer_pos += 2;
            }
            else
                g_warning("Nocturn MIDI buffer overflow.");
        }
    }
}

void usbio_fill_midi_output_buffer(struct cbox_usb_midi_output *umo)
{
    cbox_midi_merger_render(&umo->hdr.merger);
    if (!umo->hdr.buffer.count)
        return;
    
    if (umo->ifptr->protocol == USBMIDI_PROTOCOL_CLASS)
        encode_events_class(umo);
    else
    if (umo->ifptr->protocol == USBMIDI_PROTOCOL_NOCTURN)
        encode_events_nocturn(umo);
}

void usbio_send_midi_to_output(struct cbox_usb_midi_output *umo)
{
    if (!umo->endpoint_buffer_pos)
        return;
    
    int transferred = 0;
    int res = 0;
    if (umo->ifptr->epdesc_out.interrupt)
        res = libusb_interrupt_transfer(umo->ifptr->handle, umo->ifptr->epdesc_out.bEndpointAddress, umo->endpoint_buffer, umo->endpoint_buffer_pos, &transferred, 10);
    else
        res = libusb_bulk_transfer(umo->ifptr->handle, umo->ifptr->epdesc_out.bEndpointAddress, umo->endpoint_buffer, umo->endpoint_buffer_pos, &transferred, 10);
    if (res == 0 && transferred == umo->endpoint_buffer_pos)
        umo->endpoint_buffer_pos = 0;
    else
        g_warning("Failed to send MIDI events, transferred = %d out of %d, result = %d", (int)transferred, (int)umo->endpoint_buffer_pos, res);
}

void usbio_update_port_routing(struct cbox_io_impl *ioi)
{
    struct cbox_usb_io_impl *uii = (struct cbox_usb_io_impl *)ioi;
    for(GList *p = uii->rt_midi_ports; p; p = p->next)
    {
        struct cbox_usb_midi_interface *umi = p->data;
        if (umi->input_port)
        {
            if (!umi->input_port->hdr.output_set)
                cbox_midi_merger_connect(&uii->midi_input_merger, &umi->input_port->hdr.buffer, app.rt, NULL);
            else
                cbox_midi_merger_disconnect(&uii->midi_input_merger, &umi->input_port->hdr.buffer, app.rt);
        }
    }
}

void usbio_start_midi_capture(struct cbox_usb_io_impl *uii)
{
    uii->rt_midi_ports = g_list_copy(uii->midi_ports);

    for(GList *p = uii->rt_midi_ports; p; p = p->next)
    {
        struct cbox_usb_midi_interface *umi = p->data;
        cbox_midi_buffer_clear(&umi->input_port->hdr.buffer);
        if (umi->epdesc_in.found)
        {
            umi->current_sysex_length = 0;
            umi->transfer_in = usbio_transfer_new(uii->usbctx, "MIDI In", 0, 0, umi);
            int pktsize = umi->epdesc_in.wMaxPacketSize;
            if (pktsize > MAX_MIDI_PACKET_SIZE)
                pktsize = MAX_MIDI_PACKET_SIZE;
            if (umi->epdesc_in.interrupt)
                libusb_fill_interrupt_transfer(umi->transfer_in->transfer, umi->handle, umi->epdesc_in.bEndpointAddress, umi->midi_recv_data, pktsize, midi_transfer_cb, umi->transfer_in, 0);
            else
                libusb_fill_bulk_transfer(umi->transfer_in->transfer, umi->handle, umi->epdesc_in.bEndpointAddress, umi->midi_recv_data, pktsize, midi_transfer_cb, umi->transfer_in, 0);
        }
        else
            umi->transfer_in = NULL;
        if (umi->epdesc_out.found)
            umi->transfer_out = usbio_transfer_new(uii->usbctx, "MIDI Out", 0, 0, umi);
        else
            umi->transfer_out = NULL;
    }
    for(GList *p = uii->rt_midi_ports; p; p = p->next)
    {
        struct cbox_usb_midi_interface *umi = p->data;
        int res = usbio_transfer_submit(umi->transfer_in);
        if (res != 0)
        {
            usbio_transfer_destroy(umi->transfer_in);
            umi->transfer_in = NULL;
        }
    }
}

void usbio_stop_midi_capture(struct cbox_usb_io_impl *uii)
{
    for(GList *p = uii->rt_midi_ports; p; p = p->next)
    {
        struct cbox_usb_midi_interface *umi = p->data;
        
        if (umi->transfer_in)
        {
            usbio_transfer_shutdown(umi->transfer_in);
            usbio_transfer_destroy(umi->transfer_in);
            umi->transfer_in = NULL;
            cbox_midi_buffer_clear(&umi->input_port->hdr.buffer);
        }
        if (umi->transfer_out)
        {
            usbio_transfer_shutdown(umi->transfer_out);
            usbio_transfer_destroy(umi->transfer_out);
            umi->transfer_out = NULL;
        }
    }
    for(GList *p = uii->rt_midi_ports; p; p = p->next)
    {
        struct cbox_usb_midi_interface *umi = p->data;
        if (umi->epdesc_in.found)
            cbox_midi_merger_disconnect(&uii->midi_input_merger, &umi->input_port->hdr.buffer, NULL);
    }
    g_list_free(uii->rt_midi_ports);
}

void cbox_usb_midi_info_init(struct cbox_usb_midi_info *umi, struct cbox_usb_device_info *udi)
{
    umi->udi = udi;
    umi->intf = -1;
    umi->alt_setting = -1;
    umi->epdesc_in.found = FALSE;
    umi->epdesc_out.found = FALSE;
}

struct cbox_usb_midi_interface *usbio_open_midi_interface(struct cbox_usb_io_impl *uii, const struct cbox_usb_midi_info *uminf, struct libusb_device_handle *handle)
{
    struct cbox_usb_device_info *devinfo = uminf->udi;
    int bus = devinfo->bus;
    int devadr = devinfo->devadr;
    GError *error = NULL;
    // printf("Has MIDI port\n");
    // printf("Output endpoint address = %02x\n", ep->bEndpointAddress);
    if (!configure_usb_interface(handle, bus, devadr, uminf->intf, uminf->alt_setting, "MIDI (class driver)", &error))
    {
        g_warning("%s", error->message);
        g_error_free(error);
        return NULL;
    }
    
    struct cbox_usb_midi_interface *umi = calloc(sizeof(struct cbox_usb_midi_interface), 1);
    umi->uii = uii;
    umi->devinfo = devinfo;
    umi->handle = handle;
    umi->busdevadr = devinfo->busdevadr;
    uii->midi_ports = g_list_prepend(uii->midi_ports, umi);
    umi->protocol = uminf->protocol;
    memcpy(&umi->epdesc_in, &uminf->epdesc_in, sizeof(umi->epdesc_in));
    memcpy(&umi->epdesc_out, &uminf->epdesc_out, sizeof(umi->epdesc_out));

    // Drain the output buffer of the device - otherwise playing a few notes and running the program will cause
    // those notes to play.
    unsigned char flushbuf[256];
    int transferred = 0;
    int len = umi->epdesc_in.wMaxPacketSize;
    if (len > MAX_MIDI_PACKET_SIZE)
        len = MAX_MIDI_PACKET_SIZE;
    if (umi->epdesc_in.interrupt)
    {
        while(0 == libusb_interrupt_transfer(handle, umi->epdesc_in.bEndpointAddress, flushbuf, len, &transferred, 10) && transferred > 0)
            usleep(1000);
    }
    else
    {
        while(0 == libusb_bulk_transfer(handle, umi->epdesc_in.bEndpointAddress, flushbuf, len, &transferred, 10) && transferred > 0)
            usleep(1000);
    }
    devinfo->status = CBOX_DEVICE_STATUS_OPENED;
    
    // Initialise device - only used for Novation Nocturn, and it performs a
    // device reset. Perhaps not the best implementation of hot swap - it
    // will reset the device even when some other device was connected.
    if (umi->protocol == USBMIDI_PROTOCOL_NOCTURN)
    {
        static uint8_t data1[] = { 0xB0, 0, 0 };
        libusb_interrupt_transfer(handle, umi->epdesc_out.bEndpointAddress, data1, sizeof(data1), &transferred, 10);
    }
    
    return umi;
}

