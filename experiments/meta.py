#! /usr/bin/env python3
# -*- coding: utf-8 -*-
import re
from calfbox import cbox  #use the globally installed calfbox
from asyncio import get_event_loop
from sys import stdout, maxsize
import os, signal

D1024 =210 * 2**0 # = 210. The lcm of 2, 3, 5, 7 . according to www.informatics.indiana.edu/donbyrd/CMNExtremes.htm this is the real world limit.
D512 = 210 * 2**1
D256 = 210 * 2**2
D128 = 210 * 2**3
D64  = 210 * 2**4
D32  = 210 * 2**5
D16  = 210 * 2**6   #16th     13440 ticks
D8   = 210 * 2**7   #eigth    26880 ticks
D4   = 210 * 2**8   #quarter  53760 ticks
D2   = 210 * 2**9   #half     107520 ticks
D1   = 210 * 2**10  #whole    215040 ticks
DB   = 210 * 2**11  #brevis   430080 ticks
DL   = 210 * 2**12  #longa
DM   = 210 * 2**13  #maxima
MAXIMUM = 0x7FFFFFFF # 31bit. maximum number of calfbox ticks allowed for its timeline, for example for the song duration


ly2pitch = {
    "ceses,,," : 00,
    "ces,,," : 10,
    "c,,," : 20,
    "cis,,," : 30,
    "cisis,,," : 40,
    "deses,,," : 50,
    "des,,," : 60,
    "d,,," : 70,
    "dis,,," : 80,
    "disis,,," : 90,
    "eeses,,," : 100,
    "ees,,," : 110,
    "e,,," : 120,
    "eis,,," : 130,
    "eisis,,," : 140,
    "feses,,," : 150,
    "fes,,," : 160,
    "f,,," : 170,
    "fis,,," : 180,
    "fisis,,," : 190,
    "geses,,," : 200,
    "ges,,," : 210,
    "g,,," : 220,
    "gis,,," : 230,
    "gisis,,," : 240,
    "aeses,,," : 250,
    "aes,,," : 260,
    "a,,," : 270,
    "ais,,," : 280,
    "aisis,,," : 290,
    "beses,,," : 300,
    "bes,,," : 310,
    "b,,," : 320,
    "bis,,," : 330,
    "bisis,,," : 340,
    "ceses,," : 350,
    "ces,," : 360,
    "c,," : 370,
    "cis,," : 380,
    "cisis,," : 390,
    "deses,," : 400,
    "des,," : 410,
    "d,," : 420,
    "dis,," : 430,
    "disis,," : 440,
    "eeses,," : 450,
    "ees,," : 460,
    "e,," : 470,
    "eis,," : 480,
    "eisis,," : 490,
    "feses,," : 500,
    "fes,," : 510,
    "f,," : 520,
    "fis,," : 530,
    "fisis,," : 540,
    "geses,," : 550,
    "ges,," : 560,
    "g,," : 570,
    "gis,," : 580,
    "gisis,," : 590,
    "aeses,," : 600,
    "aes,," : 610,
    "a,," : 620,
    "ais,," : 630,
    "aisis,," : 640,
    "beses,," : 650,
    "bes,," : 660,
    "b,," : 670,
    "bis,," : 680,
    "bisis,," : 690,
    "ceses," : 700,
    "ces," : 710,
    "c," : 720,
    "cis," : 730,
    "cisis," : 740,
    "deses," : 750,
    "des," : 760,
    "d," : 770,
    "dis," : 780,
    "disis," : 790,
    "eeses," : 800,
    "ees," : 810,
    "e," : 820,
    "eis," : 830,
    "eisis," : 840,
    "feses," : 850,
    "fes," : 860,
    "f," : 870,
    "fis," : 880,
    "fisis," : 890,
    "geses," : 900,
    "ges," : 910,
    "g," : 920,
    "gis," : 930,
    "gisis," : 940,
    "aeses," : 950,
    "aes," : 960,
    "a," : 970,
    "ais," : 980,
    "aisis," : 990,
    "beses," : 1000,
    "bes," : 1010,
    "b," : 1020,
    "bis," : 1030,
    "bisis," : 1040,
    "ceses" : 1050,
    "ces" : 1060,
    "c" : 1070,
    "cis" : 1080,
    "cisis" : 1090,
    "deses" : 1100,
    "des" : 1110,
    "d" : 1120,
    "dis" : 1130,
    "disis" : 1140,
    "eeses" : 1150,
    "ees" : 1160,
    "e" : 1170,
    "eis" : 1180,
    "eisis" : 1190,
    "feses" : 1200,
    "fes" : 1210,
    "f" : 1220,
    "fis" : 1230,
    "fisis" : 1240,
    "geses" : 1250,
    "ges" : 1260,
    "g" : 1270,
    "gis" : 1280,
    "gisis" : 1290,
    "aeses" : 1300,
    "aes" : 1310,
    "a" : 1320,
    "ais" : 1330,
    "aisis" : 1340,
    "beses" : 1350,
    "bes" : 1360,
    "b" : 1370,
    "bis" : 1380,
    "bisis" : 1390,
    "ceses'" : 1400,
    "ces'" : 1410,
    "c'" : 1420,
    "cis'" : 1430,
    "cisis'" : 1440,
    "deses'" : 1450,
    "des'" : 1460,
    "d'" : 1470,
    "dis'" : 1480,
    "disis'" : 1490,
    "eeses'" : 1500,
    "ees'" : 1510,
    "e'" : 1520,
    "eis'" : 1530,
    "eisis'" : 1540,
    "feses'" : 1550,
    "fes'" : 1560,
    "f'" : 1570,
    "fis'" : 1580,
    "fisis'" : 1590,
    "geses'" : 1600,
    "ges'" : 1610,
    "g'" : 1620,
    "gis'" : 1630,
    "gisis'" : 1640,
    "aeses'" : 1650,
    "aes'" : 1660,
    "a'" : 1670,
    "ais'" : 1680,
    "aisis'" : 1690,
    "beses'" : 1700,
    "bes'" : 1710,
    "b'" : 1720,
    "bis'" : 1730,
    "bisis'" : 1740,
    "ceses''" : 1750,
    "ces''" : 1760,
    "c''" : 1770,
    "cis''" : 1780,
    "cisis''" : 1790,
    "deses''" : 1800,
    "des''" : 1810,
    "d''" : 1820,
    "dis''" : 1830,
    "disis''" : 1840,
    "eeses''" : 1850,
    "ees''" : 1860,
    "e''" : 1870,
    "eis''" : 1880,
    "eisis''" : 1890,
    "feses''" : 1900,
    "fes''" : 1910,
    "f''" : 1920,
    "fis''" : 1930,
    "fisis''" : 1940,
    "geses''" : 1950,
    "ges''" : 1960,
    "g''" : 1970,
    "gis''" : 1980,
    "gisis''" : 1990,
    "aeses''" : 2000,
    "aes''" : 2010,
    "a''" : 2020,
    "ais''" : 2030,
    "aisis''" : 2040,
    "beses''" : 2050,
    "bes''" : 2060,
    "b''" : 2070,
    "bis''" : 2080,
    "bisis''" : 2090,
    "ceses'''" : 2100,
    "ces'''" : 2110,
    "c'''" : 2120,
    "cis'''" : 2130,
    "cisis'''" : 2140,
    "deses'''" : 2150,
    "des'''" : 2160,
    "d'''" : 2170,
    "dis'''" : 2180,
    "disis'''" : 2190,
    "eeses'''" : 2200,
    "ees'''" : 2210,
    "e'''" : 2220,
    "eis'''" : 2230,
    "eisis'''" : 2240,
    "feses'''" : 2250,
    "fes'''" : 2260,
    "f'''" : 2270,
    "fis'''" : 2280,
    "fisis'''" : 2290,
    "geses'''" : 2300,
    "ges'''" : 2310,
    "g'''" : 2320,
    "gis'''" : 2330,
    "gisis'''" : 2340,
    "aeses'''" : 2350,
    "aes'''" : 2360,
    "a'''" : 2370,
    "ais'''" : 2380,
    "aisis'''" : 2390,
    "beses'''" : 2400,
    "bes'''" : 2410,
    "b'''" : 2420,
    "bis'''" : 2430,
    "bisis'''" : 2440,
    "ceses''''" : 2450,
    "ces''''" : 2460,
    "c''''" : 2470,
    "cis''''" : 2480,
    "cisis''''" : 2490,
    "deses''''" : 2500,
    "des''''" : 2510,
    "d''''" : 2520,
    "dis''''" : 2530,
    "disis''''" : 2540,
    "eeses''''" : 2550,
    "ees''''" : 2560,
    "e''''" : 2570,
    "eis''''" : 2580,
    "eisis''''" : 2590,
    "feses''''" : 2600,
    "fes''''" : 2610,
    "f''''" : 2620,
    "fis''''" : 2630,
    "fisis''''" : 2640,
    "geses''''" : 2650,
    "ges''''" : 2660,
    "g''''" : 2670,
    "gis''''" : 2680,
    "gisis''''" : 2690,
    "aeses''''" : 2700,
    "aes''''" : 2710,
    "a''''" : 2720,
    "ais''''" : 2730,
    "aisis''''" : 2740,
    "beses''''" : 2750,
    "bes''''" : 2760,
    "b''''" : 2770,
    "bis''''" : 2780,
    "bisis''''" : 2790,
    "ceses'''''" : 2800,
    "ces'''''" : 2810,
    "c'''''" : 2820,
    "cis'''''" : 2830,
    "cisis'''''" : 2840,
    "deses'''''" : 2850,
    "des'''''" : 2860,
    "d'''''" : 2870,
    "dis'''''" : 2880,
    "disis'''''" : 2890,
    "eeses'''''" : 2900,
    "ees'''''" : 2910,
    "e'''''" : 2920,
    "eis'''''" : 2930,
    "eisis'''''" : 2940,
    "feses'''''" : 2950,
    "fes'''''" : 2960,
    "f'''''" : 2970,
    "fis'''''" : 2980,
    "fisis'''''" : 2990,
    "geses'''''" : 3000,
    "ges'''''" : 3010,
    "g'''''" : 3020,
    "gis'''''" : 3030,
    "gisis'''''" : 3040,
    "aeses'''''" : 3050,
    "aes'''''" : 3060,
    "a'''''" : 3070,
    "ais'''''" : 3080,
    "aisis'''''" : 3090,
    "beses'''''" : 3100,
    "bes'''''" : 3110,
    "b'''''" : 3120,
    "bis'''''" : 3130,
    "bisis'''''" : 3140,
    #"r" : float('inf'), a rest is not a pitch
    }

