#! /usr/bin/env python3
# -*- coding: utf-8 -*-

from calfbox import cbox

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine("") #empty string so cbox doesn't look for the .cboxrc file
cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm_no = instrument.engine.get_unused_program()
pgm = instrument.engine.load_patch_from_file(pgm_no, 'synthbass.sfz', 'SynthBass')

# This is not needed, it's here to test for a bug where reloading a program at
# the same slot didn't return the newly loaded program object.
pgm = instrument.engine.load_patch_from_file(pgm_no, 'synthbass.sfz', 'SynthBass')
assert pgm

instrument.engine.set_patch(1, pgm_no)
print (instrument.engine.get_patches())
print (instrument.get_output_slot(0))
print (instrument.get_output_slot(0).status())
instrument.get_output_slot(0).set_insert_engine("reverb")
print (instrument.get_output_slot(0).status())
instrument.get_output_slot(0).engine.cmd("/wet_amt", None, 1.0)
for i in pgm.get_global().get_children()[0].get_children():
    print ("<group>", i.as_string())
    for j in i.get_children():
        print ("<region>", j.as_string())

print("Ready!")

while True:
    cbox.call_on_idle(cmd_dumper)
