#! /usr/bin/env python3
# -*- coding: utf-8 -*-
from meta import ly2Track, initCbox, start, connectPhysicalKeyboards

def processMidiIn(eventLoop):
    """cbox event: ("event address", None, [firstByte, pitch, velocit])

    Cbox event come in pairs

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
    lenList = len(eventList)
    if lenList >= 2 and lenList % 2 == 0:
        for (address, uninterestingStuff, event) in eventList: #we are only interested in the event, which is a list again.
            #print (address, "event:", event, "playback:", cbox.Transport.status().playing)
            if address in ("/io/midi/event_time_samples", "/io/midi/event_time_ppqn", ):
                assert len(event) == 1
                #We are very strict at the moment. A timestamp is only allowed if there wasn't another timestamp waiting. It is strict because only simple_event with len==3 eat up the timestamp. Any other event will trigger an error.
                if processMidiIn.lastTimestamp:
                    raise NotImplementedError("the previous event didn't eat up the timestamp")
                else:
                    if address == "/io/midi/event_time_ppqn":
                        assert cbox.Transport.status().playing == 1
                        during_recording = True
                    else:
                        assert not cbox.Transport.status().playing == 1
                        during_recording = False
                    processMidiIn.lastTimestamp = event[0]

            elif address == "/io/midi/simple_event" and len(event) == 3: #we can only unpack the event after knowing its length.
                assert processMidiIn.lastTimestamp # Midi events are always preceded by timestamps
                first, second, third = event
                channel = first & 0x0F
                mode = first & 0xF0  #0x90 note on, 0x80 note off and so on.

                if mode == 0x90: #Note On. 144 in decimal
                    midipitch = second
                    velocity = third
                    if during_recording:
                        print("ON: {}, Channel: {}, Pitch: {}, Velocity: {}".format(processMidiIn.lastTimestamp, channel, midipitch, velocity))
                    #else:
                    #    print("ON Time-Samples: {}, Channel: {}, Pitch: {}, Velocity: {}".format(processMidiIn.lastTimestamp, channel, midipitch, velocity))

                elif mode == 0x80: #Note Off. 128 in decimal
                    midipitch = second
                    velocity = third
                    if during_recording:
                        print("OFF: {}, Channel: {}, Pitch: {}, Velocity: {}".format(processMidiIn.lastTimestamp, channel, midipitch, velocity))

                #elif mode == 0xB0:  #CC
                #    ccNumber = second
                #    ccValue = third
                #else:
                    #discard the events

                processMidiIn.lastTimestamp = None

            else:
                raise NotImplementedError("Address type {} unknown".format(address))


    eventLoop.call_later(0.1, processMidiIn, eventLoop)
processMidiIn.lastTimestamp = None

scene, cbox, eventLoop = initCbox("test01", internalEventProcessor=False)
#ly2Track(trackName="doWeNeedThis", lyString="c8")
eventLoop.call_later(0.1, processMidiIn, eventLoop) #100ms
connectPhysicalKeyboards()
start()
