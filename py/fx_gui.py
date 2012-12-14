import cbox
from gui_tools import *

#################################################################################################################################

class EffectWindow(Gtk.Window):
    engine_name = None
    
    def __init__(self, location, main_window, path):
        Gtk.Window.__init__(self, Gtk.WindowType.TOPLEVEL)
        self.set_type_hint(Gdk.WindowTypeHint.UTILITY)
        self.set_transient_for(main_window)
        self.main_window = main_window
        self.path = path
        self.vpath = cbox.VarPath(path)
        self.set_title("%s - %s" % (self.effect_name, location))
        self.vbox = Gtk.VBox()
        menu_bar = Gtk.MenuBar()
        menu_bar.append(create_menu("_Effect", [
            ("_Save as...", self.on_effect_save_as if self.engine_name is not None else None),
            ("_Close", lambda w: self.destroy()),
        ]))
        self.vbox.pack_start(menu_bar, False, False, 0)
        if hasattr(self, 'params'):
            values = cbox.GetThings(self.path + "/status", [p.name for p in self.params], [])
            self.refreshers = []
            t = Gtk.Table(2, len(self.params))
            for i in range(len(self.params)):
                p = self.params[i]
                t.attach(p.create_label(), 0, 1, i, i+1, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
                widget, refresher = p.create_widget(self.vpath)
                refresher(values)
                t.attach(widget, 1, 2, i, i+1, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
                self.refreshers.append(refresher)
            self.vbox.pack_start(t, True, True, 5)
        self.add(self.vbox)
        
    def create_param_table(self, cols, rows, values, extra_rows = 0):
        t = Gtk.Table(4, rows + 1 + extra_rows)
        self.cols = cols
        self.table_refreshers = []
        for i in range(len(self.cols)):
            par = self.cols[i]
            t.attach(par.create_label(), i, i + 1, 0, 1, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL)
            for j in range(rows):
                widget, refresher = par.create_widget(self.vpath.plus(None, j))
                t.attach(widget, i, i + 1, j + 1, j + 2, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL)
                refresher(values)
                self.table_refreshers.append(refresher)
        return t
        
    def get_save_params(self):
        if hasattr(self, 'params'):
            values = cbox.GetThings(self.path + "/status", [p.name for p in self.params], [])
            result = {'engine' : self.engine_name}
            for p in self.params:
                result[p.name] = str(getattr(values, p.name))
            return result
        return None
        
    def refresh(self):
        if hasattr(self, 'params'):
            values = cbox.GetThings(self.path + "/status", [p.name for p in self.params], [])
            for refresher in self.refreshers:
                refresher(values)
                
    def on_effect_save_as(self, w):
        data = self.get_save_params()
        if data is None:
            print "Save not implemented for this effect"
            return
            
        dlg = SaveConfigObjectDialog(self, "Select name for effect preset")
        try:
            if dlg.run() == Gtk.ResponseType.OK and dlg.get_name() != "":
                cs = cbox.CfgSection("fxpreset:" + dlg.get_name())
                for name in sorted(data.keys()):
                    cs[name] = data[name]
                cbox.Config.save()
        finally:
            dlg.destroy()
                
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
    engine_name = "phaser"
    effect_name = "Phaser"
    
class ChorusWindow(EffectWindow):
    params = [
        SliderRow("Min. delay", "min_delay", 1, 20),
        SliderRow("Mod depth", "mod_depth", 1, 20),
        MappedSliderRow("LFO frequency", "lfo_freq", lfo_freq_mapper),
        SliderRow("Stereo", "stereo_phase", 0, 360),
        SliderRow("Wet/dry", "wet_dry", 0, 1)
    ]
    engine_name = "chorus"
    effect_name = "Chorus"

class DelayWindow(EffectWindow):
    params = [
        SliderRow("Delay time (ms)", "time", 1, 1000),
        SliderRow("Feedback", "fb_amt", 0, 1),
        SliderRow("Wet/dry", "wet_dry", 0, 1)
    ]
    engine_name = "delay"
    effect_name = "Delay"

class ReverbWindow(EffectWindow):
    params = [
        SliderRow("Decay time", "decay_time", 500, 5000),
        SliderRow("Dry amount", "dry_amt", -100, 12),
        SliderRow("Wet amount", "wet_amt", -100, 12),
        MappedSliderRow("Lowpass", "lowpass", LogMapper(300, 20000, freq_format)),
        MappedSliderRow("Highpass", "highpass", LogMapper(30, 2000, freq_format))
    ]
    engine_name = "reverb"
    effect_name = "Reverb"

class ToneControlWindow(EffectWindow):
    params = [
        MappedSliderRow("Lowpass", "lowpass", LogMapper(300, 20000, freq_format)),
        MappedSliderRow("Highpass", "highpass", LogMapper(30, 2000, freq_format))
    ]
    engine_name = "tone_control"
    effect_name = "Tone Control"

class CompressorWindow(EffectWindow):
    params = [
        SliderRow("Threshold", "threshold", -100, 12),
        MappedSliderRow("Ratio", "ratio", LogMapper(1, 100, "%0.2f")),
        MappedSliderRow("Attack", "attack", LogMapper(1, 1000, ms_format)),
        MappedSliderRow("Release", "release", LogMapper(1, 1000, ms_format)),
        SliderRow("Make-up gain", "makeup", -48, 48),
    ]
    engine_name = "compressor"
    effect_name = "Compressor"

class GateWindow(EffectWindow):
    params = [
        SliderRow("Threshold", "threshold", -100, 12),
        MappedSliderRow("Ratio", "ratio", LogMapper(1, 100, "%0.2f")),
        MappedSliderRow("Attack", "attack", LogMapper(1, 1000, ms_format)),
        MappedSliderRow("Hold", "hold", LogMapper(1, 1000, ms_format)),
        MappedSliderRow("Release", "release", LogMapper(1, 1000, ms_format)),
    ]
    engine_name = "gate"
    effect_name = "Gate"

class DistortionWindow(EffectWindow):
    params = [
        SliderRow("Drive", "drive", -36, 36),
        SliderRow("Shape", "shape", -1, 2),
    ]
    engine_name = "distortion"
    effect_name = "Distortion"

class FuzzWindow(EffectWindow):
    params = [
        SliderRow("Drive", "drive", -36, 36),
        SliderRow("Wet/dry", "wet_dry", 0, 1),
        SliderRow("Rectify", "rectify", 0, 1),
        MappedSliderRow("Pre freq", "band", LogMapper(100, 5000, freq_format)),
        SliderRow("Pre width", "bandwidth", 0.25, 4),
        MappedSliderRow("Post freq", "band2", LogMapper(100, 5000, freq_format)),
        SliderRow("Post width", "bandwidth2", 0.25, 4),
    ]
    engine_name = "fuzz"
    effect_name = "Fuzz"

class EQCommon(object):
    columns = [
        CheckBoxRow("Active", "active"),
        MappedSliderRow("Center Freq", "center", filter_freq_mapper),
        MappedSliderRow("Filter Q", "q", LogMapper(0.01, 100, "%f")),
        SliderRow("Gain", "gain", -36, 36),
    ]
    def get_save_params(self):
        values = cbox.GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        result = {}
        for row in range(self.bands):
            row2 = 1 + row
            result['band%s_active' % row2] = values.active[row]
            result['band%s_center' % row2] = values.center[row]
            result['band%s_q' % row2] = values.q[row]
            result['band%s_gain' % row2] = values.gain[row]
        return result

class EQWindow(EffectWindow, EQCommon):
    def __init__(self, instrument, main_window, path):
        EffectWindow.__init__(self, instrument, main_window, path)
        values = cbox.GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        self.vbox.add(self.create_param_table(self.columns, 4, values))
    def get_save_params(self):
        return EQCommon.get_save_params(self)
    effect_name = "Equalizer"
    engine_name = "parametric_eq"
    bands = 4
        
class FBRWindow(EffectWindow, EQCommon):
    effect_name = "Feedback Reduction"
    engine_name = "feedback_reducer"
    bands = 16
    
    def __init__(self, instrument, main_window, path):
        EffectWindow.__init__(self, instrument, main_window, path)
        values = cbox.GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        t = self.create_param_table(self.columns, 16, values, 1)        
        self.vbox.add(t)
        self.ready_label = Gtk.Label("-")
        t.attach(self.ready_label, 0, 2, 17, 18)
        set_timer(self, 100, self.update)
        sbutton = Gtk.Button.new_with_mnemonic("_Start")
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
        
    def get_save_params(self):
        return EQCommon.get_save_params(self)

class FXChainWindow(EffectWindow):
    effect_name = "Effect chain"
    
    def __init__(self, instrument, main_window, path):
        EffectWindow.__init__(self, instrument, main_window, path)
        self.fx_table = None
        self.choosers = []
        self.refresh_table()
        
    def refresh_table(self):
        res = cbox.GetThings(self.path + "/status", ["*module", "%bypass"], [])
        values = res.module
        bypass = res.bypass
        fx_count = len(values)
        t = Gtk.Table(fx_count + 2, 9)
        for c in self.choosers:
            c.close_popup()
        self.choosers = []
        for i in range(1, fx_count + 1):
            engine, preset = values[i - 1]
            chooser = InsertEffectChooser("%s/module/%s" % (self.path, i), "%s: slot %s" % (self.get_title(), i), engine, preset, bypass[i], self.main_window)
            t.attach(chooser.fx_engine, 0, 1, i, i + 1, 0, Gtk.AttachOptions.SHRINK)
            t.attach(chooser.fx_preset, 1, 2, i, i + 1, 0, Gtk.AttachOptions.SHRINK)
            t.attach(chooser.fx_edit, 2, 3, i, i + 1, 0, Gtk.AttachOptions.SHRINK)
            t.attach(chooser.fx_bypass, 3, 4, i, i + 1, 0, Gtk.AttachOptions.SHRINK)
            buttons = [
                ("+", self.on_add_clicked, lambda pos: True),
                ("-", self.on_delete_clicked, lambda pos: True),
                ("Up", self.on_up_clicked, lambda pos: pos > 1),
                ("Down", self.on_down_clicked, lambda pos: pos < fx_count)
            ]
            for j in range(len(buttons)):
                label, method, cond = buttons[j]
                if not cond(i):
                    continue
                button = Gtk.Button(label)
                button.connect('clicked', lambda button, method, pos: method(pos), method, i)
                t.attach(button, 4 + j, 5 + j, i, i + 1, 0, Gtk.AttachOptions.SHRINK)
            self.choosers.append(chooser)
        button = Gtk.Button("+")
        button.connect('clicked', lambda button, pos: self.on_add_clicked(pos), fx_count + 1)
        t.attach(button, 3, 4, fx_count + 1, fx_count + 2, 0, Gtk.AttachOptions.SHRINK)
        if self.fx_table is not None:
            self.vbox.remove(self.fx_table)
        self.vbox.pack_start(t, True, True, 5)
        t.show_all()
        self.fx_table = t
    def on_add_clicked(self, pos):
        cbox.do_cmd(self.path + "/insert", None, [pos])
        self.refresh_table()
    def on_delete_clicked(self, pos):
        cbox.do_cmd(self.path + "/delete", None, [pos])
        self.refresh_table()
    def on_up_clicked(self, pos):
        cbox.do_cmd(self.path + "/move", None, [pos, pos - 1])
        self.refresh_table()
    def on_down_clicked(self, pos):
        cbox.do_cmd(self.path + "/move", None, [pos, pos + 1])
        self.refresh_table()

#################################################################################################################################

effect_engines = ['', 'phaser', 'reverb', 'chorus', 'feedback_reducer', 'tone_control', 'delay', 'parametric_eq', 'compressor', 'gate', 'distortion', 'fuzz', 'fxchain']

effect_window_map = {
    'phaser': PhaserWindow,
    'chorus': ChorusWindow,
    'delay': DelayWindow,
    'reverb' : ReverbWindow,
    'feedback_reducer': FBRWindow,
    'parametric_eq': EQWindow,
    'tone_control': ToneControlWindow,
    'compressor': CompressorWindow,
    'gate': GateWindow,
    'distortion': DistortionWindow,
    'fuzz': FuzzWindow,
    'fxchain': FXChainWindow,
}

#################################################################################################################################

class EffectListModel(Gtk.ListStore):
    def __init__(self):
        self.presets = {}
        Gtk.ListStore.__init__(self, GObject.TYPE_STRING)
        for engine in effect_engines:
            self.presets[engine] = Gtk.ListStore(GObject.TYPE_STRING, GObject.TYPE_STRING)
            self.append((engine,))
            
        for preset in cbox.Config.sections("fxpreset:"):
            engine = preset["engine"]
            if engine in self.presets:
                title = preset.title if hasattr(preset, 'title') else preset.name[9:] 
                self.presets[engine].append((preset.name[9:], title))
    
    def get_model_for_engine(self, engine):
        return self.presets[engine]

effect_list_model = EffectListModel()

#################################################################################################################################

class InsertEffectChooser(object):
    def __init__(self, opath, location, engine, preset, bypass, main_window):
        self.opath = opath
        self.location = location
        self.main_window = main_window
        self.popup = None
        
        self.fx_engine = standard_combo(effect_list_model, ls_index(effect_list_model, engine, 0), width = 120)
        self.fx_engine.connect('changed', self.fx_engine_changed)
        
        if engine in effect_engines:
            model = effect_list_model.get_model_for_engine(engine)
            self.fx_preset = standard_combo(model, active_item_lookup = preset, column = 1, lookup_column = 0, width = 120)
        else:
            self.fx_preset = standard_combo(None, active_item = 0, column = 1, width = 120)
        self.fx_preset.connect('changed', self.fx_preset_changed)

        self.fx_edit = Gtk.Button.new_with_mnemonic("_Edit")
        self.fx_edit.connect("clicked", self.edit_effect_clicked)
        self.fx_edit.set_sensitive(engine in effect_window_map)
        
        self.fx_bypass = Gtk.ToggleButton.new_with_mnemonic("_Bypass")
        self.fx_bypass.set_active(bypass > 0)
        self.fx_bypass.connect("clicked", self.bypass_effect_clicked)
        
    def edit_effect_clicked(self, button):
        if self.popup is not None:
            self.popup.present()
            return
        engine = cbox.GetThings(self.opath + "/status", ['insert_engine'], []).insert_engine
        wclass = effect_window_map[engine]
        popup = wclass(self.location, self.main_window, "%s/engine" % self.opath)
        popup.show_all()
        popup.present()
        popup.connect('delete_event', self.on_popup_closed)
        self.popup = popup
            
    def fx_engine_changed(self, combo):
        if self.popup is not None:
            self.popup.destroy()
            self.popup = None
            
        engine = combo.get_model()[combo.get_active()][0]
        cbox.do_cmd(self.opath + '/insert_engine', None, [engine])
        self.fx_preset.set_model(effect_list_model.get_model_for_engine(engine))
        self.fx_preset.set_active(0)
        self.fx_edit.set_sensitive(engine in effect_window_map)
        
    def fx_preset_changed(self, combo):
        if combo.get_active() >= 0:
            cbox.do_cmd(self.opath + '/insert_preset', None, [combo.get_model()[combo.get_active()][0]])
        if self.popup is not None:
            self.popup.refresh()

    def on_popup_closed(self, popup, event):
        self.popup = None
        
    def close_popup(self):
        if self.popup is not None:
            self.popup.destroy();

    def bypass_effect_clicked(self, button):
        cbox.do_cmd(self.opath + "/set_bypass", None, [1 if button.get_active() else 0])

#################################################################################################################################

class LoadEffectDialog(SelectObjectDialog):
    title = "Load an aux effect"
    def __init__(self, parent):
        SelectObjectDialog.__init__(self, parent)
    def update_model(self, model):
        for s in cbox.Config.sections("fxpreset:"):
            title = s["title"]
            model.append((s.name[9:], s['engine'], s.name, title))

