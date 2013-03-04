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
pgm_no = instrument.engine.get_unused_program()
pgm = instrument.engine.load_patch_from_file(pgm_no, 'synthbass.sfz', 'SynthBass')
instrument.engine.set_patch(1, pgm_no)
print (instrument.engine.get_patches())
for i in pgm.get_groups():
    print ("<group>", i.as_string())
    for j in i.get_children():
        print ("<region>", j.as_string())

print("Ready!")

while True:
    cbox.call_on_idle()
