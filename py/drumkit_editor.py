import cbox
import glob
import os
from gui_tools import *
import sfzparser

#sample_dir = "/media/resources/samples/dooleydrums/"
sample_dir = cbox.Config.get("init", "sample_dir")

####################################################################################################################################################

class SampleDirsModel(gtk.ListStore):
    def __init__(self):
        gtk.ListStore.__init__(self, gobject.TYPE_STRING, gobject.TYPE_STRING)
        for entry in cbox.Config.keys("sample_dirs"):
            path = cbox.Config.get("sample_dirs", entry)
            self.append((entry, path))

####################################################################################################################################################

class SampleFilesModel(gtk.ListStore):
    def __init__(self):
        gtk.ListStore.__init__(self, gobject.TYPE_STRING, gobject.TYPE_STRING)
        
    def refresh(self, sample_dir):
        print sample_dir
        self.clear()
        self.append(("","<empty>"))
        filelist = glob.glob("%s/*" % sample_dir)
        for f in sorted(filelist):
            if not f.lower().endswith(".wav"):
                continue
            self.append((f,os.path.basename(f)))

####################################################################################################################################################

class KeyModelPath(object):
    def __init__(self, controller, var = None):
        self.controller = controller
        self.var = var
        self.args = []
    def plus(self, var):
        if self.var is not None:
            print "Warning: key model plus used twice with %s and %s" % (self.var, var)
        return KeyModelPath(self.controller, var)
    def set(self, value):
        model = self.controller.get_current_layer_model()
        oldval = model.attribs[self.var]
        model.attribs[self.var] = value
        if value != oldval:
            print "%s: set %s to %s" % (self.controller, self.var, value)
            self.controller.update_kit_later()

####################################################################################################################################################

layer_attribs = {
    'volume' : 0.0,
    'pan' : 0.0,
    'ampeg_attack' : 0.001,
    'ampeg_hold' : 0.001,
    'ampeg_decay' : 0.001,
    'ampeg_sustain' : 100.0,
    'ampeg_release' : 0.1,
    'tune' : 0.0,
    'transpose' : 0,
    'cutoff' : 22000.0,
    'resonance' : 0.7,
    'fileg_depth' : 0.0,
    'fileg_attack' : 0.001,
    'fileg_hold' : 0.001,
    'fileg_decay' : 0.001,
    'fileg_sustain' : 100.0,
    'fileg_release' : 0.1,
    'lovel' : 1,
    'hivel' : 127,
    'group' : 0,
    'off_by' : 0,
}

####################################################################################################################################################

class KeySampleModel(object):
    def __init__(self, key, sample, filename):
        self.key = key
        self.sample = sample
        self.filename = filename
        self.mode = "one_shot"
        self.attribs = layer_attribs.copy()
    def set_sample(self, sample, filename):
        self.sample = sample
        self.filename = filename
    def to_sfz(self):
        if self.filename == '':
            return ""
        s = "<region> key=%d sample=%s loop_mode=%s" % (self.key, self.filename, self.mode)
        s += "".join([" %s=%s" % item for item in self.attribs.iteritems()])
        return s + "\n"
    def to_markup(self):
        return "<small>%s</small>" % self.sample

####################################################################################################################################################

class KeyModel(gtk.ListStore):
    def __init__(self):
        gtk.ListStore.__init__(self, gobject.TYPE_STRING, gobject.TYPE_PYOBJECT)
    def to_sfz(self):
        return "".join([ksm.to_sfz() for name, ksm in self])
    def to_markup(self):
        return "\n".join([ksm.to_markup() for name, ksm in self])

####################################################################################################################################################

