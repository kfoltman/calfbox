import os
import sys
import struct
import time
import unittest

sys.path = [__file__[0 : __file__.rfind('/')]] + sys.path
sys.argv = [__file__]

import cbox

global Document
Document = cbox.Document

Document.dump()

class TestCbox(unittest.TestCase):
    def verify_uuid(self, uuid, class_name, path = None):
        self.assertEquals(cbox.GetThings(Document.uuid_cmd(uuid, "/get_class_name"), ['class_name'], []).class_name, class_name)
        if path is not None:
            self.assertEquals(cbox.GetThings(path + "/status", ['uuid'], []).uuid, uuid)
        self.assertEquals(cbox.GetThings(Document.uuid_cmd(uuid, "/status"), ['uuid'], []).uuid, uuid)
        
    def test_scene(self):
        scene = Document.get_scene()
        scene_status = scene.status()
        layer = scene_status.layers[0]
        self.verify_uuid(scene.uuid, "cbox_scene", "/scene")
        self.verify_uuid(layer.uuid, "cbox_layer", "/scene/layer/1")

        layers = scene.status().layers
        self.assertEquals(len(layers), 1)
        self.assertEquals(layers[0].uuid, layer.uuid)
        layers[0].set_consume(0)
        self.assertEquals(layers[0].status().consume, 0)
        layers[0].set_consume(1)
        self.assertEquals(layers[0].status().consume, 1)
        layers[0].set_enable(0)
        self.assertEquals(layers[0].status().enable, 0)
        layers[0].set_enable(1)
        self.assertEquals(layers[0].status().enable, 1)
        
        layer_status = layers[0].status()
        instr_uuid = layer_status.instrument_uuid
        iname = layer_status.instrument_name
        self.verify_uuid(instr_uuid, "cbox_instrument", "/scene/instr/%s" % iname)
        
        aux = scene.load_aux("piano_reverb")
        module = aux.get_slot_engine()
        self.verify_uuid(aux.uuid, "cbox_aux_bus", "/scene/aux/piano_reverb")
        scene.delete_aux("piano_reverb")

    def test_aux_scene(self):
        scene = Document.new_scene(44100, 1024)
        scene.add_instrument_layer("default")
        scene_status = scene.status()
        layer = scene_status.layers[0]
        self.verify_uuid(scene.uuid, "cbox_scene")
        self.verify_uuid(layer.uuid, "cbox_layer", scene.make_path("/layer/1"))

        layers = scene.status().layers
        self.assertEquals(len(layers), 1)
        self.assertEquals(layers[0].uuid, layer.uuid)
        layers[0].set_consume(0)
        self.assertEquals(layers[0].status().consume, 0)
        layers[0].set_consume(1)
        self.assertEquals(layers[0].status().consume, 1)
        layers[0].set_enable(0)
        self.assertEquals(layers[0].status().enable, 0)
        layers[0].set_enable(1)
        self.assertEquals(layers[0].status().enable, 1)
        
        layer_status = layers[0].status()
        instr_uuid = layer_status.instrument_uuid
        iname = layer_status.instrument_name
        self.verify_uuid(instr_uuid, "cbox_instrument", scene.make_path("/instr/%s" % iname))
        
        aux = scene.load_aux("piano_reverb")
        module = aux.get_slot_engine()
        self.verify_uuid(aux.uuid, "cbox_aux_bus", scene.make_path("/aux/piano_reverb"))
        scene.delete_aux("piano_reverb")

    def test_sampler_api(self):
        scene = Document.new_scene(44100, 1024)
        # XXXKF: I should add a method to add a layer using engine name alone, without
        # having to add a scene, layer or instrument in the INI
        scene.add_new_instrument_layer("temporary", "sampler")
        scene_status = scene.status()
        layer = scene_status.layers[0]
        self.verify_uuid(scene.uuid, "cbox_scene")
        self.verify_uuid(layer.uuid, "cbox_layer", scene.make_path("/layer/1"))
        instrument = layer.get_instrument()
        self.assertEquals(instrument.status().engine, "sampler")
        
        # XXXKF: this lack of stable Python representation of engines is annoying
        #instrument.cmd('/engine/load_patch_from_string', None, 1, '.', '<region> key=36 sample=impulse.wav cutoff=1000 <region> key=37 sample=impulse.wav cutoff=2000', 'test_sampler_api')
        instrument.cmd('/engine/load_patch_from_string', None, 0, '.', '<group> resonance=3 <region> unknown=123 key=36 sample=impulse.wav cutoff=1000 <region> key=37 sample=impulse.wav cutoff=2000', 'test_sampler_api')
        patches = instrument.get_things("/engine/patches", ["*patch"]).patch
        patches_dict = {}
        for patch in patches:
            patchid, patchname, patchuuid, patchchannelcount = patch
            self.verify_uuid(patchuuid, 'sampler_program')
            program = Document.map_uuid(patchuuid)
            self.verify_uuid(program.uuid, 'sampler_program')
            self.assertEquals(program.uuid, patchuuid)
            regions = program.get_things("/regions", ["*region"]).region
            patches_dict[patchid] = (patchname, len(regions))
            for region_uuid in regions:
                region_str = Document.map_uuid(region_uuid).as_string()
                print patchname, region_uuid, region_str
                if patchname == 'test_sampler_api':
                    self.assertTrue('impulse.wav' in region_str)
                    self.assertTrue('key=c' in region_str)
                    if 'key=c2' in region_str:
                        self.assertTrue('unknown=123' in region_str)
                        self.assertTrue('cutoff=1000' in region_str)
                    else:
                        self.assertFalse('unknown=123' in region_str)
                        self.assertTrue('cutoff=2000' in region_str)
        self.assertEquals(patches_dict, {0 : ('test_sampler_api', 2)})
        region = Document.map_uuid(region_uuid)
        group = Document.map_uuid(region.status().parent_group)
        self.assertTrue("resonance=3" in group.as_string())
        region.set_param("cutoff", 9000)
        self.assertTrue('cutoff=9000' in region.as_string())
        region.set_param("sample", 'test.wav')
        self.assertTrue('test.wav' in region.as_string())
        region.set_param("key", '12')
        self.assertTrue('key=c0' in region.as_string())
        print region.status()
        print group.as_string()
        print region.as_string()
        
    def test_rt(self):
        rt = Document.get_rt()
        self.assertEquals(cbox.GetThings(Document.uuid_cmd(rt.uuid, "/status"), ['uuid'], []).uuid, rt.uuid)

    def test_recorder_api(self):
        scene = Document.get_scene()
        layer = scene.status().layers[0]
        instr = Document.map_uuid(layer.status().instrument_uuid)
        self.assertEquals(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])
        
        meter_uuid = cbox.GetThings("/new_meter", ['uuid'], []).uuid
        instr.cmd('/output/1/rec_dry/attach', None, meter_uuid)
        self.assertEquals(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [meter_uuid])
        instr.cmd('/output/1/rec_dry/detach', None, meter_uuid)
        self.assertEquals(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])

        rec_uuid = cbox.GetThings("/new_recorder", ['uuid'], ['test.wav']).uuid
        instr.cmd('/output/1/rec_dry/attach', None, rec_uuid)
        self.assertEquals(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [rec_uuid])
        instr.cmd('/output/1/rec_dry/detach', None, rec_uuid)
        self.assertEquals(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])
        self.assertTrue(os.path.exists('test.wav'))
        self.assertTrue(os.path.getsize('test.wav') < 512)

        rec_uuid = cbox.GetThings("/new_recorder", ['uuid'], ['test.wav']).uuid
        instr.cmd('/output/1/rec_dry/attach', None, rec_uuid)
        self.assertEquals(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [rec_uuid])
        data = struct.unpack_from("512f", cbox.GetThings("/scene/render_stereo", ['data'], [512]).data)
        instr.cmd('/output/1/rec_dry/detach', None, rec_uuid)
        self.assertEquals(instr.get_things("/output/1/rec_dry/status", ['*handler']).handler, [])
        self.assertTrue(os.path.exists('test.wav'))
        self.assertTrue(os.path.getsize('test.wav') > 512 * 4 * 2)
        
    def test_song(self):
        song = Document.get_song()
        tp = song.status()
        self.assertEqual(tp.tracks, [])
        self.assertEqual(tp.patterns, [])
        
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
        
unittest.main()
