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

# Add the first track
trk = song.add_track()

# Create a binary blob that contains the MIDI events
pblob = bytes()
for noteindex in range(20):
    # note on
    pblob += cbox.Pattern.serialize_event(noteindex * 24, 0x90, 36+noteindex*3, 127)
    # note off
    pblob += cbox.Pattern.serialize_event(noteindex * 24 + 23, 0x90, 36+noteindex*3, 0)

# This will be the length of the pattern (in pulses). It should be large enough
# to fit all the events
pattern_len = 10 * 24 * 2

# Create a new pattern object using events from the blob
pattern = song.pattern_from_blob(pblob, pattern_len)

# Add an instance (clip) of the pattern to the track at position 0
# The clip will contain the whole pattern (it is also possible to insert
# a single slice of the pattern)
clip = trk.add_clip(0, 0, pattern_len, pattern)

# Stop the song at the end
song.set_loop(pattern_len, pattern_len)

# Set tempo - the argument must be a float
Transport.set_tempo(160.0)

# Send the updated song data to the realtime thread
song.update_playback()

# The /master object API doesn't have any nice Python wrapper yet, so accessing
# it is a bit ugly, still - it works

# Start playback
Transport.play()
print ("Playing")

while True:
    # Get transport information - current position (samples and pulses), current tempo etc.
    master = Transport.status()
    print (master.pos_ppqn)
    # Query JACK ports, new USB devices etc.
    cbox.call_on_idle()
    time.sleep(0.1)
