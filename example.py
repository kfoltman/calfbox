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
    return l

def gain_value_changed(adjustment, instr, output_pair):
    cbox.do_cmd("/instr/%s/set_gain" % instr, None, [output_pair, adjustment.get_value()])

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

class MainWindow(gtk.Window):
    def __init__(self):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.vbox = gtk.VBox(spacing = 5)
        self.add(self.vbox)
        self.update()
        
    def update(self):
        rt = GetThings("/rt/status", ['audio_channels'], [])
        scene = GetThings("/scene/status", ['*layer', '*instrument', 'name', 'title'], [])
        
        l = gtk.Label()
        l.set_markup('<b>Scene:</b> %s\n<b>Title:</b> %s\nInputs: %s\nOutputs: %s' % (scene.name, scene.title, rt.audio_channels[0], rt.audio_channels[1]))
        self.vbox.add(l)
        
        for i in scene.instrument:
            idata = GetThings("/instr/%s/status" % i[0], ['%gain', '%output', 'aux_offset'], [])
            #attribs = GetThings("/scene/instr_info", ['engine', 'name'], [i])
            #markup += '<b>Instrument %d:</b> engine %s, name %s\n' % (i, attribs.engine, attribs.name)
            f = gtk.Frame(label = 'Instrument %s' % i[0])
            b = gtk.VBox(spacing = 5)
            b.set_border_width(5)
            f.add(b)
            b.add(gtk.Label("Engine: %s" % i[1]))
            b.add(gtk.HSeparator())
            t = gtk.Table(1 + len(idata.output), 3)
            t.attach(bold_label("Instr. output"), 0, 1, 0, 1, gtk.SHRINK)
            t.attach(bold_label("Send to"), 1, 2, 0, 1, gtk.SHRINK)
            t.attach(bold_label("Gain [dB]"), 2, 3, 0, 1)
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
                y += 1
            if i[1] == 'stream_player':
                b.add(gtk.HSeparator())
                b.add(StreamWindow(i[0]))
            self.vbox.add(f)

def do_quit(window, event):
    gtk.main_quit()

w = MainWindow()
w.set_title("My UI")
w.show_all()
w.connect('delete_event', do_quit)

gtk.main()

