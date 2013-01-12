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
#include <errno.h>

#define OMEGA_EP_PLAYBACK 0x01
//#define OMEGA_EP_CAPTURE 0x83

#define MULTIMIX_EP_PLAYBACK 0x02
//#define MULTIMIX_EP_CAPTURE 0x86
#define MULTIMIX_EP_SYNC 0x81

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
    uii->play_function = usbio_play_buffer_adaptive;
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

    uii->play_function = usbio_play_buffer_asynchronous;
    uii->handle_audiodev = handle;
    uii->audio_output_endpoint = MULTIMIX_EP_PLAYBACK;
    uii->audio_output_pktsize = 156;
    uii->audio_sync_endpoint = MULTIMIX_EP_SYNC;
    return TRUE;
}

////////////////////////////////////////////////////////////////////////////////

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

static const struct libusb_endpoint_descriptor *get_endpoint_by_address(const struct libusb_interface_descriptor *asdescr, uint8_t addr)
{
    for (int epi = 0; epi < asdescr->bNumEndpoints; epi++)
    {
        const struct libusb_endpoint_descriptor *ep = &asdescr->endpoint[epi];
        if (ep->bEndpointAddress == addr)
            return ep;
    }
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////

struct usb_ut_descriptor_header
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
};

struct usb_class_specific_header
{
    struct usb_ut_descriptor_header hdr;
    uint8_t bcdADC[2];
    uint8_t wTotalLengthLo, wTotalLengthHi;
    uint8_t bInCollection;
    uint8_t baInterfaceNr[0];
};

struct usbaudio_input_terminal_descriptor
{
    uint8_t bTerminalID;
    uint8_t wTerminalTypeLo, wTerminalTypeHi;
    uint8_t bAssocTerminal;
    uint8_t bNrChannels;
    uint8_t wChannelConfigLo, wChannelConfigHi;
    uint8_t iChannelNames;
    uint8_t iTerminal;
};

struct usbaudio_output_terminal_descriptor
{
    uint8_t bTerminalID;
    uint8_t wTerminalTypeLo, wTerminalTypeHi;
    uint8_t bAssocTerminal;
    uint8_t bSourceID;
    uint8_t iTerminal;
};

struct usbaudio_mixer_unit_descriptor1
{
    uint8_t bUnitID;
    uint8_t bNrInPins;
    uint8_t baSourceID[0];
};

struct usbaudio_mixer_unit_descriptor2
{
    uint8_t bNrChannels;
    uint8_t wChannelConfigLo, wChannelConfigHi;
    uint8_t bmControls[0];
    // after the last index, 8-bit iMixer
};

struct usbaudio_selector_unit_descriptor1
{
    uint8_t bUnitID;
    uint8_t bNrInPins;
    uint8_t baSourceID[0];
    // after the last index, 8-bit iSelector
};

struct usbaudio_feature_unit_descriptor1
{
    uint8_t bUnitID;
    uint8_t bSourceID;
    uint8_t bControlSize;
    uint8_t bmaControls[0];
    // after the last index, 8-bit iFeature
};

// Unit/Terminal Descriptor header from the spec
#include <stdio.h>

// termt10.pdf
static const char *get_terminal_type_name(int type)
{
    switch(type)
    {
        case 0x101: return "USB Streaming";
        case 0x1FF: return "USB vendor specific";
        case 0x201: return "Microphone";
        case 0x202: return "Desktop microphone";
        case 0x203: return "Personal microphone";
        case 0x204: return "Omni-directional microphone";
        case 0x205: return "Microphone array";
        case 0x206: return "Processing microphone array";
        case 0x301: return "Speaker";
        case 0x302: return "Headphones";
        case 0x303: return "Head-mounted display audio";
        case 0x304: return "Desktop speaker";
        case 0x305: return "Room speaker";
        case 0x306: return "Communication speaker";
        case 0x307: return "Low-frequency effects speaker";
        case 0x400: return "Bi-directional Undefined";
        case 0x401: return "Handset (handheld)";
        case 0x402: return "Handset (head-mounted)";
        case 0x403: return "Speakerphone";
        case 0x404: return "Echo-suppressing speakerphone";
        case 0x405: return "Echo-cancelling speakerphone";
        case 0x501: return "Phone line";
        case 0x502: return "Telephone";
        case 0x503: return "Down line phone";
        case 0x600: return "External Undefined";
        case 0x601: return "Analog connector";
        case 0x602: return "Digital audio interface";
        case 0x603: return "Line connector";
        case 0x604: return "Legacy audio";
        case 0x605: return "S/PDIF";
        case 0x606: return "1394 D/A Stream";
        case 0x607: return "1394 D/V Stream Sountrack";
        case 0x700: return "Embedded undefined";
        case 0x701: return "Level calibration noise source";
        case 0x702: return "Equalization noise";
        case 0x703: return "CD player";
        case 0x704: return "DAT";
        case 0x705: return "DCC";
        case 0x706: return "MiniDisc";
        case 0x707: return "Analog tape";
        case 0x708: return "Phono";
        case 0x709: return "VCR Audio";
        case 0x70A: return "VideoDisc Audio";
        case 0x70B: return "DVD Audio";
        case 0x70C: return "TV Tuner Audio";
        case 0x70D: return "Satellite Receiver Audio";
        case 0x70E: return "Cable Tuner Audio";
        case 0x70F: return "DSS Audio";
        case 0x710: return "Radio Receiver";
        case 0x711: return "Radio Transmitter";
        case 0x712: return "Multitrack Recorder";
        case 0x713: return "Synthesizer";
        default:
            return "Unknown";
    }
}

