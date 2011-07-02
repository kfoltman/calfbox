import cbox
import pygtk
import gtk
import glib
import gobject
import math

def callback(cmd, cb, args):
    print cmd, cb, args

def bold_label(text, halign = 1):
    l = gtk.Label()
    l.set_markup("<b>%s</b>" % text)
    l.set_alignment(halign, 0.5)
    return l

def left_label(text):
    l = gtk.Label(text)
    l.set_alignment(0, 0.5)
    return l

def standard_hslider(adj):
    sc = gtk.HScale(adj)
    sc.set_size_request(160, -1)
    sc.set_value_pos(gtk.POS_RIGHT)
    return sc
    
class LogMapper:
    def __init__(self, min, max, format = "%f"):
        self.min = min
        self.max = max
        self.format_string = format
    def map(self, value):
        return float(self.min) * ((float(self.max) / self.min) ** (value / 100.0))
    def unmap(self, value):
        return math.log(value / float(self.min)) * 100 / math.log(float(self.max) / self.min)
    def format_value(self, value):
        return self.format_string % self.map(value)
    

freq_format = "%0.1f"
lfo_freq_mapper = LogMapper(0.01, 20, "%0.2f")

def standard_mapped_hslider(adj, mapper):
    sc = gtk.HScale(adj)
    sc.set_size_request(160, -1)
    sc.set_value_pos(gtk.POS_RIGHT)
    sc.connect('format-value', lambda scale, value, mapper: mapper.format_value(value), mapper)
    return sc

def standard_align(w, xo, yo, xs, ys):
    a = gtk.Alignment(xo, yo, xs, ys)
    a.add(w)
    return a
    
def standard_vscroll_window(width, height, content = None):
    scroller = gtk.ScrolledWindow()
    scroller.set_size_request(width, height)
    scroller.set_shadow_type(gtk.SHADOW_NONE);
    scroller.set_policy(gtk.POLICY_NEVER, gtk.POLICY_AUTOMATIC);
    if content is not None:
        scroller.add_with_viewport(content)
    return scroller

def standard_combo(list_store, active_item = None):
    cb = gtk.ComboBox(list_store)
    if active_item is not None:
        cb.set_active(active_item)
    cell = gtk.CellRendererText()
    cb.pack_start(cell, True)
    cb.add_attribute(cell, 'text', 0)
    return cb
    
def ls_index(list_store, value, column):
    for i in range(len(list_store)):
        if list_store[i][column] == value:
            return i
    return None

def standard_filter(patterns, name):
    f = gtk.FileFilter()
    for p in patterns:
        f.add_pattern(p)
    f.set_name(name)
    return f

def checkbox_changed_bool(adjustment, path, *items):
    cbox.do_cmd(path, None, list(items) + [1 if adjustment.get_active() else 0])

def adjustment_changed_int(adjustment, path, *items):
    cbox.do_cmd(path, None, list(items) + [int(adjustment.get_value())])

def adjustment_changed_float(adjustment, path, *items):
    cbox.do_cmd(path, None, list(items) + [float(adjustment.get_value())])

def adjustment_changed_float_mapped(adjustment, path, mapper, *items):
    cbox.do_cmd(path, None, list(items) + [mapper.map(adjustment.get_value())])

def combo_value_changed(combo, path, value_offset = 0):
    if combo.get_active() != -1:
        cbox.do_cmd(path, None, [value_offset + combo.get_active()])

def combo_value_changed_use_column(combo, path, column):
    if combo.get_active() != -1:
        cbox.do_cmd(path, None, [combo.get_model()[combo.get_active()][column]])

