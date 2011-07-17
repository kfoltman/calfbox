import cbox
from gui_tools import *

#################################################################################################################################

class EffectWindow(gtk.Window):
    def __init__(self, instrument, output, main_window, path):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_UTILITY)
        self.set_transient_for(main_window)
        self.main_window = main_window
        self.path = path
        self.vpath = cbox.VarPath(path)
        self.set_title("%s - %s" % (self.effect_name, instrument))
        if hasattr(self, 'params'):
            values = cbox.GetThings(self.path + "/status", [p.name for p in self.params], [])
            self.refreshers = []
            t = gtk.Table(2, len(self.params))
            for i in range(len(self.params)):
                p = self.params[i]
                t.attach(p.create_label(), 0, 1, i, i+1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
                widget, refresher = p.create_widget(self.vpath)
                refresher(values)
                t.attach(widget, 1, 2, i, i+1, gtk.EXPAND | gtk.FILL, gtk.SHRINK)
                self.refreshers.append(refresher)
            self.add(t)
        self.connect('delete_event', self.on_close)
        
    def create_param_table(self, cols, rows, values, extra_rows = 0):
        t = gtk.Table(4, rows + 1 + extra_rows)
        self.cols = cols
        self.table_refreshers = []
        for i in range(len(self.cols)):
            par = self.cols[i]
            t.attach(par.create_label(), i, i + 1, 0, 1, gtk.SHRINK | gtk.FILL)
            for j in range(rows):
                widget, refresher = par.create_widget(self.vpath.plus(None, j))
                t.attach(widget, i, i + 1, j + 1, j + 2, gtk.EXPAND | gtk.FILL)
                refresher(values)
                self.table_refreshers.append(refresher)
        return t
        
    def refresh(self):
        if hasattr(self, 'params'):
            values = cbox.GetThings(self.path + "/status", [p.name for p in self.params], [])
            for refresher in self.refreshers:
                refresher(values)
                
    def on_close(self, widget, event):
        self.main_window.on_effect_popup_close(self)

#################################################################################################################################

class PhaserWindow(EffectWindow):
    params = [
        MappedSliderRow("Center", "center_freq", LogMapper(100, 2000, freq_format)),
        SliderRow("Mod depth", "mod_depth", 0, 7200),
        SliderRow("Feedback", "fb_amt", -1, 1),
        MappedSliderRow("LFO frequency", "lfo_freq", lfo_freq_mapper),
        SliderRow("Stereo", "stereo_phase", 0, 360),
        SliderRow("Wet/dry", "wet_dry", 0, 1),
        IntSliderRow("Stages", "stages", 1, 12)
    ]
    effect_name = "Phaser"
    
class ChorusWindow(EffectWindow):
    params = [
        SliderRow("Min. delay", "min_delay", 1, 20),
        SliderRow("Mod depth", "mod_depth", 1, 20),
        MappedSliderRow("LFO frequency", "lfo_freq", lfo_freq_mapper),
        SliderRow("Stereo", "stereo_phase", 0, 360),
        SliderRow("Wet/dry", "wet_dry", 0, 1)
    ]
    effect_name = "Chorus"

class DelayWindow(EffectWindow):
    params = [
        SliderRow("Delay time (ms)", "time", 1, 1000),
        SliderRow("Feedback", "fb_amt", 0, 1),
        SliderRow("Wet/dry", "wet_dry", 0, 1)
    ]
    effect_name = "Delay"

class ReverbWindow(EffectWindow):
    params = [
        SliderRow("Decay time", "decay_time", 500, 5000),
        SliderRow("Dry amount", "dry_amt", -100, 12),
        SliderRow("Wet amount", "wet_amt", -100, 12),
        MappedSliderRow("Lowpass", "lowpass", LogMapper(300, 20000, freq_format)),
        MappedSliderRow("Highpass", "highpass", LogMapper(30, 2000, freq_format)),
        SliderRow("Diffusion", "diffusion", 0.2, 0.8)
    ]
    effect_name = "Reverb"

class ToneControlWindow(EffectWindow):
    params = [
        MappedSliderRow("Lowpass", "lowpass", LogMapper(300, 20000, freq_format)),
        MappedSliderRow("Highpass", "highpass", LogMapper(30, 2000, freq_format))
    ]
    effect_name = "Tone Control"

class CompressorWindow(EffectWindow):
    params = [
        SliderRow("Threshold", "threshold", -100, 12),
        MappedSliderRow("Ratio", "ratio", LogMapper(1, 100, "%0.2f")),
        MappedSliderRow("Attack", "attack", LogMapper(1, 1000, ms_format)),
        MappedSliderRow("Release", "release", LogMapper(1, 1000, ms_format)),
        SliderRow("Make-up gain", "makeup", -48, 48),
    ]
    effect_name = "Tone Control"

eq_cols = [
    CheckBoxRow("Active", "active"),
    MappedSliderRow("Center Freq", "center", filter_freq_mapper),
    MappedSliderRow("Filter Q", "q", LogMapper(0.01, 100, "%f")),
    SliderRow("Gain", "gain", -36, 36),
]

class EQWindow(EffectWindow):
    def __init__(self, instrument, output, main_window, path):
        EffectWindow.__init__(self, instrument, output, main_window, path)
        values = cbox.GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        self.add(self.create_param_table(eq_cols, 4, values))
    effect_name = "Equalizer"
        
class FBRWindow(EffectWindow):
    effect_name = "Feedback Reduction"
    
    def __init__(self, instrument, output, main_window, path):
        EffectWindow.__init__(self, instrument, output, main_window, path)
        values = cbox.GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        t = self.create_param_table(eq_cols, 16, values, 1)        
        self.add(t)
        self.ready_label = gtk.Label("-")
        t.attach(self.ready_label, 0, 2, 17, 18)
        set_timer(self, 100, self.update)
        sbutton = gtk.Button("_Start")
        sbutton.connect("clicked", lambda button, path: cbox.do_cmd(path + "/start", None, []), self.path)
        t.attach(sbutton, 2, 4, 17, 18)
        
    def refresh_table(self):
        values = cbox.GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        for refresher in self.table_refreshers:
            refresher(values)
        
    def update(self):
        values = cbox.GetThings(self.path + "/status", ["finished", "refresh"], [])
        if values.refresh:
            self.refresh_table()
        
        if values.finished > 0:
            self.ready_label.set_text("Ready")
        else:
            self.ready_label.set_text("Not Ready")
        return True

#################################################################################################################################

effect_engines = ['', 'phaser', 'reverb', 'chorus', 'feedback_reducer', 'tone_control', 'delay', 'parametric_eq', 'compressor']

effect_window_map = {
    'phaser': PhaserWindow,
    'chorus': ChorusWindow,
    'delay': DelayWindow,
    'reverb' : ReverbWindow,
    'feedback_reducer': FBRWindow,
    'parametric_eq': EQWindow,
    'tone_control': ToneControlWindow,
    'compressor': CompressorWindow,
}