def plain(pitch):
    """ Extract the note from a note-number, without any octave but with the tailing zero.
    This means we double-use the lowest octave as abstract version."""
    #Dividing through the octave, 350, results in the number of the octave and the note as remainder.
    return divmod(pitch, 350)[1]

def octave(pitch):
    """Return the octave of given note. Lowest 0 is X,,,"""
    return divmod(pitch, 350)[0]

def halfToneDistanceFromC(pitch):
    """Return the half-tone step distance from C. The "sounding" interval"""
    return {
        #00 : 10, # ceses,,, -> bes
        #10 : 11, # ces,,, -> b

        00 : -2, # ceses,,, -> bes
        10 : -1, # ces,,, -> b
        20 : 0, # c,,,
        30 : 1, # cis,,,
        40 : 2, # cisis,,, -> d ...
        50 : 0, # deses,,,
        60 : 1, # des,,,
        70 : 2, # d,,,
        80 : 3, # dis,,,
        90 : 4, # disis,,,
        100 : 2, # eeses,,,
        110 : 3, # ees,,,
        120 : 4, # e,,,
        130 : 5, # eis,,,
        140 : 6, # eisis,,,
        150 : 3, # feses,,,
        160 : 4, # fes,,,
        170 : 5, # f,,,
        180 : 6, # fis,,,
        190 : 7, # fisis,,,
        200 : 5, # geses,,,
        210 : 6, # ges,,,
        220 : 7, # g,,,
        230 : 8, # gis,,,
        240 : 9, # gisis,,,
        250 : 7, # aeses,,,
        260 : 8, # aes,,,
        270 : 9, # a,,,
        280 : 10, # ais,,,
        290 : 11, # aisis,,,
        300 : 9, # beses,,,
        310 : 10, # bes,,,
        320 : 11, # b,,,
        330 : 12, # bis,,,
        340 : 13, # bisis,,,
        #330 : 0, # bis,,,
        #340 : 1, # bisis,,,
        }[plain(pitch)]


