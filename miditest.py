import array
import binascii
import usb.core
import usb.util
import time

class USBMIDIConfiguration:
    def __init__(self, cfg, ifno, ifalt):
        self.cfg = cfg
        self.ifno = ifno
        self.ifalt = ifalt
    def __str__(self):
        return "cfg=%d ifno=%d ifalt=%d" % (self.cfg, self.ifno, self.ifalt)
    def __repr__(self):
        return "USBMIDIConfiguration(%d,%d,%d)" % (self.cfg, self.ifno, self.ifalt)

class USBMIDIDeviceDescriptor:
    def __init__(self, vendorID, productID, interfaces = None):
        self.vendorID = vendorID
        self.productID = productID
        if interfaces is None:
            self.interfaces = []
        else:
            self.interfaces = interfaces
    def add_interface(self, config, ifno, ifalt):
        self.interfaces.append(USBMIDIConfiguration(config, ifno, ifalt))
    def has_interfaces(self):
        return len(self.interfaces)
    def __str__(self):
        return "vid=%04x pid=%04x" % (self.vendorID, self.productID)
    def __repr__(self):
        return "USBMIDIDeviceDescriptor(0x%04x, 0x%04x, %s)" % (self.vendorID, self.productID, self.interfaces)

class USBMIDI:
    cin_sizes = [None, None, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1]
    def __init__(self, mididev, midicfg, debug = False):
        dev = usb.core.find(idVendor = mididev.vendorID, idProduct = mididev.productID)
        self.dev = dev
        intf = None
        for cfgo in dev:
            if cfgo.bConfigurationValue == midicfg.cfg:
                intf = cfgo[(midicfg.ifno, midicfg.ifalt)]
        if not intf:
            raise ValueError, "Configuration %d not found" % midicfg.cfg
        print intf.bNumEndpoints
        self.epIn = None
        self.epOut = None
        for ep in intf:
            if debug:
                print "endpoint %x" % ep.bEndpointAddress
            if ep.bEndpointAddress > 0x80:
                if self.epIn is None:
                    self.epIn = ep
            else:
                if self.epOut is None:
                    self.epOut = ep
            
    def read(self):
        try:
            data = self.epIn.read(64)
            if data is None:
                return None
            return array.array('B', data)
        except usb.core.USBError, e:
            return None
            
    def encode(self, port, msg):
        a = array.array('B')
        a.append(16 * port + (msg[0] >> 4))
        a.fromlist(msg)
        return a
        
    def write(self, data):
        self.epOut.write(data)
            
    def parse(self, data):
        i = 0
        msgs = []
        while i < len(data):
            if data[i] == 0:
                break
            cin, cable_id = data[i] & 15, data[i] >> 4
            msgs.append(data[i + 1 : i + 1 + KeyRig25.cin_sizes[cin]])
            i += 4
        return msgs
        
    @staticmethod
    def findall(vendorID = None, productID = None, debug = False):
        dev_list = []
        devices = usb.core.find(find_all = True)
        for dev in devices:
            if vendorID is not None and dev.idVendor != vendorID:
                continue
            if productID is not None and dev.idProduct != productID:
                continue
            thisdev = USBMIDIDeviceDescriptor(dev.idVendor, dev.idProduct)
            if debug:
                print "Device %04x:%04x, class %d" % (dev.idVendor, dev.idProduct, dev.bDeviceClass)
            if dev.bDeviceClass == 0: # device defined at interface level
                for cfg in dev:
                    if debug:
                        print "Configuration ", cfg.bConfigurationValue
                    for intf in cfg:
                        if debug:
                            print "Interface %d alternate-setting %d" % (intf.bInterfaceNumber, intf.bAlternateSetting)
                            print "Class %d subclass %d" % (intf.bInterfaceClass, intf.bInterfaceSubClass)
                        if intf.bInterfaceClass == 1 and intf.bInterfaceSubClass == 3:
                            if debug:
                                print "(%d,%d,%d): This is USB MIDI" % (cfg.bConfigurationValue, intf.bInterfaceNumber, intf.bAlternateSetting)
                            thisdev.add_interface(cfg.bConfigurationValue, intf.bInterfaceNumber, intf.bAlternateSetting)
            if thisdev.has_interfaces():
                dev_list.append(thisdev)
        return dev_list
                            
        #print devices

class KeyRig25(USBMIDI):
    def __init__(self):
        devlist = USBMIDI.findall(vendorID = 0x763, productID = 0x115)
        if not devlist:
            raise ValueError
        USBMIDI.__init__(self, devlist[0], devlist[0].interfaces[0])
    
class XMidi2x2(USBMIDI):
    def __init__(self):
        devlist = USBMIDI.findall(vendorID = 0x41e, productID = 0x3f08)
        if not devlist:
            raise ValueError
        USBMIDI.__init__(self, devlist[0], devlist[0].interfaces[0])

print USBMIDI.findall()
xmidi = XMidi2x2()
xmidi.write(xmidi.encode(1, [0x90, 36, 100]))
xmidi.write(xmidi.encode(1, [0x80, 36, 100]))

krig = KeyRig25()
while True:
    data = krig.read()
    if data is not None:
        decoded = krig.parse(data)
        reencoded = array.array('B')
        for msg in decoded:
            reencoded.extend(xmidi.encode(1, list(msg)))
        xmidi.write(reencoded)
        print decoded
