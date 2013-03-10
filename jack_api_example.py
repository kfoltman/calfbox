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

cbox.Config.set("io", "use_usb", 0)
cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

client_name = cbox.GetThings("/io/status", ['client_name'], []).client_name
print ("Client name: %s" % client_name)
cbox.do_cmd("/io/create_midi_output", None, ['drums'])

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm_no = instrument.engine.get_unused_program()
pgm = instrument.engine.load_patch_from_file(pgm_no, 'synthbass.sfz', 'SynthBass')
instrument.engine.set_patch(1, pgm_no)
instrument.engine.set_patch(10, pgm_no)

song = Document.get_song()
track = song.add_track()
track.set_external_output("drums")
print ("Track outputs to: %s:%s" % (client_name, track.status().external_output))
pattern = song.load_drum_pattern("pat1")
track.add_clip(0, 0, pattern.status().loop_end, pattern)
song.set_loop(0, pattern.status().loop_end)
song.update_playback()
cbox.Transport.play()

print("Ready!")

while True:
    cbox.call_on_idle(cmd_dumper)
