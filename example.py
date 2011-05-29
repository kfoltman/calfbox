import cbox
import pygtk
import gtk
import glib

def callback(cmd, cb, args):
    print cmd, cb, args

class GetThings:
    def __init__(self, cmd, anames, args):
        for i in anames:
            if i.startswith("*"):
                setattr(self, i[1:], [])
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
                    setattr(self, cmd, getattr(self, cmd) + [args[0]])
                else:
                    setattr(self, cmd, getattr(self, cmd) + [args])
        cbox.do_cmd(cmd, update_callback, args)

class StreamWindow(gtk.VBox):
    def __init__(self, instrument):
        gtk.Widget.__init__(self)
        self.path = "/instr/%s" % instrument
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
        scene = GetThings("/scene/status", ['*layer', '*instrument', 'name', 'title'], [])
        
        l = gtk.Label()
        l.set_markup('<b>Scene:</b> %s\n<b>Title:</b> %s\n' % (scene.name, scene.title))
        self.vbox.add(l)
        
        for i in scene.instrument:
            #attribs = GetThings("/scene/instr_info", ['engine', 'name'], [i])
            #markup += '<b>Instrument %d:</b> engine %s, name %s\n' % (i, attribs.engine, attribs.name)
            if i[1] == 'stream_player':
                f = gtk.Frame(label = 'Instrument %s (%s)' % (i[0], i[1]))
                f.add(StreamWindow(i[0]))
                self.vbox.add(f)
            else:
                self.vbox.add(gtk.HSeparator())
                self.vbox.add(gtk.Label('Instrument %s (%s)' % (i[0], i[1])))

def do_quit(window, event):
    gtk.main_quit()

w = MainWindow()
w.set_title("My UI")
w.show_all()
w.connect('delete_event', do_quit)

gtk.main()

