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

inputs = cbox.JackIO.get_ports(".*", cbox.JackIO.AUDIO_TYPE, cbox.JackIO.PORT_IS_SOURCE | cbox.JackIO.PORT_IS_PHYSICAL)
outputs = cbox.JackIO.get_ports(".*", cbox.JackIO.AUDIO_TYPE, cbox.JackIO.PORT_IS_SINK | cbox.JackIO.PORT_IS_PHYSICAL)

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm_no = instrument.engine.get_unused_program()
pgm = instrument.engine.load_patch_from_file(pgm_no, 'synthbass.sfz', 'SynthBass')
instrument.engine.set_patch(1, pgm_no)
instrument.engine.set_patch(10, pgm_no)

print ("Connecting")

uuid = cbox.JackIO.create_audio_output('noises')
router = cbox.JackIO.create_audio_output_router(uuid, uuid)
assert type(router) is cbox.DocRecorder
router2 = cbox.JackIO.create_audio_output_router(uuid, uuid)
assert type(router2) is cbox.DocRecorder
instrument.get_output_slot(0).rec_wet.attach(router)
instrument.get_output_slot(0).rec_wet.attach(router2)

exc = None
try:
    instrument.get_output_slot(0).rec_wet.attach(router2)
except Exception as e:
    exc = e
assert "Router already attached" in str(exc)

instrument.get_output_slot(0).rec_wet.detach(router2)

try:
    instrument.get_output_slot(0).rec_wet.detach(router2)
except Exception as e:
    exc = e
assert "Recorder is not attached" in str(exc)

router.delete()

print("Ready!")

while True:
    events = cbox.get_new_events()
    if events:
        print (events)
    time.sleep(0.05)
