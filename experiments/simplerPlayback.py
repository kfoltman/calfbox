#! /usr/bin/env python3
# -*- coding: utf-8 -*-
from meta import ly2Track, initCbox, start

scene, cbox, eventLoop = initCbox("test01")

ly2Track(trackName="someInstrument", lyString="c'4 d' e' f'2 g' c''")
start()
