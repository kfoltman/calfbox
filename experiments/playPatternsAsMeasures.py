#! /usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Copyright 2017, Nils Hilbricht, Germany ( https://www.hilbricht.net )

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

from meta import ly2cbox, cboxSetTrack, initCbox, start, D4

scene, cbox, eventLoop = initCbox("test02")

#Generate Music
music = "c'4 d' e' f'"
cboxBlob, durationInTicks = ly2cbox(music)
#cboxSetTrack("someInstrument", durationInTicks, pattern)

oneMeasureInTicks = D4 * 4
patternList = [
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
    cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks),
]

cboxSetTrack("metronome", oneMeasureInTicks, patternList)

def userfunction():
    D4 = 210 * 2**8
    MEASURE = 4 * D4
    cbox.Transport.stop()
    cbox.Document.get_song().update_playback()
    cbox.Transport.seek_ppqn(4 * MEASURE + 2 * D4  ) #4th measure in the middle
    cbox.Transport.play()

start(userfunction=userfunction)
