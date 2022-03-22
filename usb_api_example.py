#! /usr/bin/env python3
# -*- coding: utf-8 -*-

from calfbox import cbox

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine()

cbox.Config.add_section("drumpattern:pat1", """
title=Straight - Verse
beats=4
track1=bd
track2=sd
track3=hh
track4=ho
bd_note=c1
sd_note=d1
hh_note=f#1
ho_note=a#1
bd_trigger=9... .... 9.6. ....
sd_trigger=.... 9..5 .2.. 9...
hh_trigger=9353 7353 7353 73.3
ho_trigger=.... .... .... ..3.
""")

cbox.Config.set("io", "use_usb", 1)
cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

status = cbox.JackIO.status()
client_name = status.client_name
print ("Client type: %s" % status.client_type)
print ("Client name: %s" % client_name)
print ("Audio inputs: %d, outputs: %d" % (status.audio_inputs, status.audio_outputs))
print ("Period: %d frames" % (status.buffer_size))
print ("Sample rate: %d frames/sec" % (status.sample_rate))
print ("Output resolution: %d bits/sample" % (status.output_resolution))
print ("MIDI input devices: %s" % (status.midi_input))
#cbox.JackIO.create_midi_output('drums', 'system:midi_playback_1')

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm_no = instrument.engine.get_unused_program()
pgm = instrument.engine.load_patch_from_file(pgm_no, 'synthbass.sfz', 'SynthBass')
instrument.engine.set_patch(1, pgm_no)
instrument.engine.set_patch(10, pgm_no)

song = Document.get_song()
track = song.add_track()
pattern = song.load_drum_pattern("pat1")
track.add_clip(0, 0, pattern.status().loop_end, pattern)
song.set_loop(0, pattern.status().loop_end)
song.update_playback()
cbox.Transport.play()

print("Ready!")

while True:
    cbox.call_on_idle(cmd_dumper)
