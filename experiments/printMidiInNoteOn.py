#! /usr/bin/env python3
# -*- coding: utf-8 -*-
from meta import initCbox, start, connectPhysicalKeyboards

def processMidiIn(eventLoop):
    """cbox event: ("event address", None, [firstByte, pitch, velocit])

    first is the normal midi-event + channel
    channel = first & 0x0F
    mode = first & 0xF0

    Of course it doesn't need to be pitch and velocity.
    But this is easier to document than "data2, data3"

    Either call cbox.call_on_idle or cbox.get_new_events.
    Both will clean the event queue but only the latter will give us
    the results as python data.
    """
    eventList = cbox.get_new_events()
    for address, uninterestingStuff, event in eventList: #we are only interested in the event, which is a list again.
        length = len(event)
        if length == 3: #we can only unpack the event after knowing its length.
            first, second, third = event
            channel = first & 0x0F
            mode = first & 0xF0  #0x90 note on, 0x80 note off and so on.

            if mode == 0x90: #Note On
                midipitch = second
                velocity = third
                print("Channel: {}, Pitch: {}, Velocity: {}".format(channel, midipitch, velocity))

            elif mode == 0x80: #Note Off
                midipitch = second
                velocity = third

            elif mode == 0xB0:  #CC
                ccNumber = second
                ccValue = third

            #else:
                #discard the events
    eventLoop.call_later(0.1, processMidiIn, eventLoop)

scene, cbox, eventLoop = initCbox("test01", internalEventProcessor=False)
eventLoop.call_later(0.1, processMidiIn, eventLoop) #100ms
connectPhysicalKeyboards()
start()
