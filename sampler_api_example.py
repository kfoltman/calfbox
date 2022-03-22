#! /usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import struct
import time
import unittest

sys.path = ["./py"] + sys.path

import cbox

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine("") #empty string so cbox doesn't look for the .cboxrc file
cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()

npfs = instrument.engine.load_patch_from_string(0, '.', '', 'new_patch')
instrument.engine.set_patch(1, 0)

mgrp = npfs.get_global().get_children()[0]
g1 = mgrp.new_child()
g1.set_param("cutoff", "100")
g1.set_param("resonance", "6")
g1.set_param("fil_type", "lpf_4p")
g1.set_param("fileg_start", "50")
g1.set_param("fileg_attack", "0.01")
g1.set_param("fileg_decay", "0.2")
g1.set_param("fileg_sustain", "20")
g1.set_param("fileg_depth", "5400")
g1.set_param("fileg_release", "10")
g1.set_param("ampeg_release", "0.1")
g1.set_param("amp_veltrack", "0")
g1.set_param("volume", "-12")
g1.set_param("fileg_depthcc14", "-5400")

#g1.set_param("cutoff", "1000")
#g1.set_param("fillfo_freq", "4")
#g1.set_param("fillfo_depth", "2400")
#g1.set_param("fillfo_wave", "12")
#g1.set_param("fillfo_freqcc2", "4")

r1 = g1.new_child()
r1.set_param("sample", "*saw")
r1.set_param("transpose", "0")
r1.set_param("tune", "5")
r1.set_param("gain_cc17", "12")

r2 = g1.new_child()
r2.set_param("sample", "*sqr")
r2.set_param("transpose", "12")
r2.set_param("gain_cc17", "-12")

print(instrument.engine.status())

print("Ready!")

while True:
    cbox.call_on_idle(cmd_dumper)
