#! /usr/bin/env python3
# -*- coding: utf-8 -*-

from calfbox import cbox
import time

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine("") #empty string so cbox doesn't look for the .cboxrc file

cbox.Config.set("io", "use_usb", 0)
cbox.Config.set("io", "midi", "*.*")

cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

status = cbox.JackIO.status()
client_name = status.client_name
print ("Client name: %s" % client_name)
print ("Audio inputs: %d, outputs: %d" % (status.audio_inputs, status.audio_outputs))
print ("Sample rate: %d frames/sec" % (status.sample_rate))
print ("JACK period: %d frames" % (status.buffer_size))

left_dry = cbox.JackIO.create_audio_output('left_dry')
right_dry = cbox.JackIO.create_audio_output('right_dry')
left_wet = cbox.JackIO.create_audio_output('left_wet', '#1')
right_wet = cbox.JackIO.create_audio_output('right_wet', '#2')
router_dry = cbox.JackIO.create_audio_output_router(left_dry, right_dry)
assert type(router_dry) is cbox.DocRecorder
router_wet = cbox.JackIO.create_audio_output_router(left_wet, right_wet)
assert type(router_wet) is cbox.DocRecorder

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "tonewheel_organ").get_instrument()
instrument.get_output_slot(0).set_insert_engine("delay")
instrument.get_output_slot(0).rec_dry.attach(router_dry)
instrument.get_output_slot(0).rec_wet.attach(router_wet)
assert router_dry.uuid == instrument.get_output_slot(0).rec_dry.status().handler[0].uuid
assert router_wet.uuid == instrument.get_output_slot(0).rec_wet.status().handler[0].uuid
router_wet.set_gain(-3.0)
assert router_wet.status().gain == -3

print("Ready!")

while True:
    events = cbox.get_new_events()
    if events:
        print (events)
    time.sleep(0.05)
