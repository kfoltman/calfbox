import cbox
import pygtk
import gtk
import glib
import gobject

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
    
def standard_align(w, xo, yo, xs, ys):
    a = gtk.Alignment(xo, yo, xs, ys)
    a.add(w)
    return a
    
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

def output_combo_value_changed(combo, instr, output_pair):
    if combo.get_active() != -1:
        cbox.do_cmd("/instr/%s/set_output" % instr, None, [output_pair, 1 + combo.get_active()])

def add_slider_row(t, row, label, path, values, item, min, max, setter = adjustment_changed_float):
    t.attach(bold_label(label), 0, 1, row, row+1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
    adj = gtk.Adjustment(getattr(values, item), min, max, 1, 6, 0)
    adj.connect("value_changed", setter, path + "/" + item)
    t.attach(standard_hslider(adj), 1, 2, row, row+1, gtk.EXPAND | gtk.FILL, gtk.SHRINK)

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
        def update_callback(cmd, fb, args):
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
        

class FluidsynthWindow(gtk.VBox):
    def __init__(self, instrument, path):
        gtk.Widget.__init__(self)
        self.path = path
        
        self.patches = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
        patches = GetThings("%s/patches" % self.path, ["%patch"], []).patch
        self.mapping = {}
        for id in patches:
            self.mapping[id] = len(self.mapping)
            self.patches.append((patches[id], id))
        
        attribs = GetThings("%s/status" % self.path, ['%patch', 'polyphony'], [])

        panel = gtk.VBox(spacing=5)
        table = gtk.Table(2, 1)
        add_slider_row(table, 0, "Polyphony", self.path, attribs, "polyphony", 2, 256, adjustment_changed_int)
        panel.pack_start(table, False, False)
        
        self.table = gtk.Table(2, 16)
        self.table.set_col_spacings(5)

        for i in range(0, 16):
            self.table.attach(bold_label("Channel %s" % (1 + i)), 0, 1, i, i + 1, gtk.SHRINK, gtk.SHRINK)
            cb = gtk.ComboBox(self.patches)
            cell = gtk.CellRendererText()
            cb.pack_start(cell, True)
            cb.add_attribute(cell, 'text', 0)
            cb.set_active(self.mapping[attribs.patch[i + 1][0]])
            cb.connect('changed', self.patch_combo_changed, i + 1)
            self.table.attach(cb, 1, 2, i, i + 1, gtk.SHRINK, gtk.SHRINK)
        scroller = gtk.ScrolledWindow()
        scroller.set_size_request(-1, 160)
        scroller.set_shadow_type(gtk.SHADOW_NONE);
        scroller.set_policy(gtk.POLICY_NEVER, gtk.POLICY_AUTOMATIC);
        scroller.add_with_viewport(self.table)

        panel.pack_start(scroller, True, True)
        self.add(panel)
        self.refresh_id = glib.timeout_add(1, lambda: self.update())

    def update(self):
        #self.status_label.set_markup(s)
        return True
        
    def patch_combo_changed(self, combo, channel):
        cbox.do_cmd(self.path + "/set_patch", None, [int(channel), int(self.patches[combo.get_active()][1])])

class PluginWindow(gtk.Window):
    def __init__(self, instrument, output, plugin_name, main_window):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_UTILITY)
        self.set_transient_for(main_window)
        self.path = "/instr/%s/insert%s/engine" % (instrument, "" if output == 1 else str(output))
        self.set_title("%s - %s" % (plugin_name, instrument))

class PhaserWindow(PluginWindow):
    def __init__(self, instrument, output, main_window):
        PluginWindow.__init__(self, instrument, output, "Phaser", main_window)
        values = GetThings(self.path + "/status", ["center_freq", "mod_depth", "fb_amt", "lfo_freq", "stereo_phase", "stages", "wet_dry"], [])
        t = gtk.Table(2, 7)
        add_slider_row(t, 0, "Center", self.path, values, "center_freq", 100, 20000)
        add_slider_row(t, 1, "Mod depth", self.path, values, "mod_depth", 0, 4000)
        add_slider_row(t, 2, "Feedback", self.path, values, "fb_amt", -1, 1)
        add_slider_row(t, 3, "LFO frequency", self.path, values, "lfo_freq", 0, 20)
        add_slider_row(t, 4, "Stereo", self.path, values, "stereo_phase", 0, 360)
        add_slider_row(t, 5, "Wet/dry", self.path, values, "wet_dry", 0, 1)
        add_slider_row(t, 6, "Stages", self.path, values, "stages", 1, 12, setter = adjustment_changed_int)
        self.add(t)