def add_slider_row(t, row, label, path, values, item, min, max, setter = adjustment_changed_float):
    t.attach(bold_label(label), 0, 1, row, row+1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
    adj = gtk.Adjustment(getattr(values, item), min, max, 1, 6, 0)
    if setter is not None:
        adj.connect("value_changed", setter, path + "/" + item)
    slider = standard_hslider(adj)
    t.attach(slider, 1, 2, row, row+1, gtk.EXPAND | gtk.FILL, gtk.SHRINK)
    if setter is None:
        slider.set_sensitive(False)
    return (slider, adj)

def add_mapped_slider_row(t, row, label, path, values, item, mapper, setter = adjustment_changed_float_mapped):
    t.attach(bold_label(label), 0, 1, row, row+1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
    adj = gtk.Adjustment(mapper.unmap(getattr(values, item)), 0, 100, 1, 6, 0)
    adj.connect("value_changed", setter, path + "/" + item, mapper)
    slider = standard_mapped_hslider(adj, mapper)
    t.attach(slider, 1, 2, row, row+1, gtk.EXPAND | gtk.FILL, gtk.SHRINK)
    return (slider, adj)

def add_display_row(t, row, label, path, values, item):
    t.attach(bold_label(label), 0, 1, row, row+1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
    w = left_label(getattr(values, item))
    t.attach(w, 1, 2, row, row+1, gtk.EXPAND | gtk.FILL, gtk.SHRINK)
    return w

class GetThings:
    def __init__(self, cmd, anames, args):
        for i in anames:
            if i.startswith("*"):
                setattr(self, i[1:], [])
            elif i.startswith("%"):
                setattr(self, i[1:], {})
            else:
                setattr(self, i, None)
        anames = set(anames)
        self.seq = []
        def update_callback(cmd, fb, args):
            self.seq.append((cmd, fb, args))
            cmd = cmd[1:]
            if cmd in anames:
                if len(args) == 1:
                    setattr(self, cmd, args[0])
                else:
                    setattr(self, cmd, args)
            elif "*" + cmd in anames:
                if len(args) == 1:
                    getattr(self, cmd).append(args[0])
                else:
                    getattr(self, cmd).append(args)
            elif "%" + cmd in anames:
                if len(args) == 2:
                    getattr(self, cmd)[args[0]] = args[1]
                else:
                    getattr(self, cmd)[args[0]] = args[1:]
        cbox.do_cmd(cmd, update_callback, args)
    def __str__(self):
        return str(self.seq)

def cfg_sections(prefix = ""):
    return GetThings('/config/sections', ['*section'], [str(prefix)]).section

def cfg_get(section, key):
    return GetThings('/config/get', ['value'], [str(section), str(key)]).value

class StreamWindow(gtk.VBox):
    def __init__(self, instrument, path):
        gtk.Widget.__init__(self)
        self.path = path
        
        panel = gtk.VBox(spacing=5)
        
        self.filebutton = gtk.FileChooserButton("Streamed file")
        self.filebutton.set_action(gtk.FILE_CHOOSER_ACTION_OPEN)
        self.filebutton.set_local_only(True)
        self.filebutton.set_filename(GetThings("%s/status" % self.path, ['filename'], []).filename)
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV", "*.ogg", "*.OGG", "*.flac", "*.FLAC"], "All loadable audio files"))
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV"], "RIFF WAVE files"))
        self.filebutton.add_filter(standard_filter(["*.ogg", "*.OGG"], "OGG container files"))
        self.filebutton.add_filter(standard_filter(["*.flac", "*.FLAC"], "FLAC files"))
        self.filebutton.add_filter(standard_filter(["*"], "All files"))
        self.filebutton.connect('file-set', self.file_set)
        panel.pack_start(self.filebutton, False, False)

        self.adjustment = gtk.Adjustment()
        self.adjustment_handler = self.adjustment.connect('value-changed', self.pos_slider_moved)
        self.progress = standard_hslider(self.adjustment)
        panel.pack_start(self.progress, False, False)

        self.play_button = gtk.Button(label = "_Play")
        self.rewind_button = gtk.Button(label = "_Rewind")
        self.stop_button = gtk.Button(label = "_Stop")
        buttons = gtk.HBox(spacing = 5)
        buttons.add(self.play_button)
        buttons.add(self.rewind_button)
        buttons.add(self.stop_button)
        panel.pack_start(buttons, False, False)
        
        self.add(panel)
        self.play_button.connect('clicked', lambda x: cbox.do_cmd("%s/play" % self.path, None, []))
        self.rewind_button.connect('clicked', lambda x: cbox.do_cmd("%s/seek" % self.path, None, [0]))
        self.stop_button.connect('clicked', lambda x: cbox.do_cmd("%s/stop" % self.path, None, []))
        self.refresh_id = glib.timeout_add(30, lambda: self.update())

    def update(self):
        attribs = GetThings("%s/status" % self.path, ['filename', 'pos', 'length', 'playing'], [])
        self.adjustment.handler_block(self.adjustment_handler)
        try:
            self.adjustment.set_all(attribs.pos, 0, attribs.length, 44100, 44100 * 10, 0)
        finally:
            self.adjustment.handler_unblock(self.adjustment_handler)
        return True
        
    def pos_slider_moved(self, adjustment):
        cbox.do_cmd("%s/seek" % self.path, None, [int(adjustment.get_value())])
        
    def file_set(self, button):
        cbox.do_cmd("%s/load" % self.path, None, [button.get_filename(), -1])
        

class WithPatchTable:
    def __init__(self, attribs):
        self.patches = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
        patches = GetThings("%s/patches" % self.path, ["%patch"], []).patch
        self.mapping = {}
        for id in patches:
            self.mapping[id] = len(self.mapping)
            self.patches.append((patches[id], id))
        
        self.patch_combos = []
        self.table = gtk.Table(2, 16)
        self.table.set_col_spacings(5)

        for i in range(16):
            self.table.attach(bold_label("Channel %s" % (1 + i)), 0, 1, i, i + 1, gtk.SHRINK, gtk.SHRINK)
            cb = standard_combo(self.patches, self.mapping[attribs.patch[i + 1][0]])
            cb.connect('changed', self.patch_combo_changed, i + 1)
            self.table.attach(cb, 1, 2, i, i + 1, gtk.SHRINK, gtk.SHRINK)
            self.patch_combos.append(cb)

        self.refresh_id = glib.timeout_add(500, lambda: self.patch_combo_update())

    def patch_combo_changed(self, combo, channel):
        cbox.do_cmd(self.path + "/set_patch", None, [int(channel), int(self.patches[combo.get_active()][1])])

    def patch_combo_update(self):
        attribs = GetThings("%s/status" % self.path, ['%patch'], [])
        for i in range(16):
            cb = self.patch_combos[i]
            patch_id = int(self.patches[cb.get_active()][1])
            if patch_id != attribs.patch[i + 1][1]:
                cb.set_active(self.mapping[attribs.patch[i + 1][0]])
        #self.status_label.set_markup(s)
        return True
        
class FluidsynthWindow(gtk.VBox, WithPatchTable):
    def __init__(self, instrument, path):
        gtk.Widget.__init__(self)
        self.path = path
        
        attribs = GetThings("%s/status" % self.path, ['%patch', 'polyphony'], [])

        panel = gtk.VBox(spacing=5)
        table = gtk.Table(2, 1)
        add_slider_row(table, 0, "Polyphony", self.path, attribs, "polyphony", 2, 256, adjustment_changed_int)
        panel.pack_start(table, False, False)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True)
        self.add(panel)

class SamplerWindow(gtk.VBox, WithPatchTable):
    def __init__(self, instrument, path):
        gtk.Widget.__init__(self)
        self.path = path
        
        attribs = GetThings("%s/status" % self.path, ['%patch', 'polyphony', 'active_voices'], [])

        panel = gtk.VBox(spacing=5)
        table = gtk.Table(2, 2)
        table.set_col_spacings(5)
        add_slider_row(table, 0, "Polyphony", self.path, attribs, "polyphony", 1, 128, adjustment_changed_int)
        self.voices_widget = add_display_row(table, 1, "Voices in use", self.path, attribs, "active_voices")
        panel.pack_start(table, False, False)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True)
        self.add(panel)
        self.refresh_id = glib.timeout_add(200, lambda: self.voices_update())
        
    def voices_update(self):
        attribs = GetThings("%s/status" % self.path, ['active_voices'], [])
        self.voices_widget.set_text(str(attribs.active_voices))
        return True

class TableRowWidget:
    def __init__(self, label, name, **kwargs):
        self.label = label
        self.name = name
        self.kwargs = kwargs

class SliderRow(TableRowWidget):
    def __init__(self, label, name, minv, maxv, **kwargs):
        TableRowWidget.__init__(self, label, name, **kwargs)
        self.minv = minv
        self.maxv = maxv
    def add_row(self, table, row_no, path, values):
        return add_slider_row(table, row_no, self.label, path, values, self.name, self.minv, self.maxv, **self.kwargs)
    def update(self, values, slider, adjustment):
        adjustment.set_value(getattr(values, self.name))

class MappedSliderRow(TableRowWidget):
    def __init__(self, label, name, mapper, **kwargs):
        TableRowWidget.__init__(self, label, name, **kwargs)
        self.mapper = mapper
    def add_row(self, table, row_no, path, values):
        return add_mapped_slider_row(table, row_no, self.label, path, values, self.name, self.mapper, **self.kwargs)
    def update(self, values, slider, adjustment):
        adjustment.set_value(self.mapper.unmap(getattr(values, self.name)))

class EffectWindow(gtk.Window):
    def __init__(self, instrument, output, main_window, path):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_UTILITY)
        self.set_transient_for(main_window)
        self.main_window = main_window
        self.path = path
        self.set_title("%s - %s" % (self.effect_name, instrument))
        if hasattr(self, 'params'):
            values = GetThings(self.path + "/status", [p.name for p in self.params], [])
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
            values = GetThings(self.path + "/status", [p.name for p in self.params], [])
            for param, state in self.refreshers:
                param.update(values, *state)
                
    def on_close(self, widget, event):
        self.main_window.on_effect_popup_close(self)

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

