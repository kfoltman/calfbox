import os
import sys
import struct
import time
import unittest

sys.path = ["./py"] + sys.path

import cbox

global Document
global Transport
Document = cbox.Document
Transport = cbox.Transport

song = Document.get_song()

# Delete all the tracks and patterns
song.clear()

# Create a binary blob that contains the MIDI events
pblob = bytes()
for noteindex in range(20):
    # note on
    pblob += cbox.Pattern.serialize_event(noteindex * 12, 0x90, 36+noteindex*3, 127)
    # note off
    pblob += cbox.Pattern.serialize_event(noteindex * 12 + 11, 0x90, 36+noteindex*3, 0)

# This will be the length of the pattern (in pulses). It should be large enough
# to fit all the events
pattern_len = 10 * 24 * 2

# Create a new pattern object using events from the blob
pattern = song.pattern_from_blob(pblob, pattern_len)

retrig = 10
i = 0
while i < 50:
    i += 1
    retrig -= 1
    if retrig <= 0:
        print ("Triggering adhoc pattern with ID 1")
        Document.get_scene().play_pattern(pattern, 240, 0)
        retrig = 5
    # Query JACK ports, new USB devices etc.
    cbox.call_on_idle()
    time.sleep(0.1)