class ChorusWindow(PluginWindow):
    def __init__(self, instrument, output, main_window):
        PluginWindow.__init__(self, instrument, output, "Chorus", main_window)
        values = GetThings(self.path + "/status", ["min_delay", "mod_depth", "lfo_freq", "stereo_phase", "stages", "wet_dry"], [])
        t = gtk.Table(2, 5)
        add_slider_row(t, 0, "Min. delay", self.path, values, "min_delay", 1, 20)
        add_slider_row(t, 1, "Mod depth", self.path, values, "mod_depth", 1, 20)
        add_slider_row(t, 2, "LFO frequency", self.path, values, "lfo_freq", 0, 20)
        add_slider_row(t, 3, "Stereo", self.path, values, "stereo_phase", 0, 360)
        add_slider_row(t, 4, "Wet/dry", self.path, values, "wet_dry", 0, 1)
        self.add(t)

class ReverbWindow(PluginWindow):
    def __init__(self, instrument, output, main_window):
        PluginWindow.__init__(self, instrument, output, "Reverb", main_window)
        values = GetThings(self.path + "/status", ["decay_time", "dry_amt", "wet_amt", "lowpass", "highpass", "diffusion"], [])
        t = gtk.Table(2, 6)
        add_slider_row(t, 0, "Decay time", self.path, values, "decay_time", 500, 5000)
        add_slider_row(t, 1, "Dry amount", self.path, values, "dry_amt", -100, 12)
        add_slider_row(t, 2, "Wet amount", self.path, values, "wet_amt", -100, 12)
        add_slider_row(t, 3, "Lowpass", self.path, values, "lowpass", 300, 20000)
        add_slider_row(t, 4, "Highpass", self.path, values, "highpass", 30, 2000)
        add_slider_row(t, 5, "Diffusion", self.path, values, "diffusion", 0.2, 0.8)
        self.add(t)

class FBRWindow(PluginWindow):
    def __init__(self, instrument, output, main_window):
        PluginWindow.__init__(self, instrument, output, "Feedback Reducer", main_window)
        values = GetThings(self.path + "/status", ["%active", "%center", "%q", "%gain"], [])
        t = gtk.Table(4, 17)
        cols = [
            ("Active", 0, 1, "active", 'checkbox'), 
            ("Center Freq", 10, 20000, "center", 'slider'),
            ("Filter Q", 0.1, 10, "q", 'slider'),
            ("Gain", -24, 24, "gain", 'slider'),
        ]
        for i in range(len(cols)):
            par = cols[i]
            t.attach(bold_label(par[0], halign=0.5), i, i + 1, 0, 1, gtk.SHRINK | gtk.FILL)
            for j in range(16):
                value = getattr(values, par[3])[j]
                if par[4] == 'slider':
                    adj = gtk.Adjustment(value, par[1], par[2], 1, 6, 0)
                    adj.connect("value_changed", adjustment_changed_float, self.path + "/" + par[3], int(j))
                    t.attach(standard_hslider(adj), i, i + 1, j + 1, j + 2, gtk.EXPAND | gtk.FILL)
                else:
                    cb = gtk.CheckButton(par[0])
                    cb.set_active(value > 0)
                    cb.connect("clicked", checkbox_changed_bool, self.path + "/" + par[3], int(j))
                    t.attach(cb, i, i + 1, j + 1, j + 2, gtk.SHRINK | gtk.FILL)
                
        
        self.add(t)

engine_window_map = {
    'phaser': PhaserWindow,
    'chorus': ChorusWindow,
    'reverb' : ReverbWindow,
    'feedback_reducer': FBRWindow,
    'stream_player' : StreamWindow,
    'fluidsynth' : FluidsynthWindow
}

