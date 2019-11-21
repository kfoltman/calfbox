import os
import sys
import struct
import time
import unittest

sys.path = ["./py"] + sys.path

import cbox

global Document
Document = cbox.Document

def verify_region(region, has, has_not, full = False):
    values = set()
    values_dict = {}
    rtext = region.as_string() if not full else region.as_string_full()
    for pair in rtext.split(" "):
        if "=" in pair:
            key, value = pair.split("=")
            values.add(pair)
            values.add(key)
            values_dict[key] = value
    if 'genericmod' in rtext:
        print (rtext, "Expected:", has)
        assert False
    for i in has:
        if i not in values:
            print("Not found: %s, has: %s" % (i, rtext))
            assert False
    for i in has_not:
        if i in values:
            print("Found unwanted string: %s=%s" % (i, values_dict[i]))
            assert False

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()

npfs = instrument.engine.load_patch_from_string(0, '.', '', 'new_patch')
instrument.engine.set_patch(1, 0)

glb = npfs.get_global()
assert glb
assert glb.get_children()
master = glb.get_children()[0]

g1 = master.new_child()
g1.set_param("cutoff", "100")
g1.set_param("resonance", "6")
g1.set_param("fil_type", "lpf_4p")
g1.set_param("fileg_decay", "0.2")
g1.set_param("fileg_sustain", "10")
g1.set_param("fileg_depth", "5400")
g1.set_param("fileg_release", "10")
g1.set_param("ampeg_release", "0.1")
g1.set_param("amp_veltrack", "0")
g1.set_param("volume", "-12")
g1.set_param("fileg_depthcc14", "-5400")

def check_exception(param, value, substr):
    try:
        g1.set_param(param, value)
        assert False, "Exception with substring %s expected when setting %s to %s, none caught" % (substr, param, value)
    except Exception as e:
        assert substr in str(e), "Exception with substring %s expected when setting %s to %s, caught another one: %s" % (substr, param, value, str(e))

check_exception("cutoff", "bla", "correct numeric value")
check_exception("key", "bla", "valid note name")
check_exception("lochan", "bla", "correct integer value")
check_exception("lochan", "10.5", "correct integer value")
check_exception("offset", "bla", "correct unsigned integer value")
check_exception("offset", "10.5", "correct unsigned integer value")
check_exception("offset", "-1000", "correct unsigned integer value")
g1.set_param("locc5", "100")
check_exception("locc8", "110", "Conflicting controller")
check_exception("hicc8", "110", "Conflicting controller")

#g1.set_param("cutoff", "1000")
#g1.set_param("fillfo_freq", "4")
#g1.set_param("fillfo_depth", "2400")
#g1.set_param("fillfo_wave", "12")

r1 = g1.new_child()
r1.set_param("sample", "*saw")
r1.set_param("transpose", "0")
r1.set_param("tune", "5")
r1.set_param("gain_cc17", "12")

r2 = g1.new_child()
r2.set_param("sample", "*sqr")
r2.set_param("transpose", "12")
r2.set_param("gain_cc17", "-12")
verify_region(r2, ["sample=*sqr", "gain_cc17=-12", "transpose=12"], [])
r2.set_param("gain_cc17", "-24")
verify_region(r2, ["gain_cc17=-24"], ["gain_cc17=-12"])
r2.unset_param("gain_cc17")
verify_region(r2, ["sample=*sqr"], ["gain_cc17"])
r2.unset_param("transpose")
verify_region(r2, ["sample=*sqr"], ["transpose"])
r2.unset_param("sample")
verify_region(r2, [], ["transpose", "sample"])

