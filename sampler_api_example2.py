import os
import sys
import struct
import time
import unittest

sys.path = ["./py"] + sys.path

import cbox

global Document
Document = cbox.Document

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm = instrument.cmd_makeobj("/engine/load_patch_from_file", 0, 'synthbass.sfz', 'SynthBass')
instrument.cmd("/engine/set_patch", None, 1, 0)
for i in pgm.get_groups():
    print ("<group>", i.as_string())
    for j in i.get_children():
        print ("<region>", j.as_string())

print("Ready!")

while True:
    cbox.call_on_idle()
