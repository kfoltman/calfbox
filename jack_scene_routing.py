#! /usr/bin/env python3
# -*- coding: utf-8 -*-

from calfbox import cbox
import time

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine("") #empty string so cbox doesn't look for the .cboxrc file

cbox.Config.set("io", "use_usb", 0)
cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

status = cbox.JackIO.status()
client_name = status.client_name
print ("Client name: %s" % client_name)
print ("Audio inputs: %d, outputs: %d" % (status.audio_inputs, status.audio_outputs))
print ("Sample rate: %d frames/sec" % (status.sample_rate))
print ("JACK period: %d frames" % (status.buffer_size))

uuid_ext1 = cbox.JackIO.create_midi_output('ext1')
uuid_ext2 = cbox.JackIO.create_midi_output('ext2')

scene = Document.get_scene()
scene.clear()
layer = scene.add_new_midi_layer(uuid_ext2)
#layer.set_external_output(uuid_ext1)

print("Ready!")

while True:
    events = cbox.get_new_events()
    if events:
        print (events)
    time.sleep(0.05)