class BankModel(dict):
    def __init__(self):
        dict.__init__(self)
        self.clear()
    def to_sfz(self):
        s = ""
        for key in self:
            s += self[key].to_sfz()
        return s
    def clear(self):
        dict.clear(self)
        for b in range(36, 36 + 16):
            self[b] = KeyModel()
    def from_sfz(self, data, path):
        self.clear()
        sfz = sfzparser.SFZ()
        sfz.parse(data)
        for r in sfz.regions:
            rdata = r.merged()
            if ('key' in rdata) and ('sample' in rdata) and (rdata['sample'] != ''):
                key = int(rdata['key'])
                sample = rdata['sample']
                sample_short = os.path.basename(sample)
                if key in self:
                    ksm = KeySampleModel(key, sample_short, sfzparser.find_sample_in_path(path, sample))
                    for k, v in rdata.iteritems():
                        if k in ksm.attribs:
                            if type(layer_attribs[k]) is float:
                                ksm.attribs[k] = float(v)
                            elif type(layer_attribs[k]) is int:
                                ksm.attribs[k] = int(float(v))
                            else:
                                ksm.attribs[k] = v
                    self[key].append((sample_short, ksm))

####################################################################################################################################################

class LayerListView(gtk.TreeView):
    def __init__(self, controller):
        gtk.TreeView.__init__(self, None)
        self.controller = controller
        self.insert_column_with_attributes(0, "Name", gtk.CellRendererText(), text=0)
        self.set_cursor((0,))
        #self.enable_model_drag_source(gtk.gdk.BUTTON1_MASK, [("text/plain", 0, 1)], gtk.gdk.ACTION_COPY)
        self.connect('cursor-changed', self.cursor_changed)
        #self.connect('drag-data-get', self.drag_data_get)
    def cursor_changed(self, w):
        self.controller.on_layer_changed()

####################################################################################################################################################

class LayerEditor(gtk.VBox):
    def __init__(self, controller, bank_model):
        gtk.VBox.__init__(self)
        self.table = gtk.Table(len(self.fields) + 1, 2)
        self.table.set_size_request(240, -1)
        self.controller = controller
        self.bank_model = bank_model
        self.name_widget = gtk.Label()
        self.table.attach(self.name_widget, 0, 2, 0, 1)
        self.refreshers = []
        for i in range(len(self.fields)):
            self.refreshers.append(self.fields[i].add_row(self.table, i + 1, KeyModelPath(controller), None))
            #self.table.attach(left_label(self.fields[i].label), 0, 1, i + 1, i + 2)
        self.pack_start(self.table, False, False)
        
    def refresh(self):
        data = self.controller.get_current_layer_model()
        if data is None:
            self.name_widget.set_text("")
        else:
            self.name_widget.set_text(data.sample)
            data = data.attribs
        for r in self.refreshers:
            r(data)

    fields = [
        SliderRow("Volume", "volume", -100, 0),
        SliderRow("Pan", "pan", -100, 100),
        SliderRow("Tune", "tune", -100, 100),
        IntSliderRow("Transpose", "transpose", -48, 48),
        IntSliderRow("Low velocity", "lovel", 1, 127),
        IntSliderRow("High velocity", "hivel", 1, 127),
        MappedSliderRow("Amp Attack", "ampeg_attack", env_mapper),
        MappedSliderRow("Amp Hold", "ampeg_hold", env_mapper),
        MappedSliderRow("Amp Decay", "ampeg_decay", env_mapper),
        SliderRow("Amp Sustain", "ampeg_sustain", 0, 100),
        MappedSliderRow("Amp Release", "ampeg_release", env_mapper),
        MappedSliderRow("Flt Cutoff", "cutoff", filter_freq_mapper),
        MappedSliderRow("Flt Resonance", "resonance", LogMapper(0.707, 16, "%0.1f x")),
        SliderRow("Flt Depth", "fileg_depth", -4800, 4800),
        MappedSliderRow("Flt Attack", "fileg_attack", env_mapper),
        MappedSliderRow("Flt Hold", "fileg_hold", env_mapper),
        MappedSliderRow("Flt Decay", "fileg_decay", env_mapper),
        SliderRow("Flt Sustain", "fileg_sustain", 0, 100),
        MappedSliderRow("Flt Release", "fileg_release", env_mapper),
        IntSliderRow("Group", "group", 0, 15),
        IntSliderRow("Off by group", "off_by", 0, 15),
    ]
    
