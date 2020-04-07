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

from meta import initCbox, start, connectPhysicalKeyboards

def processMidiIn(eventLoop):
    eventList = cbox.get_new_events()
    if eventList:
        for (address, uninterestingStuff, event) in eventList: #we are only interested in the event, which is a list again.
            print (address, "event:", event, "playback:", cbox.Transport.status().playing)
    eventLoop.call_later(0.1, processMidiIn, eventLoop)

scene, cbox, eventLoop = initCbox("test01", internalEventProcessor=False)
eventLoop.call_later(0.1, processMidiIn, eventLoop) #100ms
connectPhysicalKeyboards()
start()