static gboolean parse_audio_control_class(struct cbox_usb_audio_info *uai, const struct libusb_interface_descriptor *asdescr, gboolean other_config)
{
#if 0
    if (asdescr->extra_length < 8)
    {
        g_warning("AudioControl interface descriptor length is %d, should be at least 8", (int)asdescr->extra_length);
        return FALSE;
    }
    const struct usb_class_specific_header *extra = (const struct usb_class_specific_header *)asdescr->extra;
    uint16_t wTotalLength = extra->wTotalLengthLo + 256 * extra->wTotalLengthHi;
    if (wTotalLength > asdescr->extra_length)
    {
        g_warning("AudioControl interface descriptor total length is %d, but libusb value is %d", (int)wTotalLength, (int)asdescr->extra_length);
        return FALSE;
    }
    if (extra->hdr.bDescriptorType != 36)
    {
        g_warning("AudioControl interface descriptor type is %d, but expected 36 (CS_INTERFACE)", (int)extra->hdr.bDescriptorType);
        return FALSE;
    }
    if (extra->hdr.bDescriptorSubtype != 1)
    {
        g_warning("AudioControl interface descriptor type is %d, but expected 1 (HEADER)", (int)extra->hdr.bDescriptorSubtype);
        return FALSE;
    }
    
    printf("Device %04x:%04x\n", uai->udi->vid, uai->udi->pid);
    printf("hdrlen=%d dt=%d dst=%d ver=%02x%02x total len=%d\n", extra->hdr.bLength, extra->hdr.bDescriptorType, extra->hdr.bDescriptorSubtype, extra->bcdADC[0], extra->bcdADC[1], wTotalLength);
    for(int i = 0; i < extra->bInCollection; i++)
    {
        printf("interface nr %d = %d\n", i, extra->baInterfaceNr[i]);
    }
    
    for(uint32_t pos = extra->hdr.bLength; pos < wTotalLength; pos += asdescr->extra[pos])
    {
        const struct usb_ut_descriptor_header *hdr = (const struct usb_ut_descriptor_header *)(asdescr->extra + pos);
        if (hdr->bDescriptorType != 36)
        {
            g_warning("Skipping unit/terminal descriptor type %d,%d", (int)hdr->bDescriptorType, (int)hdr->bDescriptorSubtype);
            continue;
        }
        printf("hdr %d,%d len %d\n", hdr->bDescriptorType, hdr->bDescriptorSubtype, hdr->bLength);        
        switch(hdr->bDescriptorSubtype)
        {
            case 2: // INPUT_TERMINAL
            {
                const struct usbaudio_input_terminal_descriptor *itd = (const struct usbaudio_input_terminal_descriptor *)(hdr + 1);
                int wTerminalType = itd->wTerminalTypeHi * 256 + itd->wTerminalTypeLo;
                printf("INPUT TERMINAL %d: type %04x (%s)\n", (int)itd->bTerminalID, wTerminalType, get_terminal_type_name(wTerminalType));
                break;
            }
            case 3: // OUTPUT_TERMINAL
            {
                const struct usbaudio_output_terminal_descriptor *otd = (const struct usbaudio_output_terminal_descriptor *)(hdr + 1);
                int wTerminalType = otd->wTerminalTypeHi * 256 + otd->wTerminalTypeLo;
                printf("OUTPUT TERMINAL %d: type %04x (%s) source %d\n", (int)otd->bTerminalID, wTerminalType, get_terminal_type_name(wTerminalType), otd->bSourceID);
                break;
            }
            case 4: // MIXER_UNIT
            {
                const struct usbaudio_mixer_unit_descriptor1 *mud = (const struct usbaudio_mixer_unit_descriptor1 *)(hdr + 1);
                printf("MIXER UNIT %d\n", (int)mud->bUnitID);
                for (int i = 0; i < mud->bNrInPins; i++)
                {
                    printf("Input[%d] = %d\n", i, mud->baSourceID[i]);
                }
                break;
            }
            case 5: // SELECTOR_UNIT
            {
                const struct usbaudio_selector_unit_descriptor1 *sud = (const struct usbaudio_selector_unit_descriptor1 *)(hdr + 1);
                printf("SELECTOR UNIT %d (%d pins)\n", (int)sud->bUnitID, sud->bNrInPins);
                for (int i = 0; i < sud->bNrInPins; i++)
                {
                    printf("Input[%d] = %d\n", i, sud->baSourceID[i]);
                }
                break;
            }
            case 6: // FEATURE_UNIT
            {
                static const char *features[] = {"Mute", "Volume", "Bass", "Mid", "Treble", "EQ", "AGC", "Delay", "Bass Boost", "Loudness" };
                const struct usbaudio_feature_unit_descriptor1 *fud = (const struct usbaudio_feature_unit_descriptor1 *)(hdr + 1);
                printf("FEATURE UNIT %d (source = %d, control size %d)\n", (int)fud->bUnitID, fud->bSourceID, fud->bControlSize);
                for (int i = 0; i < fud->bControlSize * 8; i++)
                {
                    if (i >= sizeof(features) / sizeof(features[0]))
                        break;
                    if (fud->bmaControls[i >> 3] & (1 << (i & 7)))
                        printf("Master %s\n", features[i]);
                }
                for (int ch = 1; ch < (hdr->bLength - 7) / fud->bControlSize; ch++)
                {
                    int chofs = ch * fud->bControlSize;
                    for (int i = 0; i < fud->bControlSize * 8; i++)
                    {
                        if (i >= sizeof(features) / sizeof(features[0]))
                            break;
                        if (fud->bmaControls[(i >> 3) + chofs] & (1 << (i & 7)))
                            printf("Channel %d %s\n", ch, features[i]);
                    }
                }
                break;
            }
            default:
                printf("Unsupported unit type %d\n", hdr->bDescriptorSubtype);
                break;
        }
    }
#endif
    return FALSE;
}

