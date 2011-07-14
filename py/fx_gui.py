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
        self.set_title("%s - %s" % (self.effect_name, instrument))
        if hasattr(self, 'params'):
            values = cbox.GetThings(self.path + "/status", [p.name for p in self.params], [])
            self.refreshers = []
            t = gtk.Table(2, len(self.params))
            for i in range(len(self.params)):
                p = self.params[i]
                self.refreshers.append((p, p.add_row(t, i, self.path, values)))
            self.add(t)
        self.connect('delete_event', self.on_close)
        
    def create_param_table(self, cols, rows, values, extra_rows = 0):
        t = gtk.Table(4, rows + 1 + extra_rows)
        self.cols = eq_cols
        self.table_widgets = {}
        for i in range(len(self.cols)):
            par = self.cols[i]
            t.attach(bold_label(par[0], halign=0.5), i, i + 1, 0, 1, gtk.SHRINK | gtk.FILL)
            for j in range(rows):
                value = getattr(values, par[3])[j]
                if par[4] == 'slider':
                    adj = gtk.Adjustment(value, par[1], par[2], 1, 6, 0)
                    adj.connect("value_changed", adjustment_changed_float, self.path + "/" + par[3], int(j))
                    widget = standard_hslider(adj)
                elif par[4] == 'log_slider':
                    mapper = LogMapper(par[1], par[2], "%f")
                    adj = gtk.Adjustment(mapper.unmap(value), 0, 100, 1, 6, 0)
                    adj.connect("value_changed", adjustment_changed_float_mapped, self.path + "/" + par[3], mapper, int(j))
                    widget = standard_mapped_hslider(adj, mapper)
                else:
                    widget = gtk.CheckButton(par[0])
                    widget.set_active(value > 0)
                    widget.connect("clicked", checkbox_changed_bool, self.path + "/" + par[3], int(j))
                t.attach(widget, i, i + 1, j + 1, j + 2, gtk.EXPAND | gtk.FILL)
                self.table_widgets[(i, j)] = widget
        return t
        
    def refresh(self):
        if hasattr(self, 'params'):
            values = cbox.GetThings(self.path + "/status", [p.name for p in self.params], [])
            for param, state in self.refreshers:
                param.update(values, *state)
                
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
        SliderRow("Stages", "stages", 1, 12, setter = adjustment_changed_int)
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
    ("Active", 0, 1, "active", 'checkbox'), 
    ("Center Freq", 10, 20000, "center", 'log_slider'),
    ("Filter Q", 0.1, 100, "q", 'log_slider'),
    ("Gain", -36, 36, "gain", 'slider'),
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
        
    def refresh_values(self):
        values = cbox.GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        for i in range(len(self.cols)):
            par = self.cols[i]
            for j in range(16):
                value = getattr(values, par[3])[j]
                if par[4] == 'slider':
                    self.table_widgets[(i, j)].get_adjustment().set_value(value)
                elif par[4] == 'log_slider':
                    self.table_widgets[(i, j)].get_adjustment().set_value(log_map(value, par[1], par[2]))
                else:
                    self.table_widgets[(i, j)].set_active(value > 0)
        
    def update(self):
        values = cbox.GetThings(self.path + "/status", ["finished", "refresh"], [])
        if values.refresh:
            self.refresh_values()
        
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