class MainWindow(gtk.Window):
    def __init__(self):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.vbox = gtk.VBox(spacing = 5)
        self.add(self.vbox)
        self.create()
        self.refresh_id = glib.timeout_add(30, lambda: self.update())

    def create_master(self, scene):
        self.scene_list = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_STRING)
        for s in cfg_sections("scene:"):
            title = cfg_get(s, "title")
            if title is None:
                self.scene_list.append((s[6:], ""))
            else:
                self.scene_list.append((s[6:], "(%s)" % title))
        
        self.master_info = left_label("")
        self.timesig_info = left_label("")
        
        t = gtk.Table(2, 6)
        t.set_col_spacings(5)
        t.set_row_spacings(5)
        
        t.attach(bold_label("Scene"), 0, 1, 0, 1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        cb = gtk.ComboBox(self.scene_list)
        cell = gtk.CellRendererText()
        cb.pack_start(cell, False)
        cb.add_attribute(cell, 'text', 0)
        cell = gtk.CellRendererText()
        cell.props.foreground = "blue"
        cb.pack_end(cell, True)
        cb.add_attribute(cell, 'text', 1)
        cb.connect('changed', self.scene_combo_changed)
        t.attach(cb, 1, 2, 0, 1, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        
        t.attach(bold_label("Title"), 0, 1, 1, 2, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        t.attach(left_label(scene.title), 1, 2, 1, 2, gtk.SHRINK | gtk.FILL, gtk.SHRINK)
        
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

    def create(self):
        rt = GetThings("/rt/status", ['audio_channels'], [])
        scene = GetThings("/scene/status", ['*layer', '*instrument', 'name', 'title', 'transpose'], [])
        
        self.nb = gtk.Notebook()
        self.vbox.add(self.nb)
        self.nb.append_page(self.create_master(scene), gtk.Label("Master"))
        self.create_instrument_pages(scene, rt)

    def create_instrument_pages(self, scene, rt):
        for i in scene.instrument:
            idata = GetThings("/instr/%s/status" % i[0], ['%gain', '%output', 'aux_offset', '%insert_engine'], [])
            #attribs = GetThings("/scene/instr_info", ['engine', 'name'], [i])
            #markup += '<b>Instrument %d:</b> engine %s, name %s\n' % (i, attribs.engine, attribs.name)
            b = gtk.VBox(spacing = 5)
            b.set_border_width(5)
            b.pack_start(gtk.Label("Engine: %s" % i[1]), False, False)
            b.pack_start(gtk.HSeparator(), False, False)
            t = gtk.Table(1 + len(idata.output), 5)
            t.set_col_spacings(5)
            t.attach(bold_label("Instr. output", 0.5), 0, 1, 0, 1, gtk.SHRINK, gtk.SHRINK)
            t.attach(bold_label("Send to", 0.5), 1, 2, 0, 1, gtk.SHRINK, gtk.SHRINK)
            t.attach(bold_label("Gain [dB]", 0.5), 2, 3, 0, 1, 0, gtk.SHRINK)
            t.attach(bold_label("Effect", 0.5), 3, 5, 0, 1, 0, gtk.SHRINK)
            b.pack_start(t, False, False)
            y = 1
            for o in idata.output.keys():
                if 2 * (o - 1) < idata.aux_offset:
                    output_name = "Out %s" % o
                else:
                    output_name = "Aux %s" % (o - idata.aux_offset / 2)
                t.attach(gtk.Label(output_name), 0, 1, y, y + 1, gtk.SHRINK, gtk.SHRINK)
                ls = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
                for out in range(0, rt.audio_channels[1]/2):
                    ls.append(("Out %s/%s" % (out * 2 + 1, out * 2 + 2), out))
                cb = gtk.ComboBox(ls)
                cb.set_active(idata.output[o] - 1)
                cell = gtk.CellRendererText()
                cb.pack_start(cell, True)
                cb.add_attribute(cell, 'text', 0)
                cb.connect('changed', output_combo_value_changed, i[0], o)
                t.attach(cb, 1, 2, y, y + 1, gtk.SHRINK, gtk.SHRINK)
                adj = gtk.Adjustment(idata.gain[o], -96, 12, 1, 6, 0)
                adj.connect('value_changed', adjustment_changed_float, "/instr/%s/set_gain" % i[0], int(o))
                t.attach(standard_hslider(adj), 2, 3, y, y + 1, gtk.EXPAND | gtk.FILL, gtk.SHRINK)
                fx = gtk.Label(idata.insert_engine[o])
                t.attach(fx, 3, 4, y, y + 1, 0, gtk.SHRINK)
                if idata.insert_engine[o] in engine_window_map:
                    fx = gtk.Button("_Edit")
                    t.attach(fx, 4, 5, y, y + 1, 0, gtk.SHRINK)
                    fx.connect("clicked", lambda button, instr, output, wclass, main_window: wclass(instr, output, main_window).show_all(), i[0], o, engine_window_map[idata.insert_engine[o]], self)
                y += 1
            if i[1] in engine_window_map:
                b.pack_start(gtk.HSeparator(), False, False)
                b.pack_start(engine_window_map[i[1]](i[0], "/instr/%s/engine" % i[0]), True, True)
            self.nb.append_page(b, gtk.Label(i[0]))
        self.update()
        
    def delete_instrument_pages(self):
        while self.nb.get_n_pages() > 1:
            self.nb.remove_page(self.nb.get_n_pages() - 1)
        
    def update(self):
        master = GetThings("/master/status", ['pos', 'tempo', 'timesig'], [])
        self.master_info.set_markup('%s' % master.pos)
        self.timesig_info.set_markup("%s/%s" % tuple(master.timesig))
        self.tempo_adj.set_value(master.tempo)
        return True
        
    def scene_combo_changed(self, cb):
        cbox.do_cmd("/scene/load", None, [self.scene_list[cb.get_active()][0]])
        self.delete_instrument_pages()
        rt = GetThings("/rt/status", ['audio_channels'], [])
        scene = GetThings("/scene/status", ['*layer', '*instrument', 'name', 'title', 'transpose'], [])
        self.create_instrument_pages(scene, rt)
        self.nb.show_all()
        return True

def do_quit(window, event):
    gtk.main_quit()

w = MainWindow()
w.set_title("My UI")
w.show_all()
w.connect('delete_event', do_quit)

gtk.main()