static gboolean parse_midi_class(struct cbox_usb_midi_info *umi, const struct libusb_interface_descriptor *asdescr, gboolean other_config)
{
    const struct libusb_endpoint_descriptor *ep = get_midi_input_endpoint(asdescr);
    if (!ep)
        return FALSE;
    
    if (other_config)
        return TRUE;
    
    if (umi->ep)
        return FALSE;
    umi->intf = asdescr->bInterfaceNumber;
    umi->alt_setting = asdescr->bAlternateSetting;
    umi->ep = ep;
    return TRUE;
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

    int active_config = 0, alt_config = -1;

    struct cbox_usb_audio_info uainf;
    cbox_usb_audio_info_init(&uainf, udi);

    struct cbox_usb_midi_info uminf;
    cbox_usb_midi_info_init(&uminf, udi);

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
        gboolean other_config = cur_config != udi->active_config;
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
                if (asdescr->bInterfaceClass == LIBUSB_CLASS_AUDIO && asdescr->bInterfaceSubClass == 1)
                {
                    if (parse_audio_control_class(&uainf, asdescr, other_config) && other_config)
                        udi->configs_with_audio |= config_mask;
                }
                else if (asdescr->bInterfaceClass == LIBUSB_CLASS_AUDIO && asdescr->bInterfaceSubClass == 3)
                {
                    if (parse_midi_class(&uminf, asdescr, other_config) && other_config)
                        udi->configs_with_midi |= config_mask;
                }
                else if (udi->vid == 0x09e8 && udi->pid == 0x0062) // Akai MPD16
                {
                    uminf.intf = asdescr->bInterfaceNumber;
                    uminf.alt_setting = asdescr->bAlternateSetting;
                    uminf.ep = get_endpoint_by_address(asdescr, 0x82);
                }
            }
        }
    }
    if (!uminf.ep && udi->configs_with_midi)
        g_warning("%03d:%03d - MIDI port available on different configs: mask=0x%x", bus, devadr, udi->configs_with_midi);

    if (udi->vid == 0x1210 && udi->pid == 0x0002) // Lexicon Omega
        is_audio = TRUE;
    if (udi->vid == 0x13b2 && udi->pid == 0x0030) // Alesis Multimix 8
        is_audio = TRUE;
    
    // All configs/interfaces/alts scanned, nothing interesting found -> mark as unsupported
    udi->is_midi = uminf.ep != NULL;
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
    
    if (uminf.ep)
    {
        g_debug("Found MIDI device %03d:%03d, trying to open", bus, devadr);
        if (0 != usbio_open_midi_interface(uii, udi, handle, &uminf))
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

gboolean usbio_scan_devices(struct cbox_usb_io_impl *uii, gboolean probe_only)
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
                usbio_forget_device(uii, udi);
        }
    }
    g_list_free(prev_keys);
    
    for (i = 0; i < num_devices; i++)
        added = inspect_device(uii, dev_list[i], busdevadrs[i], probe_only) || added;
    
    free(busdevadrs);
    libusb_free_device_list(dev_list, 0);
    return added || removed;
}

void usbio_forget_device(struct cbox_usb_io_impl *uii, struct cbox_usb_device_info *devinfo)
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
