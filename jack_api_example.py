from calfbox import cbox
import time

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
uuid = cbox.JackIO.create_midi_output('drums')
cbox.JackIO.autoconnect_midi_output(uuid, '*alsa_pcm:.*')
cbox.JackIO.rename_midi_output(uuid, 'kettles')

uuid2 = cbox.JackIO.create_midi_output('violins')

status = cbox.JackIO.status()
print ("Before deleting, MIDI outputs: %s" % status.midi_output)

cbox.JackIO.delete_midi_output(uuid2)
status = cbox.JackIO.status()
print ("After deleting, MIDI outputs: %s" % status.midi_output)

print ("Physical audio inputs: %s" % (",".join(cbox.JackIO.get_ports(".*", cbox.JackIO.AUDIO_TYPE, cbox.JackIO.PORT_IS_SOURCE | cbox.JackIO.PORT_IS_PHYSICAL))))
print ("Physical audio outputs: %s" % (",".join(cbox.JackIO.get_ports(".*", cbox.JackIO.AUDIO_TYPE, cbox.JackIO.PORT_IS_SINK | cbox.JackIO.PORT_IS_PHYSICAL))))
print ("Physical MIDI inputs: %s" % (",".join(cbox.JackIO.get_ports(".*", cbox.JackIO.MIDI_TYPE, cbox.JackIO.PORT_IS_SOURCE | cbox.JackIO.PORT_IS_PHYSICAL))))
print ("Physical MIDI outputs: %s" % (",".join(cbox.JackIO.get_ports(".*", cbox.JackIO.MIDI_TYPE, cbox.JackIO.PORT_IS_SINK | cbox.JackIO.PORT_IS_PHYSICAL))))

inputs = cbox.JackIO.get_ports(".*", cbox.JackIO.AUDIO_TYPE, cbox.JackIO.PORT_IS_SOURCE | cbox.JackIO.PORT_IS_PHYSICAL)
outputs = cbox.JackIO.get_ports(".*", cbox.JackIO.AUDIO_TYPE, cbox.JackIO.PORT_IS_SINK | cbox.JackIO.PORT_IS_PHYSICAL)
cbox.JackIO.port_connect(inputs[0], outputs[0])
cbox.JackIO.port_connect(inputs[1], outputs[1])

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm_no = instrument.engine.get_unused_program()
pgm = instrument.engine.load_patch_from_file(pgm_no, 'synthbass.sfz', 'SynthBass')
instrument.engine.set_patch(1, pgm_no)
instrument.engine.set_patch(10, pgm_no)

song = Document.get_song()
track = song.add_track()
track.set_external_output(uuid)
print ("Track outputs to: %s:%s" % (client_name, track.status().external_output))
pattern = song.load_drum_pattern("pat1")
track.add_clip(0, 0, pattern.status().loop_end, pattern)
song.set_loop(0, pattern.status().loop_end)
song.update_playback()
cbox.Transport.play()

print("Ready!")

while True:
    events = cbox.get_new_events()
    if events:
        print (events)
    time.sleep(0.05)