eq_cols = [
    ("Active", 0, 1, "active", 'checkbox'), 
    ("Center Freq", 10, 20000, "center", 'log_slider'),
    ("Filter Q", 0.1, 100, "q", 'log_slider'),
    ("Gain", -36, 36, "gain", 'slider'),
]

class EQWindow(EffectWindow):
    def __init__(self, instrument, output, main_window, path):
        EffectWindow.__init__(self, instrument, output, main_window, path)
        values = GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        self.add(self.create_param_table(eq_cols, 4, values))
    effect_name = "Equalizer"
        
class FBRWindow(EffectWindow):
    effect_name = "Feedback Reduction"
    
    def __init__(self, instrument, output, main_window, path):
        EffectWindow.__init__(self, instrument, output, main_window, path)
        values = GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        t = self.create_param_table(eq_cols, 16, values, 1)        
        self.add(t)
        self.ready_label = gtk.Label("-")
        t.attach(self.ready_label, 0, 2, 17, 18)
        self.refresh_id = glib.timeout_add(30, lambda: self.update())
        sbutton = gtk.Button("_Start")
        sbutton.connect("clicked", lambda button, path: cbox.do_cmd(path + "/start", None, []), self.path)
        t.attach(sbutton, 2, 4, 17, 18)
        
    def refresh_values(self):
        values = GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
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
        values = GetThings(self.path + "/status", ["finished", "refresh"], [])
        if values.refresh:
            self.refresh_values()
        
        if values.finished > 0:
            self.ready_label.set_text("Ready")
        else:
            self.ready_label.set_text("Not Ready")
        return True