lyToMidi = {} #filled for all pitches on startup, below
for ly, pitch in ly2pitch.items():
    octOffset = (octave(pitch) +1) * 12 #twelve tones per midi octave
    lyToMidi[ly] = octOffset +  halfToneDistanceFromC(pitch)


lyToTicks = {
    "16" : D16,
     "8" : D8,
     "4" : D4,
     "2" : D2,
     "1" : D1,
    }



def ly(lilypondString):
    """Take string of simple lilypond notes, return midi pitches as generator of (pitch, ticks)"""
    lastDur = "4"
    for lyNote in lilypondString.split(" "):
        try:
            lyPitch, lyDur = re.split(r'(\d+)', lyNote)[0:2]
            lastDur = lyDur
        except ValueError:
            lyPitch = re.split(r'(\d+)', lyNote)[0]
            lyDur = lastDur

        yield (lyToMidi[lyPitch], lyToTicks[lyDur])


def ly2cbox(lilypondString):
    """Return (pbytes, durationInTicks)
    a python byte data type with midi data for cbox"""
    #cbox.Pattern.serialize_event(position, midibyte1 (noteon), midibyte2(pitch), midibyte3(velocity))
    pblob = bytes()
    startTick = 0
    for midiPitch, durationInTicks in ly(lilypondString):
        endTick = startTick + durationInTicks - 1  #-1 ticks to create a small logical gap. This is nothing compared to our tick value dimensions, but it is enough for the midi protocol to treat two notes as separate ones. Imporant to say that this does NOT affect the next note on. This will be mathematically correct anyway.
        pblob += cbox.Pattern.serialize_event(startTick, 0x90, midiPitch, 100) # note on
        pblob += cbox.Pattern.serialize_event(endTick  , 0x80, midiPitch, 100) # note off
        startTick = startTick + durationInTicks #no -1 for the next note
    return pblob, startTick


