#! /usr/bin/env python3
# -*- coding: utf-8 -*-
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
