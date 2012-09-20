import os
import sys
import struct
import time
import unittest

sys.path = [__file__[0 : __file__.rfind('/')]] + sys.path
sys.argv = [__file__]

import cbox

cbox.Document.dump()

class TestCbox(unittest.TestCase):
    def test_scene(self):
        scene_uuid = cbox.GetThings("/scene/get_uuid", ['uuid'], []).uuid
        layer_uuid = cbox.GetThings("/scene/status", ['layer'], []).layer[1]
        self.assertEquals(cbox.GetThings(cbox.Document.uuid_cmd(scene_uuid, "/status"), ['uuid'], []).uuid, scene_uuid)
        self.assertEquals(cbox.GetThings(cbox.Document.uuid_cmd(layer_uuid, "/status"), ['uuid'], []).uuid, layer_uuid)

        layers = cbox.GetThings("/scene/status", ['%layer'], []).layer
        self.assertEquals(len(layers), 1)
        self.assertEquals(layers[1], layer_uuid)
        
        instr_uuid = cbox.GetThings(cbox.Document.uuid_cmd(layer_uuid, "/status"), ['instrument_uuid'], []).instrument_uuid
        iname = cbox.GetThings(cbox.Document.uuid_cmd(layer_uuid, "/status"), ['instrument_name'], []).instrument_name
        self.assertEquals(cbox.GetThings("/scene/instr/%s/status" % iname, ['uuid'], []).uuid, instr_uuid)

    def test_rt(self):
        rt_uuid = cbox.GetThings("/rt/get_uuid", ['uuid'], []).uuid
        self.assertEquals(cbox.GetThings(cbox.Document.uuid_cmd(rt_uuid, "/status"), ['uuid'], []).uuid, rt_uuid)

    def test_recorder_api(self):
        layer_uuid = cbox.GetThings("/scene/status", ['layer'], []).layer[1]
        iname = cbox.GetThings(cbox.Document.uuid_cmd(layer_uuid, "/status"), ['instrument_name'], []).instrument_name
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
        tp = cbox.GetThings("/song/status", ["*track", "*pattern"], [])
        self.assertEqual(tp.track, [])
        self.assertEqual(tp.pattern, [])
        cbox.do_cmd("/play_drum_pattern", None, ['pat1'])
        tp = cbox.GetThings("/song/status", ["%track", "%pattern"], [])
        self.assertEqual(tp.track[1][0], 'Unnamed')
        self.assertEqual(tp.pattern[1][0], 'pat1')
        track_uuid = tp.track[1][2]
        
        cbox.do_cmd(cbox.Document.uuid_cmd(track_uuid, "/name"), None, ["Now named"])
        
        tp = cbox.GetThings("/song/status", ["%track", "%pattern"], [])
        self.assertEqual(tp.track[1][0], 'Now named')
        self.assertEqual(tp.pattern[1][0], 'pat1')
        pattern_uuid = tp.pattern[1][2]
        
        clips = cbox.GetThings(cbox.Document.uuid_cmd(track_uuid, "/status"), ["*clip"], []).clip
        self.assertEqual(clips[0][0:4], [0, 0, 192, pattern_uuid])
        clip1_uuid = clips[0][4]
        
        clip2_uuid = cbox.GetThings(cbox.Document.uuid_cmd(track_uuid, "/add_clip"), ["uuid"], [192, 96, 48, pattern_uuid]).uuid
        
        clip2_data = cbox.GetThings(cbox.Document.uuid_cmd(clip2_uuid, "/status"), ['pos', 'offset', 'length', 'pattern'], [])
        self.assertEqual(clip2_data.pos, 192)
        self.assertEqual(clip2_data.offset, 96)
        self.assertEqual(clip2_data.length, 48)
        self.assertEqual(clip2_data.pattern, pattern_uuid)

        clips = cbox.GetThings(cbox.Document.uuid_cmd(track_uuid, "/status"), ["*clip"], []).clip
        self.assertEqual(clips, [[0, 0, 192, pattern_uuid, clip1_uuid], [192, 96, 48, pattern_uuid, clip2_uuid]])

        cbox.do_cmd(cbox.Document.uuid_cmd(clip1_uuid, "/delete"), None, [])

        clips = cbox.GetThings(cbox.Document.uuid_cmd(track_uuid, "/status"), ["*clip"], []).clip
        self.assertEqual(clips, [[192, 96, 48, pattern_uuid, clip2_uuid]])
        
unittest.main()
