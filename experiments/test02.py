#! /usr/bin/env python3
# -*- coding: utf-8 -*-
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
