#! /usr/bin/env python3
# -*- coding: utf-8 -*-
"""
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

from meta import ly2cbox, cboxSetTrack, initCbox, start

scene, cbox, eventLoop = initCbox("test01")

#Generate Music
music = "c'4 d' e' f'2 g' c''"
cboxBlob, durationInTicks = ly2cbox(music)
pattern = cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks)
cboxSetTrack("someInstrument", durationInTicks, pattern)

start()
