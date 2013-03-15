# Novation Nocturn driver
# Based on DWTFYW code by De Wet van Niekert (dewert). However, I had to
# put reading of the input endpoint in a separate thread because the only
# reliable way to read it is by using large timeouts (1s or so). With shorter
# timeouts, some events are lost/replaced by crossfader value.

import array
import binascii
import fcntl
import os
import usb.core
import usb.util
import sys
import time
import threading

class NocturnCommands:
    def __init__(self):
        self.pkt = ""
    def setModeButtonLight(self, button, state):
        self.pkt += chr(0x70 + button) + ('\x01' if state else '\x00')
    def setUserButtonLight(self, button, state):
        self.pkt += chr(0x78 + button) + ('\x01' if state else '\x00')
    def setEncoderMode(self, encoder, mode):
        self.pkt += chr(0x48 + encoder) + chr(mode << 4)
    def setEncoderValue(self, encoder, value):
        self.pkt += chr(0x40 + encoder) + chr(value)
    def setSpeedDialMode(self, mode):
        self.pkt += chr(0x51) + chr(mode << 4)
    def setSpeedDialValue(self, value):
        self.pkt += chr(0x50) + chr(value)
    def clear(self):
        for i in range(8):
            self.setModeButtonLight(i, False)
            self.setUserButtonLight(i, False)
            if i & 1:
                self.setEncoderMode(i, 3)
            else:
                self.setEncoderMode(i, 4)
            self.setEncoderValue(i, 64)
        self.setSpeedDialMode(5)
        self.setSpeedDialValue(64)

class NocturnHandler(threading.Thread):
    def __init__(self, n):
        threading.Thread.__init__(self)
        self.nocturn = n
        self.rpipefd, self.wpipefd = os.pipe()
        self.rpipe = os.fdopen(self.rpipefd, "rb")
        self.wpipe = os.fdopen(self.wpipefd, "wb")
        flags = fcntl.fcntl(self.rpipe, fcntl.F_GETFL)
        fcntl.fcntl(self.rpipe, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        self.setDaemon(True)
    def run(self):
        while True:
            pkt = self.nocturn.read()
            if pkt is not None:
                self.wpipe.write(pkt)
                self.wpipe.flush()
    def poll(self, handler):
        try:
            data = array.array('B', self.rpipe.read())
            i = 0
            # For longer sequences, Nocturn skips the control change message
            while i < len(data):
                if data[i] == 176:
                    i += 1
                    continue
                handler(data[i], data[i + 1])
                i += 2
        except IOError as e:
            pass
    def get_poll_fd(self):
        return self.rpipefd

class Nocturn:
    vendorID = 0x1235
    productID = 0x000a
    def __init__(self):
        dev = usb.core.find(idVendor=self.vendorID, idProduct=self.productID)
        # The values in here don't seem to matter THAT much
        initPackets=["b00000","28002b4a2c002e35","2a022c722e30"]
        #This is a minimum set that enables the device, but then it doesn't
        #really work reliably, at least the touch sensing
        #initPackets=["b00000", "2800"]

        if dev is None:
            raise ValueError('Device not found')
            sys.exit()

        self.dev = dev
        cfg = dev[1]
        intf = cfg[(0,0)]

        self.ep = intf[1]
        self.ep2 = intf[0]
        dev.set_configuration(2)
        for packet in initPackets:
            self.ep.write(binascii.unhexlify(packet))
        
        self.reset()
        self.reader = NocturnHandler(self)
        self.reader.start()

    def reset(self):
        cmd = NocturnCommands()
        cmd.clear()
        self.execute(cmd)

    def execute(self, cmd):
        self.ep.write(cmd.pkt)
    def read(self):
        try:
            data = self.ep2.read(8, None)
            return buffer(data)
        except usb.core.USBError as e:
            return None
    def poll(self, handler):
        return self.reader.poll(handler)
    def get_poll_fd(self):
        return self.reader.get_poll_fd()