engine_window_map = {
    'phaser': PhaserWindow,
    'chorus': ChorusWindow,
    'delay': DelayWindow,
    'reverb' : ReverbWindow,
    'feedback_reducer': FBRWindow,
    'parametric_eq': EQWindow,
    'tone_control': ToneControlWindow,
    'stream_player' : StreamWindow,
    'fluidsynth' : FluidsynthWindow,
    'sampler' : SamplerWindow
}

effect_engines = ['', 'phaser', 'reverb', 'chorus', 'feedback_reducer', 'tone_control', 'delay', 'parametric_eq']

class SceneDialog(gtk.Dialog):
    def __init__(self, parent):
        gtk.Dialog.__init__(self, "Select a scene", parent, gtk.DIALOG_MODAL, 
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_OK, gtk.RESPONSE_OK))
        self.set_default_response(gtk.RESPONSE_OK)
        model = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_STRING, gobject.TYPE_STRING)
        
        for s in cfg_sections("scene:"):
            title = cfg_get(s, "title")
            model.append((s[6:], "Scene", s))
        for s in cfg_sections("instrument:"):
            title = cfg_get(s, "title")
            model.append((s[11:], "Instrument", s))
        for s in cfg_sections("layer:"):
            title = cfg_get(s, "title")
            model.append((s[6:], "Layer", s))
                
        scenes = gtk.TreeView(model)
        scenes.insert_column_with_attributes(0, "Name", gtk.CellRendererText(), text=0)
        scenes.insert_column_with_attributes(1, "Type", gtk.CellRendererText(), text=1)
        self.vbox.pack_start(scenes)
        scenes.show()
        scenes.grab_focus()
        self.scenes = scenes
        self.scenes.connect('row-activated', lambda w, path, column: self.response(gtk.RESPONSE_OK))
    def get_selected_scene(self):
        return self.scenes.get_model()[self.scenes.get_cursor()[0][0]]