####################################################################################################################################################

class PadButton(gtk.RadioButton):
    def __init__(self, controller, bank_model, key):
        gtk.RadioButton.__init__(self, use_underline = False)
        self.set_mode(False)
        self.controller = controller
        self.bank_model = bank_model
        self.key = key
        self.set_size_request(100, 100)
        self.update_label()
        self.drag_dest_set(gtk.DEST_DEFAULT_ALL, [("text/plain", 0, 1)], gtk.gdk.ACTION_COPY)
        self.connect('drag_data_received', self.drag_data_received)
        self.connect('toggled', lambda widget: widget.controller.on_pad_selected(widget) if widget.get_active() else None)
        self.connect('pressed', self.on_clicked)
    def drag_data_received(self, widget, context, x, y, selection, info, etime):
        sample, filename = selection.data.split("|")
        self.bank_model[self.key].append((sample, KeySampleModel(self.key, sample, filename)))
        self.update_label()
        self.controller.on_sample_dragged(self)
    def update_label(self):
        data = self.bank_model[self.key]
        if data == None:
            self.set_label("-")
        else:
            self.set_label("-")
            self.get_child().set_markup(data.to_markup())
            self.get_child().set_line_wrap(True)
    def on_clicked(self, w):
        cbox.do_cmd('/play_note', None, [1, self.key, 127])

####################################################################################################################################################

class PadTable(gtk.Table):
    def __init__(self, controller, bank_model, rows, columns):
        gtk.Table.__init__(self, rows, columns, True)
        
        self.keys = {}
        group = None
        for r in range(0, rows):
            for c in range(0, columns):
                key = 36 + (rows - r - 1) * columns + c
                b = PadButton(controller, bank_model, key)
                b.set_group(group)
                a = gtk.Alignment(0.5, 0.5)
                a.add(b)
                group = b
                self.attach(a, c, c + 1, r, r + 1)
                self.keys[key] = b
    def refresh(self):
        for pad in self.keys.values():
            pad.update_label()

####################################################################################################################################################

class FileView(gtk.TreeView):
    def __init__(self):
        self.files_model = SampleFilesModel()
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

####################################################################################################################################################

