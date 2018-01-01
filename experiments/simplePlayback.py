#! /usr/bin/env python3
# -*- coding: utf-8 -*-
from meta import ly2cbox, cboxSetTrack, initCbox, start

scene, cbox, eventLoop = initCbox("test01")

#Generate Music
music = "c'4 d' e' f'2 g' c''"
cboxBlob, durationInTicks = ly2cbox(music)
pattern = cbox.Document.get_song().pattern_from_blob(cboxBlob, durationInTicks)
cboxSetTrack("someInstrument", durationInTicks, pattern)

start()