class MainWindow(gtk.Window):
    def __init__(self):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.vbox = gtk.VBox(spacing = 5)
        self.add(self.vbox)
        self.create()
        self.refresh_id = glib.timeout_add(30, lambda: self.update())

    def create_master(self, scene):
        self.scene_list = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_STRING)
                
        self.master_info = left_label("")
        self.timesig_info = left_label("")
        
        t = gtk.Table(2, 6)
        t.set_col_spacings(5)
        t.set_row_spacings(5)
        
        t.attach(bold_label("Scene"), 0, 1, 0, 1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        self.scene_label = left_label(scene.name)
        t.attach(self.scene_label, 1, 2, 0, 1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)

        self.title_label = left_label(scene.title)
        t.attach(bold_label("Title"), 0, 1, 1, 2, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        t.attach(self.title_label, 1, 2, 1, 2, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        
        t.attach(bold_label("Play pos"), 0, 1, 2, 3, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        t.attach(self.master_info, 1, 2, 2, 3, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        
        t.attach(bold_label("Time sig"), 0, 1, 3, 4, gtk.SHRINK, gtk.SHRINK)
        t.attach(self.timesig_info, 1, 2, 3, 4, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        
        t.attach(bold_label("Tempo"), 0, 1, 4, 5, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        self.tempo_adj = gtk.Adjustment(40, 40, 300, 1, 5, 0)
        self.tempo_adj.connect('value_changed', adjustment_changed_float, "/master/set_tempo")
        t.attach(standard_hslider(self.tempo_adj), 1, 2, 4, 5, gtk.EXPAND | gtk.FILL, gtk.SHRINK)

        t.attach(bold_label("Transpose"), 0, 1, 5, 6, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        self.transpose_adj = gtk.Adjustment(scene.transpose, -24, 24, 1, 5, 0)
        self.transpose_adj.connect('value_changed', adjustment_changed_int, '/scene/transpose')
        t.attach(standard_align(gtk.SpinButton(self.transpose_adj), 0, 0, 0, 0), 1, 2, 5, 6, gtk.EXPAND | gtk.FILL, gtk.SHRINK)
        return t

    def create_menu(self, title, items):
        menuitem = gtk.MenuItem(title)
        if items is not None:
            menu = gtk.Menu()
            menuitem.set_submenu(menu)
            for label, meth in items:
                mit = gtk.MenuItem(label)
                mit.connect('activate', meth)
                menu.append(mit)
        return menuitem

    def quit(self, w):
        self.destroy()
        
    def load_scene(self, w):
        d = SceneDialog(self)
        response = d.run()
        try:
            if response == gtk.RESPONSE_OK:
                scene = d.get_selected_scene()
                if scene[1] == 'Scene':
                    cbox.do_cmd("/scene/load", None, [scene[2][6:]])
                elif scene[1] == 'Layer':
                    cbox.do_cmd("/scene/load_layer", None, [scene[2][6:]])
                elif scene[1] == 'Instrument':
                    cbox.do_cmd("/scene/load_instrument", None, [scene[2][11:]])
                scene = GetThings("/scene/status", ['name', 'title'], [])
                self.scene_label.set_text(scene.name)
                self.title_label.set_text(scene.title)
                self.refresh_instrument_pages()
        finally:
            d.destroy()

    def refresh_instrument_pages(self):
        self.delete_instrument_pages()
        rt = GetThings("/rt/status", ['audio_channels'], [])
        scene = GetThings("/scene/status", ['*layer', '*instrument', 'name', 'title', 'transpose'], [])
        self.create_instrument_pages(scene, rt)
        self.nb.show_all()
        self.title_label.set_text(scene.title)

    def create(self):
        self.menu_bar = gtk.MenuBar()
        
        self.menu_bar.append(self.create_menu("_Scene", [
            ("_Load", self.load_scene),
            ("_Quit", self.quit),
        ]))
        
        self.vbox.pack_start(self.menu_bar, False, False)
        rt = GetThings("/rt/status", ['audio_channels'], [])
        scene = GetThings("/scene/status", ['*layer', '*instrument', '*aux', 'name', 'title', 'transpose'], [])
        
        self.nb = gtk.Notebook()
        self.vbox.add(self.nb)
        self.nb.append_page(self.create_master(scene), gtk.Label("Master"))
        self.create_instrument_pages(scene, rt)

    def create_instrument_pages(self, scene, rt):
        self.path_widgets = {}
        self.path_popups = {}
        self.fxpreset_ls = {}
        
        for preset in cfg_sections("fxpreset:"):
            engine = cfg_get(preset, "engine")
            if engine not in self.fxpreset_ls:
                self.fxpreset_ls[engine] = gtk.ListStore(gobject.TYPE_STRING)
            self.fxpreset_ls[engine].append((preset[9:],))
        
        fx_ls = gtk.ListStore(gobject.TYPE_STRING)
        for engine in effect_engines:
            if engine not in self.fxpreset_ls:
                self.fxpreset_ls[engine] = gtk.ListStore(gobject.TYPE_STRING)
            fx_ls.append((engine,))
            
        outputs_ls = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
        for out in range(0, rt.audio_channels[1]/2):
            outputs_ls.append(("Out %s/%s" % (out * 2 + 1, out * 2 + 2), out))
            
        auxbus_ls = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_STRING)
        auxbus_ls.append(("", ""))
        for bus in range(len(scene.aux)):
            auxbus_ls.append(("Aux: %s" % scene.aux[bus][1], scene.aux[bus][1]))
            
        for i in scene.instrument:
            ipath = "/instr/%s" % i[0]
            idata = GetThings(ipath + "/status", ['outputs', 'aux_offset'], [])
            #attribs = GetThings("/scene/instr_info", ['engine', 'name'], [i])
            #markup += '<b>Instrument %d:</b> engine %s, name %s\n' % (i, attribs.engine, attribs.name)
            b = gtk.VBox(spacing = 5)
            b.set_border_width(5)
            b.pack_start(gtk.Label("Engine: %s" % i[1]), False, False)
            b.pack_start(gtk.HSeparator(), False, False)
            t = gtk.Table(1 + idata.outputs, 5)
            t.set_col_spacings(5)
            t.attach(bold_label("Instr. output", 0.5), 0, 1, 0, 1, gtk.SHRINK, gtk.SHRINK)
            t.attach(bold_label("Send to", 0.5), 1, 2, 0, 1, gtk.SHRINK, gtk.SHRINK)
            t.attach(bold_label("Gain [dB]", 0.5), 2, 3, 0, 1, 0, gtk.SHRINK)
            t.attach(bold_label("Effect", 0.5), 3, 6, 0, 1, 0, gtk.SHRINK)
            b.pack_start(t, False, False)
            
            y = 1
            for o in range(1, idata.outputs + 1):
                if o < idata.aux_offset:
                    opath = "%s/output/%s" % (ipath, o)
                else:
                    opath = "%s/aux/%s" % (ipath, o - idata.aux_offset + 1)
                odata = GetThings(opath + "/status", ['gain', 'output', 'bus', 'insert_engine', 'insert_preset'], [])
                engine = odata.insert_engine
                preset = odata.insert_preset
                
                is_aux = 2 * (o - 1) >= idata.aux_offset
                
                if not is_aux:
                    output_name = "Out %s" % o
                else:
                    output_name = "Aux %s" % (o - idata.aux_offset / 2)
                t.attach(gtk.Label(output_name), 0, 1, y, y + 1, gtk.SHRINK, gtk.SHRINK)
                
                if not is_aux:
                    cb = standard_combo(outputs_ls, odata.output - 1)
                    cb.connect('changed', combo_value_changed, opath + '/output', 1)
                else:
                    cb = standard_combo(auxbus_ls, ls_index(auxbus_ls, odata.bus, 1))
                    cb.connect('changed', combo_value_changed_use_column, opath + '/bus', 1)
                t.attach(cb, 1, 2, y, y + 1, gtk.SHRINK, gtk.SHRINK)
                    
                
                adj = gtk.Adjustment(odata.gain, -96, 24, 1, 6, 0)
                adj.connect('value_changed', adjustment_changed_float, opath + '/gain')
                t.attach(standard_hslider(adj), 2, 3, y, y + 1, gtk.EXPAND | gtk.FILL, gtk.SHRINK)
                
                fx_engine = standard_combo(fx_ls, ls_index(fx_ls, engine, 0))
                fx_engine.connect('changed', self.fx_engine_changed, opath)
                t.attach(fx_engine, 3, 4, y, y + 1, 0, gtk.SHRINK)
                
                if engine in self.fxpreset_ls:
                    fx_preset = standard_combo(self.fxpreset_ls[engine], ls_index(self.fxpreset_ls[engine], preset, 0))
                else:
                    fx_preset = standard_combo(None, 0)
                fx_preset.connect('changed', self.fx_preset_changed, opath)
                t.attach(fx_preset, 4, 5, y, y + 1, 0, gtk.SHRINK)

                fx_edit = gtk.Button("_Edit")
                t.attach(fx_edit, 5, 6, y, y + 1, 0, gtk.SHRINK)
                fx_edit.connect("clicked", self.edit_effect_clicked, opath, i[0], o)
                fx_edit.set_sensitive(engine in engine_window_map)
                
                self.path_widgets[opath] = (fx_engine, fx_preset, fx_edit)
                y += 1
            if i[1] in engine_window_map:
                b.pack_start(gtk.HSeparator(), False, False)
                b.pack_start(engine_window_map[i[1]](i[0], "/instr/%s/engine" % i[0]), True, True)
            self.nb.append_page(b, gtk.Label(i[0]))
        self.update()
        
    def delete_instrument_pages(self):
        while self.nb.get_n_pages() > 1:
            self.nb.remove_page(self.nb.get_n_pages() - 1)
            
    def edit_effect_clicked(self, button, opath, instr, output):
        if opath in self.path_popups:
            self.path_popups[opath].present()
            return
        engine = GetThings(opath + "/status", ['insert_engine'], []).insert_engine
        wclass = engine_window_map[engine]
        popup = wclass(instr, output, self, "%s/engine" % opath)
        popup.show_all()
        popup.present()
        self.path_popups[opath] = popup
            
    def fx_engine_changed(self, combo, opath):
        if opath in self.path_popups:
            self.path_popups[opath].destroy()
            del self.path_popups[opath]
            
        engine = combo.get_model()[combo.get_active()][0]
        cbox.do_cmd(opath + '/insert_engine', None, [engine])
        fx_engine, fx_preset, fx_edit = self.path_widgets[opath]
        fx_preset.set_model(self.fxpreset_ls[engine])
        fx_preset.set_active(0)
        fx_edit.set_sensitive(engine in engine_window_map)
        
    def fx_preset_changed(self, combo, opath):
        if combo.get_active() >= 0:
            cbox.do_cmd(opath + '/insert_preset', None, [combo.get_model()[combo.get_active()][0]])
        if opath in self.path_popups:
            self.path_popups[opath].refresh()
        
    def update(self):
        cbox.do_cmd("/on_idle", None, [])
        master = GetThings("/master/status", ['pos', 'tempo', 'timesig'], [])
        self.master_info.set_markup('%s' % master.pos)
        self.timesig_info.set_markup("%s/%s" % tuple(master.timesig))
        self.tempo_adj.set_value(master.tempo)
        return True
    
    def on_effect_popup_close(self, popup):
        del self.path_popups[popup.path[0:-7]]

def do_quit(window):
    gtk.main_quit()

w = MainWindow()
w.set_title("My UI")
w.show_all()
w.connect('destroy', do_quit)

gtk.main()