cboxTracks = {}  #trackName:(cboxTrack,cboxMidiOutUuid)
def cboxSetTrack(trackName, durationInTicks, pattern):
    """Creates or resets calfbox tracks including jack connections
    Keeps jack connections alive.

    pattern is most likely a single pattern created through cbox.Document.get_song().pattern_from_blob
    But it can also be a list of such patterns. In this case all patterns must be the same duration
    and the parameter durationInTicks is the length of ONE pattern.
    """

    if not trackName in cboxTracks:
        cboxMidiOutUuid = cbox.JackIO.create_midi_output(trackName)
        calfboxTrack = cbox.Document.get_song().add_track()
        cboxTracks[trackName] = (calfboxTrack, cboxMidiOutUuid)
    else:
        calfboxTrack, cboxMidiOutUuid = cboxTracks[trackName]
        calfboxTrack.delete()

    calfboxTrack = cbox.Document.get_song().add_track()
    calfboxTrack.set_external_output(cboxMidiOutUuid)
    cbox.JackIO.rename_midi_output(cboxMidiOutUuid, trackName)
    calfboxTrack.set_name(trackName)

    if type(pattern) is cbox.DocPattern:
        calfboxTrack.add_clip(0, 0, durationInTicks, pattern)  #pos, offset, length(and not end-position, but is the same for the complete track), pattern
    else: #iterable
        assert iter(pattern)
        #durationInTicks is the length of ONE pattern.
        for i, pat in enumerate(pattern):
            calfboxTrack.add_clip(i*durationInTicks, 0, durationInTicks, pat) #pos, offset, length, pattern.

    return calfboxTrack


