import os
import sys
import struct
import time
import unittest

sys.path = ["./py"] + sys.path

import cbox

global Document
Document = cbox.Document

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
cbox.do_cmd("/master/set_tempo", None, [1412.8])

# Send the updated song data to the realtime thread
song.update_playback()

# The /master object API doesn't have any nice Python wrapper yet, so accessing
# it is a bit ugly, still - it works

# Start playback
cbox.do_cmd("/master/play", None, [])
print ("Playing")

while True:
    time.sleep(0.05)
    # Get transport information - current position (samples and pulses), current tempo etc.
    master = cbox.GetThings("/master/status", ['pos', 'pos_ppqn', 'tempo', 'timesig', 'sample_rate'], [])
    print (master.pos_ppqn)
    # Query JACK ports, new USB devices etc.
    cbox.do_cmd("/on_idle", None, [])
    time.sleep(0.1)
