import cbox
import glob
import os
from gui_tools import *

#sample_dir = "/media/resources/samples/dooleydrums/"
sample_dir = cbox.Config.get("init", "sample_dir")

class SampleFilesModel(gtk.ListStore):
    def __init__(self):
        gtk.ListStore.__init__(self, gobject.TYPE_STRING, gobject.TYPE_STRING)
        
    def refresh(self):
        self.clear()
        self.append(("","<empty>"))
        for f in glob.glob("%s/*" % sample_dir):
            print f
            if not f.lower().endswith(".wav"):
                continue
            self.append((f,os.path.basename(f)))

class KeyModel(object):
    def __init__(self, sample, filename):
        self.sample = sample
        self.filename = filename

class BankModel(object):
    def __init__(self):
        self.keys = {}
    def __getitem__(self, key):
        if key in self.keys:
            return self.keys[key]
        return None
    def __setitem__(self, key, value):
        if value.filename == "":
            if key in self.keys:
                del self.keys[key]
        else:
            self.keys[key] = value
    def to_sfz(self):
        s = ""
        for key in self.keys:
            s += "<region> key=%d sample=%s\n" % (key, self.keys[key].filename)
        return s

class PadButton(gtk.Button):
    def __init__(self, controller, model, key):
        gtk.Button.__init__(self, use_underline = False)
        self.controller = controller
        self.model = model
        self.key = key
        self.set_size_request(100, 100)
        self.update_label()
        self.drag_dest_set(gtk.DEST_DEFAULT_ALL, [("text/plain", 0, 1)], gtk.gdk.ACTION_COPY)
        self.connect('drag_data_received', self.drag_data_received)
    def drag_data_received(self, widget, context, x, y, selection, info, etime):
        km = KeyModel(*(selection.data.split("|")))
        self.model[self.key] = km
        self.update_label()
        self.controller.set_kit()
    def update_label(self):
        data = self.model[self.key]
        if data == None:
            self.set_label("-")
        else:
            self.get_child().set_markup("<small>%s</small>" % data.sample)
            self.get_child().set_line_wrap(True)

class PadTable(gtk.Table):
    def __init__(self, controller, model, rows, columns):
        gtk.Table.__init__(self, rows, columns, True)
        
        for r in range(0, rows):
            for c in range(0, columns):
                b = PadButton(controller, model, 36 + (rows - r - 1) * columns + c)
                a = gtk.Alignment(0.5, 0.5)
                a.add(b)
                self.attach(a, c, c + 1, r, r + 1)

class FileView(gtk.TreeView):
    def __init__(self):
        self.files_model = SampleFilesModel()
        self.files_model.refresh()
        gtk.TreeView.__init__(self, self.files_model)
        self.insert_column_with_attributes(0, "Name", gtk.CellRendererText(), text=1)
        self.set_cursor((0,))
        self.enable_model_drag_source(gtk.gdk.BUTTON1_MASK, [("text/plain", 0, 1)], gtk.gdk.ACTION_COPY)
        self.connect('cursor-changed', self.cursor_changed)
        self.connect('drag-data-get', self.drag_data_get)
        
    def cursor_changed(self, w):
        c = self.get_cursor()
        if c[0] is not None:
            fn = self.files_model[c[0][0]][0]
            if fn != "":
                cbox.do_cmd("/instr/_preview_sample/engine/load", None, [fn, -1])
                cbox.do_cmd("/instr/_preview_sample/engine/play", None, [])
            else:
                cbox.do_cmd("/instr/_preview_sample/engine/unload", None, [])
            
    def drag_data_get(self, treeview, context, selection, target_id, etime):
        cursor = treeview.get_cursor()
        if cursor is not None:
            c = cursor[0][0]
            fr = self.files_model[c]
            selection.set('text/plain', 8, str(fr[1]+"|"+fr[0]))
        
class EditorDialog(gtk.Dialog):
    def __init__(self, parent):
        gtk.Dialog.__init__(self, "Drum kit editor", parent, gtk.DIALOG_MODAL, 
            (gtk.STOCK_OK, gtk.RESPONSE_OK))
        self.set_default_response(gtk.RESPONSE_OK)
        self.hbox = gtk.HBox()
        
        self.bank_model = BankModel()
        self.tree = FileView()
        sw = gtk.ScrolledWindow()
        sw.add_with_viewport(self.tree)
        self.hbox.pack_start(sw, True, True)
        sw.set_size_request(240, -1)
        self.pads = PadTable(self, self.bank_model, 4, 4)
        self.hbox.pack_start(self.pads, True, True)
        self.vbox.pack_start(self.hbox)
        self.vbox.show_all()
        self.set_kit()

    def set_kit(self):
        cbox.do_cmd("/instr/_preview_kit/engine/load_patch_from_string", None, [0, "", self.bank_model.to_sfz(), "Preview"])
        