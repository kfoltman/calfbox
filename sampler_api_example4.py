from calfbox import cbox

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine()
cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm_no = instrument.engine.get_unused_program()
pgm = instrument.engine.load_patch_from_tar(pgm_no, 'sonatina.sbtar', 'Brass - Horn Solo.sfz', 'HornSolo')
pgm.add_control_init(7, 127)
pgm.add_control_init(10, 0)
print (pgm.load_file('Brass - Horn Solo.sfz'))
print (pgm.status())
print (list(pgm.get_control_inits()))
instrument.engine.set_patch(1, pgm_no)

print("Ready!")

while True:
    cbox.call_on_idle(cmd_dumper)
