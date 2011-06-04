import cbox
import pygtk
import gtk
import glib
import gobject

def callback(cmd, cb, args):
    print cmd, cb, args

def bold_label(text):
    l = gtk.Label()
    l.set_markup("<b>%s</b>" % text)
    l.set_alignment(1, 0.5)
    return l

def left_label(text):
    l = gtk.Label()
    l.set_markup(text)
    l.set_alignment(0, 0.5)
    return l

def effect_value_changed_int(adjustment, path):
    cbox.do_cmd(path, None, [int(adjustment.get_value())])

def effect_value_changed_float(adjustment, path):
    cbox.do_cmd(path, None, [float(adjustment.get_value())])

def gain_value_changed(adjustment, instr, output_pair):
    cbox.do_cmd("/instr/%s/set_gain" % instr, None, [output_pair, adjustment.get_value()])

def tempo_value_changed(adjustment):
    cbox.do_cmd("/master/set_tempo", None, [adjustment.get_value()])

def output_combo_value_changed(combo, instr, output_pair):
    if combo.get_active() != -1:
        cbox.do_cmd("/instr/%s/set_output" % instr, None, [output_pair, 1 + combo.get_active()])

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

class StreamWindow(gtk.VBox):
    def __init__(self, instrument):
        gtk.Widget.__init__(self)
        self.path = "/instr/%s/engine" % instrument
        self.status_label = gtk.Label("")
        self.play_button = gtk.Button(label = "_Play")
        self.rewind_button = gtk.Button(label = "_Rewind")
        self.stop_button = gtk.Button(label = "_Stop")
        panel = gtk.VBox(spacing=5)
        panel.add(self.status_label)
        buttons = gtk.HBox(spacing = 5)
        buttons.add(self.play_button)
        buttons.add(self.rewind_button)
        buttons.add(self.stop_button)
        panel.add(buttons)
        self.add(panel)
        self.play_button.connect('clicked', lambda x: cbox.do_cmd("%s/play" % self.path, None, []))
        self.rewind_button.connect('clicked', lambda x: cbox.do_cmd("%s/seek" % self.path, None, [0]))
        self.stop_button.connect('clicked', lambda x: cbox.do_cmd("%s/stop" % self.path, None, []))
        self.refresh_id = glib.timeout_add(30, lambda: self.update())

    def update(self):
        attribs = GetThings("%s/status" % self.path, ['filename', 'pos', 'playing'], [])
        self.status_label.set_markup("<b>File:</b> %s\n<b>Pos:</b> %s" % (attribs.filename, attribs.pos))
        return True

def add_slider_row(t, row, label, path, values, item, min, max, setter = effect_value_changed_float):
    t.attach(bold_label(label), 0, 1, row, row+1, gtk.SHRINK | gtk.FILL)
    adj = gtk.Adjustment(getattr(values, item), min, max, 1, 6, 0)
    adj.connect("value_changed", lambda adj: setter(adj, path + "/" + item))
    hsc = gtk.HScale(adj)
    hsc.set_size_request(100, -1)
    t.attach(hsc, 1, 2, row, row+1)

class PhaserWindow(gtk.Window):
    def __init__(self, instrument, output):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.path = "/instr/%s/insert%s/engine" % (instrument, "" if output == 1 else str(output))
        self.set_title("Phaser - %s" % instrument)
        values = GetThings(self.path + "/status", ["center_freq", "mod_depth", "fb_amt", "lfo_freq", "stereo_phase", "stages", "wet_dry"], [])
        t = gtk.Table(1, 7)
        add_slider_row(t, 0, "Center", self.path, values, "center_freq", 100, 20000)
        add_slider_row(t, 1, "Mod depth", self.path, values, "mod_depth", 0, 4000)
        add_slider_row(t, 2, "Feedback", self.path, values, "fb_amt", -1, 1)
        add_slider_row(t, 3, "LFO frequency", self.path, values, "lfo_freq", 0, 20)
        add_slider_row(t, 4, "Stereo", self.path, values, "stereo_phase", 0, 360)
        add_slider_row(t, 5, "Wet/dry", self.path, values, "wet_dry", 0, 1)
        add_slider_row(t, 6, "Stages", self.path, values, "stages", 1, 12, setter = effect_value_changed_int)
        self.add(t)

engine_window_map = {
    'phaser': PhaserWindow,
}