class EditorDialog(gtk.Dialog):
    def __init__(self, parent):
        gtk.Dialog.__init__(self, "Drum kit editor", parent, gtk.DIALOG_MODAL, 
            ())
        self.set_default_response(gtk.RESPONSE_OK)

        self.menu_bar = gtk.MenuBar()
        self.menu_bar.append(create_menu("_Kit", [
            ("_New", self.on_kit_new),
            ("_Open...", self.on_kit_open),
            ("_Save as...", self.on_kit_save_as),
            ("_Close", lambda w: self.response(gtk.RESPONSE_OK)),
        ]))
        self.menu_bar.append(create_menu("_Layer", [
            ("_Delete", self.on_layer_delete),
        ]))
        self.vbox.pack_start(self.menu_bar, False, False)
        
        self.hbox = gtk.HBox(spacing = 5)
        
        self.update_source = None
        self.current_pad = None
        self.dirs_model = SampleDirsModel()
        self.bank_model = BankModel()
        self.tree = FileView()
        self.layer_list = LayerListView(self)
        self.layer_editor = LayerEditor(self, self.bank_model)
        
        combo = gtk.ComboBox(self.dirs_model)
        cell = gtk.CellRendererText()
        combo.pack_start(cell, True)
        combo.add_attribute(cell, 'text', 0)
        combo.connect('changed', lambda cb: self.tree.files_model.refresh(cb.get_model()[cb.get_active()][1]))
        combo.set_active(0)
        sw = gtk.ScrolledWindow()
        sw.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_ALWAYS)
        sw.add(self.tree)
        
        left_box = gtk.VBox(spacing = 5)
        left_box.pack_start(combo, False, False)
        left_box.pack_start(sw)
        self.hbox.pack_start(left_box, True, True)
        sw.set_size_request(200, -1)
        
        self.pads = PadTable(self, self.bank_model, 4, 4)
        self.hbox.pack_start(self.pads, True, True)
        
        right_box = gtk.VBox(spacing = 5)
        sw = gtk.ScrolledWindow()
        sw.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_ALWAYS)
        sw.set_size_request(320, 100)
        sw.set_shadow_type(gtk.SHADOW_ETCHED_IN)
        sw.add(self.layer_list)
        right_box.pack_start(sw, True, True)
        sw = gtk.ScrolledWindow()
        sw.set_size_request(320, 200)
        sw.set_policy(gtk.POLICY_NEVER, gtk.POLICY_ALWAYS)
        sw.add_with_viewport(self.layer_editor)
        right_box.pack_start(sw, True, True)
        self.hbox.pack_start(right_box, True, True)
        
        self.vbox.pack_start(self.hbox)
        self.vbox.show_all()
        widget = self.pads.keys[36]
        widget.set_active(True)
        
        self.update_kit()

    def update_kit(self):
        cbox.do_cmd("/instr/_preview_kit/engine/load_patch_from_string", None, [0, "", self.bank_model.to_sfz(), "Preview"])
        self.update_source = None
        return False
        
    def update_kit_later(self):
        if self.update_source is not None:
            glib.source_remove(self.update_source)
        self.update_source = glib.idle_add(self.update_kit)
        
    def on_sample_dragged(self, widget):
        self.update_kit()
        if widget == self.current_pad:
            self.layer_list.set_cursor(len(self.layer_list.get_model()) - 1)
        #    self.pad_editor.refresh()
        
    def refresh_layers(self):
        self.layer_list.set_model(self.bank_model[self.current_pad.key])
        self.layer_list.set_cursor(0)
        self.layer_editor.refresh()
        
    def on_pad_selected(self, widget):
        self.current_pad = widget
        self.refresh_layers()
        
    def on_layer_changed(self):
        self.layer_editor.refresh()
    
    def on_layer_delete(self, w):
        if self.layer_list.get_cursor()[0] is None:
            return None
        model = self.layer_list.get_model()
        model.remove(model.get_iter(self.layer_list.get_cursor()[0]))
        self.current_pad.update_label()
        self.layer_editor.refresh()
        self.update_kit()
    
    def get_current_layer_model(self):
        if self.layer_list.get_cursor()[0] is None:
            return None
        return self.layer_list.get_model()[self.layer_list.get_cursor()[0]][1]

    def on_kit_new(self, widget):
        self.bank_model.from_sfz('', '')
        self.pads.refresh()
        self.refresh_layers()
        self.update_kit()

    def on_kit_open(self, widget):
        dlg = gtk.FileChooserDialog('Open a pad bank', self, gtk.FILE_CHOOSER_ACTION_OPEN,
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_OPEN, gtk.RESPONSE_APPLY))
        dlg.add_filter(standard_filter(["*.sfz", "*.SFZ"], "SFZ files"))
        dlg.add_filter(standard_filter(["*"], "All files"))
        try:
            if dlg.run() == gtk.RESPONSE_APPLY:
                sfz_data = file(dlg.get_filename(), "r").read()
                self.bank_model.from_sfz(sfz_data, dlg.get_current_folder())
            self.pads.refresh()
            self.refresh_layers()
            self.update_kit()
        finally:
            dlg.destroy()

    def on_kit_save_as(self, widget):
        dlg = gtk.FileChooserDialog('Save a pad bank', self, gtk.FILE_CHOOSER_ACTION_SAVE, 
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_SAVE, gtk.RESPONSE_APPLY))
        dlg.add_filter(standard_filter(["*.sfz", "*.SFZ"], "SFZ files"))
        dlg.add_filter(standard_filter(["*"], "All files"))
        try:
            if dlg.run() == gtk.RESPONSE_APPLY:
                file(dlg.get_filename(), "w").write(self.bank_model.to_sfz())
        finally:
            dlg.destroy()
