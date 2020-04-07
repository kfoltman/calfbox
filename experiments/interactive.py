#! /usr/bin/env -S python3 -i
# -*- coding: utf-8 -*-
"""
This is a minimal calfbox python example. It is meant as a starting
point to find bugs and test performance.

Copyright, Nils Hilbricht, Germany ( https://www.hilbricht.net )

This code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

import atexit
from pprint import pprint
from calfbox import cbox


cbox.init_engine("")
cbox.Config.set("io", "outputs", 8)
NAME = "Cbox Interactive"
cbox.Config.set("io", "client_name", NAME)

cbox.start_audio()
scene = cbox.Document.get_engine().new_scene()
scene.clear()

trackName = "trackOne"
cboxMidiOutUuid = cbox.JackIO.create_midi_output(trackName)
calfboxTrack = cbox.Document.get_song().add_track()


pblob = bytes()
pblob += cbox.Pattern.serialize_event(0, 0x90, 60, 100) # note on
pblob += cbox.Pattern.serialize_event(383, 0x80, 60, 100) # note off
pattern = cbox.Document.get_song().pattern_from_blob(pblob, 384)
calfboxTrack.add_clip(0, 0, 384, pattern)  #pos, offset, length(and not end-position, but is the same for the complete track), pattern
cbox.Document.get_song().set_loop(384, 384) #set playback length for the entire score. Why is the first value not zero? That would create an actual loop from the start to end. We want the song to play only once. The cbox way of doing that is to set the loop range to zero at the end of the track. Zero length is stop.
cbox.Document.get_song().update_playback()

print()

def exit_handler():
    #Restore initial state and stop the engine
    cbox.Transport.stop()
    cbox.stop_audio()
    cbox.shutdown_engine()
atexit.register(exit_handler)
