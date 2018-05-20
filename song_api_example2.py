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

# Make sure playback doesn't start prematurely
Transport.stop()

song = Document.get_song()

# Delete all the tracks and patterns
song.clear()

# Add the first track
trk = song.add_track()

# Create a binary blob that contains zero MIDI events
emptyblob = bytes()

# Create a new empty pattern
empty_pattern = song.pattern_from_blob(emptyblob, 16)

# This will be the length of the pattern (in pulses). It should be large enough
# to fit all the events
pattern_len = 10 * 24 * 2

# Add an instance (clip) of the empty pattern to the track
clip1 = trk.add_clip(pattern_len, 0, 16, empty_pattern)

# Add another instance after it
clip2 = trk.add_clip(2 * pattern_len, 0, 16, empty_pattern)

# Create a binary blob that contains the MIDI events
pblob = bytes()
for noteindex in range(20):
    # note on
    pblob += cbox.Pattern.serialize_event(noteindex * 24, 0x90, 36+noteindex*3, 127)
    # note off
    pblob += cbox.Pattern.serialize_event(noteindex * 24 + 23, 0x90, 36+noteindex*3, 0)

# Create a new pattern object using events from the blob
pattern = song.pattern_from_blob(pblob, pattern_len)

# Update all attributes of the second clip, rearranging the order
clip2.set_pattern(pattern)
clip2.set_pos(0)
clip2.set_offset(0)
clip2.set_length(pattern_len)

# Verify that the clips have been reordered
clips = [o.clip for o in trk.status().clips]
assert clips == [clip2, clip1]

# Stop the song at the end
song.set_loop(pattern_len, pattern_len)

# Set tempo - the argument must be a float
Transport.set_tempo(160.0)

# Send the updated song data to the realtime thread
song.update_playback()

# Flush
Transport.stop()

print ("Song length (seconds) is %f" % (cbox.Transport.ppqn_to_samples(pattern_len) * 1.0 / Transport.status().sample_rate))

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