params_to_test = [
    'lokey', 'hikey', 'lovel', 'hivel', 'key',
    'cutoff', 'pan', 'offset', 'tune', 'position', 'width',
    'amp_random', 'fil_random', 'pitch_random', 'delay_random',
    'pitch_veltrack', 'reloffset_veltrack', 'offset_veltrack',
    'delay_cc5', 'delay_cc10', 'reloffset_cc5', 'reloffset_cc10', 'offset_cc5', 'offset_cc10',
    'cutoff_cc1', "resonance_cc1", 'pitch_cc1', 'tonectl_cc1', 'gain_cc1',
    'cutoff_cc2', "resonance_cc2", 'pitch_cc2', 'tonectl_cc2', 'gain_cc2',
    'loop_start', 'loop_end',
    'ampeg_attack',
    'amplfo_depth', 'fillfo_depth',
    'fileg_vel2depth',
    'fileg_depthcc5', 'fillfo_depthcc8','amplfo_depthcc5',
    'amplfo_freqcc5', 'fillfo_freqcc10', 'pitchlfo_freqcc5',
    'cutoff_chanaft', 'resonance_chanaft',
    'cutoff_polyaft', 'resonance_polyaft',
    'amplfo_depthpolyaft', 'fillfo_depthpolyaft', 'pitchlfo_depthpolyaft', 
    'amplfo_freqpolyaft', 'fillfo_freqpolyaft', 'pitchlfo_freqpolyaft',
    'eq1_freqcc1', 'eq2_gaincc2', 'eq3_bwcc3',
    'fileg_vel2start', 'fileg_vel2delay', 'fileg_vel2attack', 'fileg_vel2hold', 'fileg_vel2decay', 'fileg_vel2sustain', 'fileg_vel2release',
    'fileg_startcc1', 'fileg_delaycc1', 'fileg_attackcc1', 'fileg_holdcc1', 'fileg_decaycc1', 'fileg_sustaincc1', 'fileg_releasecc1',
    'amp_velcurve_5', 'amp_velcurve_127',
    'locc5', 'hicc5',
    'on_locc8', 'on_hicc8',
    ]
for i in range(len(params_to_test)):
    param = params_to_test[0]
    print ("Trying %s" % param)
    rest = params_to_test[1:]
    value1, value2 = "100", "80"
    if 'key' in param:
        value1, value2 = "e1", "g1"
    r2.set_param(param, value1)
    verify_region(r2, ["%s=%s" % (param, value1)], rest)
    g1.set_param(param, value2)
    verify_region(g1, ["%s=%s" % (param, value2)], [])
    verify_region(r2, ["%s=%s" % (param, value1)], rest)
    verify_region(r2, ["%s=%s" % (param, value1)], [], full=True)
    r2.unset_param(param)
    verify_region(r2, [], params_to_test)
    
    verify_region(r2, ["%s=%s" % (param, value2)], [], full=True)
    g1.unset_param(param)
    verify_region(r2, [], ["%s=%s" % (param, value2)], full=True)
    
    params_to_test = params_to_test[1:] + params_to_test[0:1]

old_names = [
    ("hilev", "hivel"),
    ("lolev", "lovel"),
    ("loopstart", "loop_start"),
    ("loopend", "loop_end"),
    ("loopmode", "loop_mode", "one_shot", "loop_continuous"),
    ("bendup", "bend_up"),
    ("benddown", "bend_down"),
    ("offby", "off_by"),
]

for t in old_names:
    if len(t) == 2:
        old, new = t
        v1, v2 = "10", "20"
    else:
        old, new, v1, v2 = t
    print ("Trying %s" % old)
    r1.set_param(old, v1)
    verify_region(r1, ["%s=%s" % (new, v1)], [old])
    r1.set_param(old, v2)
    verify_region(r1, ["%s=%s" % (new, v2)], [old])
    r1.unset_param(old)
    verify_region(r1, [], [old, new])
    r1.set_param(new, v1)
    verify_region(r1, ["%s=%s" % (new, v1)], [old])
    r1.set_param(new, v2)
    verify_region(r1, ["%s=%s" % (new, v2)], [old])
    r1.unset_param(new)
    verify_region(r1, [], [old, new])
