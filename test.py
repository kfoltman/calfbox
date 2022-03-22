import os
import sys
import struct
import time
import unittest

# This is for locale testing
from gi.repository import GObject, Gdk, Gtk

from calfbox import cbox
cbox.init_engine("")
cbox.start_noaudio(44100)

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
cbox.Config.add_section("fxpreset:piano_reverb", """
engine=reverb
""")
cbox.Config.add_section("instrument:vintage", """
engine=sampler
""")

global Document
Document = cbox.Document

Document.dump()

class TestCbox(unittest.TestCase):
    def verify_uuid(self, uuid, class_name, path = None):
        self.assertEqual(cbox.GetThings(Document.uuid_cmd(uuid, "/get_class_name"), ['class_name'], []).class_name, class_name)
        if path is not None:
            self.assertEqual(cbox.GetThings(path + "/status", ['uuid'], []).uuid, uuid)
        self.assertEqual(cbox.GetThings(Document.uuid_cmd(uuid, "/status"), ['uuid'], []).uuid, uuid)

    def test_scene(self):
        scene = Document.get_scene()
        self.assertEqual(Document.get_engine().status().scenes[0], scene)

        scene.clear()
        scene.add_new_instrument_layer("test_instr", "sampler")

        scene_status = scene.status()
        layer = scene_status.layers[0]
        self.verify_uuid(scene.uuid, "cbox_scene", "/scene")
        self.verify_uuid(layer.uuid, "cbox_layer", "/scene/layer/1")

        layers = scene.status().layers
        self.assertEqual(len(layers), 1)
        self.assertEqual(layers[0].uuid, layer.uuid)
        layers[0].set_consume(0)
        self.assertEqual(layers[0].status().consume, 0)
        layers[0].set_consume(1)
        self.assertEqual(layers[0].status().consume, 1)
        layers[0].set_enable(0)
        self.assertEqual(layers[0].status().enable, 0)
        layers[0].set_enable(1)
        self.assertEqual(layers[0].status().enable, 1)

        layer_status = layers[0].status()
        instr_uuid = layer_status.instrument.uuid
        iname = layer_status.instrument_name
        self.assertEqual(iname, 'test_instr')
        self.verify_uuid(instr_uuid, "cbox_instrument", "/scene/instr/%s" % iname)

        aux = scene.load_aux("piano_reverb")
        module = aux.slot.engine
        self.verify_uuid(aux.uuid, "cbox_aux_bus", "/scene/aux/piano_reverb")
        scene.delete_aux("piano_reverb")

    def test_aux_scene(self):
        engine = Document.new_engine(44100, 1024)
        scene = engine.new_scene()
        self.assertEqual(engine.status().scenes[0], scene)
        scene.add_instrument_layer("vintage")
        scene_status = scene.status()
        layer = scene_status.layers[0]
        self.verify_uuid(scene.uuid, "cbox_scene")
        self.verify_uuid(layer.uuid, "cbox_layer", scene.make_path("/layer/1"))

        layers = scene.status().layers
        self.assertEqual(len(layers), 1)
        self.assertEqual(layers[0].uuid, layer.uuid)
        layers[0].set_consume(0)
        self.assertEqual(layers[0].status().consume, 0)
        layers[0].set_consume(1)
        self.assertEqual(layers[0].status().consume, 1)
        layers[0].set_enable(0)
        self.assertEqual(layers[0].status().enable, 0)
        layers[0].set_enable(1)
        self.assertEqual(layers[0].status().enable, 1)

        layer_status = layers[0].status()
        instr_uuid = layer_status.instrument.uuid
        iname = layer_status.instrument_name
        self.verify_uuid(instr_uuid, "cbox_instrument", scene.make_path("/instr/%s" % iname))

        aux = scene.load_aux("piano_reverb")
        module = aux.slot.engine
        self.verify_uuid(aux.uuid, "cbox_aux_bus", scene.make_path("/aux/piano_reverb"))
        scene.delete_aux("piano_reverb")
        scene2 = engine.new_scene()
        with self.assertRaises(Exception) as context:
            layer_status.instrument.move_to(scene2, 1)
        self.assertEqual(str(context.exception), "Invalid position 2 (valid are 1..1 or 0 for append)")
        layer_status.instrument.move_to(scene2, 0)

        layers = scene.status().layers
        self.assertEqual(len(layers), 0)
        layers = scene2.status().layers
        self.assertEqual(len(layers), 1)
        scene.add_instrument_layer("vintage")
        with self.assertRaises(Exception) as context:
            layer_status.instrument.move_to(scene, 0)
        self.assertEqual(str(context.exception), "Instrument 'vintage' already exists in target scene")

    def test_sampler_api(self):
        engine = Document.new_engine(44100, 1024)
        scene = engine.new_scene()
        scene.add_new_instrument_layer("temporary", "sampler")
        scene_status = scene.status()
        layer = scene_status.layers[0]
        self.verify_uuid(scene.uuid, "cbox_scene")
        self.verify_uuid(layer.uuid, "cbox_layer", scene.make_path("/layer/1"))
        instrument = layer.get_instrument()
        self.assertEqual(instrument.status().engine, "sampler")

        program0 = instrument.engine.load_patch_from_file(0, 'synthbass.sfz', 'test_sampler_sfz_loader')
        self.assertNotEqual(program0, None)
        self.assertEqual(program0.status().in_use, 16)
        program1 = instrument.engine.load_patch_from_string(0, '.', '<group> resonance=3 <region> unknown=123 key=36 sample=impulse.wav cutoff=1000 <region> key=37 cutoff=2000 sample=impulse.wav ', 'test_sfz_parser_trailing_spaces')
        self.assertNotEqual(program1, None)
        self.assertEqual(program1.status().in_use, 16)
        self.assertEqual(program1.status().name, 'test_sfz_parser_trailing_spaces')
        self.assertRegex(program1.get_regions()[0].as_string(), 'sample=.*impulse\.wav')
        program2 = instrument.engine.load_patch_from_string(0, '.', '<group> resonance=3 <region> unknown=123 key=36 sample=impulse.wav cutoff=1000.5 <region> key=37 sample=impulse.wav cutoff=2000', 'test_sampler_api')
        self.assertNotEqual(program2, None)
        self.assertEqual(program2.status().in_use, 16)
        try:
            program1.status()
            self.assertTrue(False)
        except Exception as e:
            self.assertTrue('UUID not found' in str(e))
        patches = instrument.engine.get_patches()
        patches_dict = {}
        self.assertEqual(len(patches), 1)
        for (patchid, patchdata) in patches.items():
            patchname, program, patchchannelcount = patchdata
            self.verify_uuid(program.uuid, 'sampler_program')
            self.assertEqual(program.status().program_no, patchid)
            self.assertEqual(program.status().name, 'test_sampler_api')
            self.assertEqual(program.status().sample_dir, '.')
            self.assertEqual(program.status().program_no, 0)
            self.assertEqual(program.status().in_use, 16)
            instrument.engine.set_patch(1, 0)
            self.assertEqual(program.status().in_use, 16)
            instrument.engine.set_patch(2, 0)
            self.assertEqual(program.status().in_use, 16)
            regions = program.get_regions()
            patches_dict[patchid] = (patchname, len(regions))
            for region in regions:
                region_str = Document.map_uuid(region.uuid).as_string()
                print (patchname, region.uuid, region_str)
                if patchname == 'test_sampler_api':
                    self.assertTrue('impulse.wav' in region_str)
                    self.assertTrue('key=c' in region_str)
                    if 'key=c2' in region_str:
                        self.assertTrue('unknown=123' in region_str)
                        self.assertTrue('cutoff=1000.5' in region_str)
                    else:
                        self.assertFalse('unknown=123' in region_str)
                        self.assertTrue('cutoff=2000' in region_str)
            program.add_control_init(11, 64)
            self.assertTrue((11,64) in program.get_control_inits())
            program.delete_control_init(11, 0)
            program.add_control_init(11, 0)
            program.add_control_init(11, 64)
            self.assertTrue((11,0) in program.get_control_inits())
            self.assertTrue((11,64) in program.get_control_inits())
            program.delete_control_init(11, 0)
            self.assertTrue((11,0) not in program.get_control_inits())
            self.assertTrue((11,64) in program.get_control_inits())
            program.delete_control_init(11, 0)
            self.assertTrue((11,0) not in program.get_control_inits())
            self.assertTrue((11,64) not in program.get_control_inits())
            program.add_control_init(11, 0)
            program.add_control_init(11, 64)
            program.delete_control_init(11, -1)
            self.assertTrue((11,0) not in program.get_control_inits())
            self.assertTrue((11,64) not in program.get_control_inits())
        self.assertEqual(patches_dict, {0 : ('test_sampler_api', 2)})
        group = region.status().parent_group
        self.assertTrue("resonance=3" in group.as_string())
        region.set_param("cutoff", 9000)
        self.assertTrue('cutoff=9000' in region.as_string())
        region.set_param("sample", 'test.wav')
        self.assertTrue('test.wav' in region.as_string())
        region.set_param("key", '12')
        self.assertTrue('key=c0' in region.as_string())
        print (region.status())
        print (group.as_string())
        print (region.as_string())
        print ("Engine:", instrument.engine)
        print ("Patches:", instrument.engine.get_patches())
        program3 = program2.clone_to(instrument, 1)
        print ("Program 1")
        print (program2.status(), program2)
        print (program2.get_groups())
        print ("Program 2")
        print (program3.status(), program3)
        print (program3.get_groups())
        print (instrument.engine.get_patches())
        program3.delete()

    def test_rt(self):
        rt = Document.get_rt()
        self.assertEqual(cbox.GetThings(Document.uuid_cmd(rt.uuid, "/status"), ['uuid'], []).uuid, rt.uuid)

    def test_recorder_api(self):
        engine = Document.new_engine(44100, 512)
        scene = engine.new_scene()
        scene.add_new_instrument_layer("temporary", "sampler")
        layer = scene.status().layers[0]
        instr = layer.status().instrument
        self.assertEqual(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])

        meter_uuid = cbox.GetThings("/new_meter", ['uuid'], []).uuid
        instr.cmd('/output/1/rec_dry/attach', None, meter_uuid)
        self.assertEqual(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [meter_uuid])
        instr.cmd('/output/1/rec_dry/detach', None, meter_uuid)
        self.assertEqual(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])
        if os.path.exists("test.wav"):
            os.unlink('test.wav')

        rec = engine.new_recorder('test.wav')
        self.assertEqual(rec.status().filename, 'test.wav')
        rec_uuid = rec.uuid
        instr.cmd('/output/1/rec_dry/attach', None, rec_uuid)
        self.assertEqual(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [rec_uuid])
        instr.cmd('/output/1/rec_dry/detach', None, rec_uuid)
        self.assertEqual(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])
        self.assertTrue(os.path.exists('test.wav'))
        self.assertTrue(os.path.getsize('test.wav') < 512)
        os.unlink('test.wav')

        rec = engine.new_recorder('test.wav')
        self.assertEqual(rec.status().filename, 'test.wav')
        rec_uuid = rec.uuid
        instr.cmd('/output/1/rec_dry/attach', None, rec_uuid)
        self.assertEqual(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [rec_uuid])
        data = struct.unpack_from("512f", engine.render_stereo(512))
        instr.cmd('/output/1/rec_dry/detach', None, rec_uuid)
        self.assertEqual(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])
        rec.delete()
        self.assertTrue(os.path.exists('test.wav'))
        self.assertTrue(os.path.getsize('test.wav') > 512 * 4 * 2)

    def test_song(self):
        song = Document.get_song()
        song.clear()
        tp = song.status()
        self.assertEqual(tp.tracks, [])
        self.assertEqual(tp.patterns, [])
        self.assertEqual(tp.mtis, [])

        track = song.add_track()
        pattern = song.load_drum_pattern('pat1')
        track.add_clip(0, 0, 192, pattern)

        song = Document.get_song()
        tp = song.status()
        self.assertEqual(tp.tracks[0].name, 'Unnamed')
        self.assertEqual(tp.patterns[0].name, 'pat1')
        track = tp.tracks[0].track
        pattern = tp.patterns[0].pattern

        track.set_name("Now named")
        self.assertEqual(track.status().name, 'Now named')
        pattern.set_name("pat1alt")
        self.assertEqual(pattern.status().name, 'pat1alt')

        tp = song.status()
        self.assertEqual(tp.tracks[0].name, 'Now named')
        self.assertEqual(tp.patterns[0].name, 'pat1alt')

        clips = track.status().clips
        self.assertEqual(clips[0].pos, 0)
        self.assertEqual(clips[0].offset, 0)
        self.assertEqual(clips[0].length, 192)
        self.assertEqual(clips[0].pattern, pattern)
        clip1 = clips[0].clip

        clip2 = track.add_clip(192, 96, 48, pattern)

        clip2_data = clip2.status()
        self.assertEqual(clip2_data.pos, 192)
        self.assertEqual(clip2_data.offset, 96)
        self.assertEqual(clip2_data.length, 48)
        self.assertEqual(clip2_data.pattern, pattern)

        clips = track.status().clips
        self.assertEqual(clips, [cbox.ClipItem(0, 0, 192, pattern.uuid, clip1.uuid), cbox.ClipItem(192, 96, 48, pattern.uuid, clip2.uuid)])

        clip1.delete()

        clips = track.status().clips
        self.assertEqual(clips, [cbox.ClipItem(192, 96, 48, pattern.uuid, clip2.uuid)])

    def test_mti(self):
        MtiItem = cbox.MtiItem
        song = Document.get_song()
        song.clear()
        tp = song.status()
        self.assertEqual(tp.tracks, [])
        self.assertEqual(tp.patterns, [])
        self.assertEqual(tp.mtis, [])
        song.set_mti(0, 120.0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0)])
        song.set_mti(60, 150.0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0), MtiItem(60, 150.0, 0, 0)])
        song.set_mti(90, 180.0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0), MtiItem(60, 150.0, 0, 0), MtiItem(90, 180.0, 0, 0)])
        song.set_mti(60, 180.0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0), MtiItem(60, 180.0, 0, 0), MtiItem(90, 180.0, 0, 0)])
        song.set_mti(65, 210.0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0), MtiItem(60, 180.0, 0, 0), MtiItem(65, 210.0, 0, 0), MtiItem(90, 180.0, 0, 0)])

        song.set_mti(60, 0.0, 0, 0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0), MtiItem(65, 210.0, 0, 0), MtiItem(90, 180.0, 0, 0)])
        song.set_mti(65, 0.0, 0, 0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0), MtiItem(90, 180.0, 0, 0)])
        song.set_mti(68, 0.0, 0, 0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 120.0, 0, 0), MtiItem(90, 180.0, 0, 0)])
        song.set_mti(0, 0.0, 0, 0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 0, 0, 0), MtiItem(90, 180.0, 0, 0)])
        song.set_mti(90, 0.0, 0, 0)
        self.assertEqual(song.status().mtis, [MtiItem(0, 0, 0, 0)])

    def test_error(self):
        thrown = False
        try:
            Document.get_scene().cmd('transpose', None, cbox)
        except ValueError as ve:
            self.assertTrue("class 'module'" in str(ve))
            thrown = True
        self.assertTrue(thrown)

unittest.main()

cbox.stop_audio()
cbox.shutdown_engine()