def ly2Track(trackName, lyString):
    """Convert a simple string of lilypond notes to a cbox track and add that to the score"""
    music = lyString
    cboxBlob, durationInTicks = ly2cbox(music)
    pattern = cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks)
    cboxSetTrack(trackName, durationInTicks, pattern)

def getLongestTrackDuationInTicks():
    return max( max(clip.pos + clip.length for clip in cboxTrack.track.status().clips) for cboxTrack in cbox.Document.get_song().status().tracks if cboxTrack.track.status().clips)
    #for cboxTrack in cbox.Document.get_song().status().tracks:
    #    print (cboxTrack.track)
    #TODO: These are different than the one above. It is more. Why?
    #for cboxTrack, cboxMidiOutUuid in cboxTracks.values():
    #    print (cboxTrack.status())


def cboxLoop(eventLoop):
    cbox.call_on_idle()
    assert eventLoop.is_running()

    #it is not that simple. status = "[Running]" if cbox.Transport.status().playing else "[Stopped]"
    if cbox.Transport.status().playing == 1:
        status = "[Running]"
    elif cbox.Transport.status().playing == 0:
        status = "[Stopped]"
    elif cbox.Transport.status().playing == 2:
        status = "[Stopping]"
    elif cbox.Transport.status().playing is None:
        status = "[Uninitialized]"
    else:
        raise ValueError("Unknown playback status: {}".format(cbox.Transport.status().playing))

    stdout.write("                                           \r") #it is a hack but it cleans the line from old artefacts
    stdout.write('{}: {}\r'.format(status, cbox.Transport.status().pos_ppqn))
    stdout.flush()
    eventLoop.call_later(0.1, cboxLoop, eventLoop) #100ms delay

eventLoop = get_event_loop()
def initCbox(clientName, internalEventProcessor=True):
    cbox.init_engine("")
    cbox.Config.set("io", "client_name", clientName)
    cbox.start_audio()
    scene = cbox.Document.get_engine().new_scene()
    scene.clear()
    cbox.do_cmd("/master/set_ppqn_factor", None, [D4]) #quarter note has how many ticks?

    cbox.Transport.stop()
    cbox.Transport.seek_ppqn(0)
    cbox.Transport.set_tempo(120.0) #must be float

    if internalEventProcessor:
        eventLoop.call_soon(cboxLoop, eventLoop)
    return scene, cbox, eventLoop


def connectPhysicalKeyboards():
    midiKeyboards = cbox.JackIO.get_ports(".*", cbox.JackIO.MIDI_TYPE, cbox.JackIO.PORT_IS_SOURCE | cbox.JackIO.PORT_IS_PHYSICAL)
    ourMidiInPort = cbox.Config.get("io", "client_name",) + ":midi"
    for keyboard in midiKeyboards:
        cbox.JackIO.port_connect(keyboard, ourMidiInPort)

def start(autoplay = False, userfunction = None):
    def ask_exit():
        print()
        eventLoop.stop()
        shutdownCbox()

    try:
        dur = getLongestTrackDuationInTicks()
        cbox.Document.get_song().set_loop(dur, dur) #set playback length for the entire score.
    except ValueError:
        print ("Starting without a track. Setting song duration to a high value to generate recording space")
        cbox.Document.get_song().set_loop(maxsize, maxsize) #set playback length for the entire score.

    cbox.Document.get_song().update_playback()
    print ("update playback called")

    for signame in ('SIGINT', 'SIGTERM'):
        eventLoop.add_signal_handler(getattr(signal, signame), ask_exit)

    if userfunction:
        print ("Send SIGUSR1 with following command to trigger user function")
        print ("kill -10 {}".format(os.getpid()))
        print ()
        eventLoop.add_signal_handler(getattr(signal, "SIGUSR1"), userfunction)

    print ("Use jack transport to control playback")
    print ("Press Ctrl+C to abort")
    print("pid %s: send SIGINT or SIGTERM to exit." % os.getpid())

    try:
        eventLoop.run_forever()
    finally:
        eventLoop.close()


def shutdownCbox():
    cbox.stop_audio()
    cbox.shutdown_engine()
