# A primitive drum machine interface for Novation Nocturn.
#
# Usage:
#   - make sure that Novation Nocturn is connected *and* that the USB device
#     that corresponds to it can be opened by the current user
#   - create an ini file containing a scene with a single instrument using
#     a sampler or fluidsynth engine + some drum mappings in SFZ or SF2
#   - ensure that the ini file contains 7 drum patterns, pat1..pat7, these
#     can be copied from cboxrc-example
#   - user buttons 1..7 start drum patterns
#   - user button 8 stops the playback
#   - encoder knob 1 adjusts the volume
#   - mixer button exits the application

import math
import sys

sys.path = ["./py"] + sys.path

import nocturn
import cbox

quit = False

instr_name = cbox.Document.get_scene().status().instruments.keys()[0]

def clamp(val, min, max):
    if val < min:
        val = min
    elif val > max:
        val = max
    return val

class NovaBox:
    def __init__(self):
        self.nocturn = nocturn.Nocturn()
        self.cur_pattern = None
        self.handlers = {}
        self.handlers[83] = self.on_xfade_touch
        for i in range(7):
            self.handlers[112 + i] = lambda cmd, val: self.on_buttonN_press(cmd - 112) if val > 0 else None
        self.handlers[112 + 7] = lambda cmd, val: self.on_button8_press() if val > 0 else None
        self.handlers[64] = self.on_knob1_change
        self.handlers[65] = self.on_knob2_change
        self.handlers[127] = lambda cmd, val: self.on_mixer_press() if val > 0 else None

    def on_knob1_change(self, cmd, val):
        scene = cbox.Document.get_scene()
        instr = scene.status().instruments[instr_name][1]
        gain = instr.get_things('/output/1/status', ['gain']).gain
        if val > 63:
            val = -128 + val
        instr.cmd('/output/1/gain', None, gain + val * 0.5)

    def on_knob2_change(self, cmd, val):
        tempo = cbox.GetThings("/master/status", ['tempo'], []).tempo
        if val > 63:
            val = -128 + val
        tempo = clamp(tempo + val * 0.5, 30, 300)
        cbox.do_cmd('/master/set_tempo', None, [tempo])

    def on_buttonN_press(self, button):
        cbox.do_cmd("/master/stop", None, [])
        song = cbox.Document.get_song()
        song.loop_single_pattern(lambda: song.load_drum_pattern('pat%d' % (button + 1)))
        cbox.do_cmd("/master/seek_ppqn", None, [0])
        cbox.do_cmd("/master/play", None, [])
        self.cur_pattern = button

    def on_button8_press(self):
        self.cur_pattern = None
        cbox.do_cmd("/master/stop", None, [])
        
    def on_mixer_press(self):
        global quit
        quit = True

    def on_xfade_touch(self, cmd, val):
        if val > 0:
            print "Do not touch"

    def handler(self, cmd, val):
        if cmd in self.handlers:
            self.handlers[cmd](cmd, val)
            return

    def poll(self):
        self.nocturn.poll(self.handler)
        
    def update(self):
        scene = cbox.Document.get_scene()
        cmds = nocturn.NocturnCommands()
        master = cbox.GetThings("/master/status", ['playing'], [])
        for i in range(7):
            cmds.setModeButtonLight(i, self.cur_pattern == i)
        gain = scene.status().instruments[instr_name][1].get_things('/output/1/status', ['gain']).gain
        cmds.setEncoderMode(0, 0)
        cmds.setEncoderValue(0, clamp(int(gain * 2 + 64), 0, 127))
        
        cmds.setModeButtonLight(7, self.cur_pattern is None)
        self.nocturn.execute(cmds)

nb = NovaBox()
while not quit:
    nb.poll()
    nb.update()
nb.nocturn.reset()
