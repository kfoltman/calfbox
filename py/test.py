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
    def verify_uuid(self, uuid, path = None):
        if path is None:
            self.assertEquals(cbox.GetThings(Document.uuid_cmd(uuid, "/status"), ['uuid'], []).uuid, uuid)
        else:
            self.assertEquals(cbox.GetThings(path + "/status", ['uuid'], []).uuid, uuid)
        
    def test_scene(self):
        scene = Document.get_scene()
        scene_status = scene.status()
        layer = scene_status.layers[0]
        self.verify_uuid(scene.uuid)
        self.verify_uuid(scene.uuid, "/scene")
        self.verify_uuid(layer.uuid)
        self.verify_uuid(layer.uuid, "/scene/layer/1")

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
        
        instr_uuid = layers[0].status().instrument_uuid
        iname = layers[0].status().instrument_name
        self.assertEquals(cbox.GetThings("/scene/instr/%s/status" % iname, ['uuid'], []).uuid, instr_uuid)
        
        scene.load_aux("piano_reverb")
        scene.delete_aux("piano_reverb")

    def test_rt(self):
        rt = Document.get_rt()
        self.assertEquals(cbox.GetThings(Document.uuid_cmd(rt.uuid, "/status"), ['uuid'], []).uuid, rt.uuid)

    def test_recorder_api(self):
        scene = Document.get_scene()
        layer = scene.status().layers[0]
        iname = layer.status().instrument_name
        self.assertEquals(cbox.GetThings("/scene/instr/%s/output/1/rec_dry/status" % iname, ['*handler'], []).handler, [])
        
        meter_uuid = cbox.GetThings("/new_meter", ['uuid'], []).uuid
        cbox.do_cmd('/scene/instr/%s/output/1/rec_dry/attach' % iname, None, [meter_uuid])
        self.assertEquals(cbox.GetThings("/scene/instr/%s/output/1/rec_dry/status" % iname, ['*handler'], []).handler, [[1, meter_uuid]])
        cbox.do_cmd('/scene/instr/%s/output/1/rec_dry/detach' % iname, None, [meter_uuid])
        self.assertEquals(cbox.GetThings("/scene/instr/%s/output/1/rec_dry/status" % iname, ['*handler'], []).handler, [])

        rec_uuid = cbox.GetThings("/new_recorder", ['uuid'], ['test.wav']).uuid
        cbox.do_cmd('/scene/instr/%s/output/1/rec_dry/attach' % iname, None, [rec_uuid])
        self.assertEquals(cbox.GetThings("/scene/instr/%s/output/1/rec_dry/status" % iname, ['*handler'], []).handler, [[1, rec_uuid]])
        cbox.do_cmd('/scene/instr/%s/output/1/rec_dry/detach' % iname, None, [rec_uuid])
        self.assertEquals(cbox.GetThings("/scene/instr/%s/output/1/rec_dry/status" % iname, ['*handler'], []).handler, [])
        self.assertTrue(os.path.exists('test.wav'))
        self.assertTrue(os.path.getsize('test.wav') < 512)

        rec_uuid = cbox.GetThings("/new_recorder", ['uuid'], ['test.wav']).uuid
        cbox.do_cmd('/scene/instr/%s/output/1/rec_dry/attach' % iname, None, [rec_uuid])
        self.assertEquals(cbox.GetThings("/scene/instr/%s/output/1/rec_dry/status" % iname, ['*handler'], []).handler, [[1, rec_uuid]])
        data = struct.unpack_from("512f", cbox.GetThings("/scene/render_stereo", ['data'], [512]).data)
        cbox.do_cmd('/scene/instr/%s/output/1/rec_dry/detach' % iname, None, [rec_uuid])
        self.assertEquals(cbox.GetThings("/scene/instr/%s/output/1/rec_dry/status" % iname, ['*handler'], []).handler, [])
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