class MainWindow(gtk.Window):
    def __init__(self):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.vbox = gtk.VBox(spacing = 5)
        self.add(self.vbox)
        self.create()
        self.refresh_id = glib.timeout_add(30, lambda: self.update())
        
    def create(self):
        rt = GetThings("/rt/status", ['audio_channels'], [])
        scene = GetThings("/scene/status", ['*layer', '*instrument', 'name', 'title'], [])
        
        self.master_info = left_label("")
        self.timesig_info = left_label("")
        
        t = gtk.Table(2, 5)
        t.set_col_spacings(5)
        t.set_row_spacings(5)
        
        t.attach(bold_label("Scene"), 0, 1, 0, 1, gtk.SHRINK | gtk.FILL)
        t.attach(left_label(scene.name), 1, 2, 0, 1)
        
        t.attach(bold_label("Title"), 0, 1, 1, 2, gtk.SHRINK | gtk.FILL)
        t.attach(left_label(scene.title), 1, 2, 1, 2)
        
        t.attach(bold_label("Play pos"), 0, 1, 2, 3, gtk.SHRINK | gtk.FILL)
        t.attach(self.master_info, 1, 2, 2, 3)
        
        t.attach(bold_label("Time sig"), 0, 1, 3, 4, gtk.SHRINK)
        t.attach(self.timesig_info, 1, 2, 3, 4)
        
        t.attach(bold_label("Tempo"), 0, 1, 4, 5, gtk.SHRINK | gtk.FILL)
        self.tempo_adj = gtk.Adjustment(40, 40, 300, 1, 5, 0)
        self.tempo_adj.connect('value_changed', tempo_value_changed)
        t.attach(gtk.HScale(self.tempo_adj), 1, 2, 4, 5)
        
        self.vbox.add(t)
        
        for i in scene.instrument:
            idata = GetThings("/instr/%s/status" % i[0], ['%gain', '%output', 'aux_offset', '%insert_engine'], [])
            #attribs = GetThings("/scene/instr_info", ['engine', 'name'], [i])
            #markup += '<b>Instrument %d:</b> engine %s, name %s\n' % (i, attribs.engine, attribs.name)
            f = gtk.Frame(label = 'Instrument %s' % i[0])
            b = gtk.VBox(spacing = 5)
            b.set_border_width(5)
            f.add(b)
            b.add(gtk.Label("Engine: %s" % i[1]))
            b.add(gtk.HSeparator())
            t = gtk.Table(1 + len(idata.output), 5)
            t.attach(bold_label("Instr. output"), 0, 1, 0, 1, gtk.SHRINK)
            t.attach(bold_label("Send to"), 1, 2, 0, 1, gtk.SHRINK)
            t.attach(bold_label("Gain [dB]"), 2, 3, 0, 1)
            t.attach(bold_label("Effect"), 3, 5, 0, 1)
            b.add(t)
            y = 1
            for o in idata.output.keys():
                if 2 * (o - 1) < idata.aux_offset:
                    output_name = "Out %s" % o
                else:
                    output_name = "Aux %s" % (o - idata.aux_offset / 2)
                t.attach(gtk.Label(output_name), 0, 1, y, y + 1, gtk.SHRINK)
                ls = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
                for out in range(0, rt.audio_channels[1]/2):
                    ls.append(("Out %s/%s" % (out * 2 + 1, out * 2 + 2), out))
                cb = gtk.ComboBox(ls)
                cb.set_active(idata.output[o] - 1)
                cell = gtk.CellRendererText()
                cb.pack_start(cell, True)
                cb.add_attribute(cell, 'text', 0)
                cb.connect('changed', output_combo_value_changed, i[0], o)
                t.attach(cb, 1, 2, y, y + 1, gtk.SHRINK)
                adj = gtk.Adjustment(idata.gain[o], -96, 0, 1, 6, 0)
                adj.connect('value_changed', gain_value_changed, i[0], o)
                t.attach(gtk.HScale(adj), 2, 3, y, y + 1)
                fx = gtk.Label(idata.insert_engine[o])
                t.attach(fx, 3, 4, y, y + 1)
                if idata.insert_engine[o] in engine_window_map:
                    fx = gtk.Button("_Edit")
                    t.attach(fx, 4, 5, y, y + 1)
                    fx.connect("clicked", lambda button, instr, output, wclass: wclass(instr, output).show_all(), i[0], o, engine_window_map[idata.insert_engine[o]])
                y += 1
            if i[1] == 'stream_player':
                b.add(gtk.HSeparator())
                b.add(StreamWindow(i[0]))
            self.vbox.add(f)
        self.update()
        
    def update(self):
        master = GetThings("/master/status", ['pos', 'tempo', 'timesig'], [])
        self.master_info.set_markup('%s' % master.pos)
        self.timesig_info.set_markup("%s/%s" % tuple(master.timesig))
        self.tempo_adj.set_value(master.tempo)
        return True

def do_quit(window, event):
    gtk.main_quit()

w = MainWindow()
w.set_title("My UI")
w.show_all()
w.connect('delete_event', do_quit)

gtk.main()

