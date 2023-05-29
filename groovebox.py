from calfbox import cbox
import glob
import os
import random
import sys

from PyQt5.QtCore import *
from PyQt5.QtGui import *
from PyQt5.QtWidgets import *

global Document
Document = cbox.Document

scene = Document.get_scene()
scene.clear()

class SampleBank:
    def __init__(self, name, pattern):
        self.name = name
        self.files = sorted(glob.glob(pattern))

path = "samples/ProducerSpot-House-Drums/"

banks = [
    SampleBank('Kicks', path + "Kicks/House Kick *.wav"),
    SampleBank('Snares', path + "Snares & Rims/House Snare *.wav"),
    SampleBank('Rims', path + "Snares & Rims/House Rim *.wav"),
    SampleBank('Hihats', path + "Hats/House HiHat *.wav"),
    SampleBank('Cymbals', path + "Cymbals/House Cymbal *.wav"),
    SampleBank('Claps', path + "Claps/House Clap *.wav"),
    SampleBank('Toms', path + "Toms/House Tom *.wav"),
    SampleBank('Percussions', path + "Percussions/House Percussion *.wav"),
]

NUM_PADS = 16

class OneShotPad:
    def __init__(self, family, variant, exclusion=None):
        self.set_sound(family, variant)
        self.exclusion = exclusion
    def set_sound(self, family, variant):
        self.family = max(0, min(len(banks) - 1, family))
        #self.variant = min(max(0, variant), len(banks[family].files) - 1)
        self.variant = max(0, min(variant, len(banks[family].files) - 1))
        self.waveform = banks[self.family].files[self.variant]
    def set_patch_params(self, region):
        region.set_param("sample", self.waveform)
        region.set_param("loop_mode", "one_shot")
        exclusion = self.exclusion or 0
        region.set_param("off_by", exclusion)
        region.set_param("group", exclusion)

class PadSampler:
    def __init__(self, scene):
        self.pads = [OneShotPad(0, 0) for _ in range(NUM_PADS)]
        self.instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
        self.pgm_no = self.instrument.engine.get_unused_program()
        self.patch = self.instrument.engine.load_patch_from_string(self.pgm_no, '.', '', 'pad_sounds')
        self.patch_global = self.patch.get_global()
        self.patch_master = self.patch_global.new_child()
        self.patch_group = self.patch_master.new_child()
        self.regions = [self.patch_group.new_child() for i in range(NUM_PADS)]
        for i, region in enumerate(self.regions):
            region.set_param("key", 36 + i)
    def reloadSounds(self):
        for i, pad in enumerate(self.pads):
            pad.set_patch_params(self.regions[i])

sampler = PadSampler(scene)

pad_defaults = [
    (0, 5), (1, 7), (3, 3), (3, 5),
    (2, 5), (5, 3), (7, 25), (4, 7),
    (6, 5), (6, 6), (6, 7), (4, 14),
    (7, 9), (7, 10), (7, 11), (7, 47),
]

for i in range(16):
    sampler.pads[i] = OneShotPad(pad_defaults[i][0], pad_defaults[i][1] - 1)

sampler.reloadSounds()
    
class PadButton(QPushButton):
    def __init__(self, sampler, pad_id):
        QPushButton.__init__(self)
        self.sampler = sampler
        self.pad_id = pad_id
        size = 144
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.setMinimumSize(size, size)
        self.setMaximumSize(size, size)
        self.setMaximumSize(size, size)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        self.updateText()
        self.pressed.connect(self.onClicked)
        self.customContextMenuRequested.connect(self.onContextMenu)
    def pad(self):
        return self.sampler.pads[self.pad_id]
    def updateText(self):
        self.setText(self.pad().waveform.split("/")[-1].replace(".wav", "").replace("House ", ""))
    def onClicked(self):
        cbox.send_midi_event(0x99, 36 + self.pad_id, 127)
    def onContextMenu(self, pt):
        pt = self.mapToGlobal(pt)
        action = menu.exec_(pt)
        if action is None:
            return
        self.setSound(action.data() >> 16, action.data() & 0xFFFF)
    def setSound(self, family, variant):
        self.pad().set_sound(family, variant)
        self.sampler.reloadSounds()
        self.updateText()
    def wheelEvent(self, ev):
        pad = self.pad()
        delta = ev.angleDelta().y()
        if delta > 0:
            self.setSound(pad.family, pad.variant + 1)
        elif delta < 0:
            self.setSound(pad.family, pad.variant - 1)

class AtomController:
    def __init__(self):
        self.uuid_atom = cbox.JackIO.create_midi_output('atom_control')
        cbox.JackIO.autoconnect_midi_output(self.uuid_atom, 'system:midi_playback_1')
        # Set native mode
        cbox.send_midi_event(0x8F, 0, 127, output=self.uuid_atom)
    def set_pad_light(self, pad, light):
        cbox.send_midi_event(0x90, 36 + pad, 127 if light else 0, output=self.uuid_atom)

keyboard_keys = list(map(ord, "ZXCVASDFQWER1234"))

class CentralWidget(QWidget):
    pass

atom = AtomController()

class MainWindow(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)
        self.setWindowTitle("Groovebox")
        self.idleTimer = self.startTimer(5)
        self.last_event_time = None
        self.last_pad = 0
        atom.set_pad_light(self.last_pad, True)
        widget = CentralWidget()
        self.setCentralWidget(widget)
        grid = QGridLayout(widget)
        self.buttons = []
        for i in range(16):
            button = PadButton(sampler, i)
            grid.addWidget(button, 3 - (i >> 2), i & 3)
            self.buttons.append(button)
        self.centralWidget().setFocus()
    def timerEvent(self, event):
        if event.timerId() == self.idleTimer:
            for event in cbox.get_new_events():
                self.processCalfboxEvent(*event)
            return
        QMainWindow.timerEvent(self, event)
    def processCalfboxEvent(self, cmd, fb, data):
        if cmd == '/io/midi/event_time_samples':
            self.last_event_time = data[0]
        elif cmd == '/io/midi/simple_event':
            print (cmd, data)
            if data[0] >= 0x90 and data[0] <= 0x9F:
                if (data[1] >= 36 and data[1] < 36 + NUM_PADS) and data[2]:
                    self.changePad(data[1] - 36)
            elif data[0] >= 0xB0 and data[0] <= 0xBF:
                button = self.buttons[self.last_pad]
                pad = button.pad()
                if data[1] == 87 and data[2] == 127: # Up
                    button.setSound(pad.family, pad.variant + 1)
                elif data[1] == 89 and data[2] == 127: # Down
                    button.setSound(pad.family, pad.variant - 1)
                if data[1] == 90 and data[2] == 127: # Left
                    button.setSound(pad.family - 1, 0)
                elif data[1] == 102 and data[2] == 127: # Right
                    button.setSound(pad.family + 1, 0)
    def changePad(self, pad):
        atom.set_pad_light(self.last_pad, False)
        self.last_pad = pad
        atom.set_pad_light(self.last_pad, True)
    def keyPressEvent(self, event):
        try:
            pos = keyboard_keys.index(event.key())
        except ValueError:
            return
        cbox.send_midi_event(0x99, 36 + pos, 127)
        self.changePad(pos)


app = QApplication(sys.argv)

menu = QMenu()
for bankid, bank in enumerate(banks):
    submenu = menu.addMenu(bank.name)
    for i, filename in enumerate(bank.files):
        action = submenu.addAction(os.path.basename(filename))
        action.setData((bankid << 16) | i)

win = MainWindow()
win.show()

print("Ready!")

app.exec_()

atom.set_pad_light(win.last_pad, False)
