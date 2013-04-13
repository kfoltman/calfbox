import cbox
import glob
import os
from gui_tools import *
import sfzparser

#sample_dir = "/media/resources/samples/dooleydrums/"
sample_dir = cbox.Config.get("init", "sample_dir")

####################################################################################################################################################

class SampleDirsModel(Gtk.ListStore):
    def __init__(self):
        Gtk.ListStore.__init__(self, GObject.TYPE_STRING, GObject.TYPE_STRING)
        found = False
        for entry in cbox.Config.keys("sample_dirs"):
            path = cbox.Config.get("sample_dirs", entry)
            self.append((entry, path))
            found = True
        if not found:
            print ("Warning: no sample directories defined. Please add one or more entries of a form: 'name=/path/to/my/samples' to [sample_dirs] section of .cboxrc")
            self.append(("home", os.getenv("HOME")))
            self.append(("/", "/"))
    def has_dir(self, dir):
        return dir in [path for entry, path in self]

####################################################################################################################################################

class SampleFilesModel(Gtk.ListStore):
    def __init__(self, dirs_model):
        self.dirs_model = dirs_model
        self.is_refreshing = False
        Gtk.ListStore.__init__(self, GObject.TYPE_STRING, GObject.TYPE_STRING)
        
    def refresh(self, sample_dir):
        try:
            self.is_refreshing = True
            self.clear()
            if sample_dir is not None:
                if not self.dirs_model.has_dir(sample_dir):
                    self.append((os.path.dirname(sample_dir.rstrip("/")) + "/", "(up)"))
                filelist = sorted(glob.glob("%s/*" % sample_dir))
                for f in sorted(filelist):
                    if os.path.isdir(f) and not self.dirs_model.has_dir(f + "/"):
                        self.append((f + "/", os.path.basename(f)+"/"))
                for f in sorted(filelist):
                    if f.lower().endswith(".wav") and not os.path.isdir(f):
                        self.append((f,os.path.basename(f)))
        finally:
            self.is_refreshing = False

####################################################################################################################################################

class KeyModelPath(object):
    def __init__(self, controller, var = None):
        self.controller = controller
        self.var = var
        self.args = []
    def plus(self, var):
        if self.var is not None:
            print ("Warning: key model plus used twice with %s and %s" % (self.var, var))
        return KeyModelPath(self.controller, var)
    def set(self, value):
        model = self.controller.get_current_layer_model()
        oldval = model.attribs[self.var]
        model.attribs[self.var] = value
        if value != oldval and not self.controller.no_sfz_update:
            print ("%s: set %s to %s" % (self.controller, self.var, value))
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
    'effect1' : 0,
    'effect2' : 0,
    'output' : 0,
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
        s += "".join([" %s=%s" % item for item in self.attribs.items()])
        return s + "\n"
    def to_markup(self):
        return "<small>%s</small>" % self.sample

####################################################################################################################################################

class KeyModel(Gtk.ListStore):
    def __init__(self, key):
        self.key = key
        Gtk.ListStore.__init__(self, GObject.TYPE_STRING, GObject.TYPE_PYOBJECT)
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
            self[b] = KeyModel(b)
    def from_sfz(self, data, path):
        self.clear()
        sfz = sfzparser.SFZ()
        sfz.parse(data)
        for r in sfz.regions:
            rdata = r.merged()
            if ('key' in rdata) and ('sample' in rdata) and (rdata['sample'] != ''):
                key = sfznote2value(rdata['key'])
                sample = rdata['sample']
                sample_short = os.path.basename(sample)
                if key in self:
                    ksm = KeySampleModel(key, sample_short, sfzparser.find_sample_in_path(path, sample))
                    for k, v in rdata.items():
                        if k in ksm.attribs:
                            if type(layer_attribs[k]) is float:
                                ksm.attribs[k] = float(v)
                            elif type(layer_attribs[k]) is int:
                                ksm.attribs[k] = int(float(v))
                            else:
                                ksm.attribs[k] = v
                    self[key].append((sample_short, ksm))

####################################################################################################################################################

class LayerListView(Gtk.TreeView):
    def __init__(self, controller):
        Gtk.TreeView.__init__(self, None)
        self.controller = controller
        self.insert_column_with_attributes(0, "Name", Gtk.CellRendererText(), text=0)
        self.set_cursor((0,))
        #self.enable_model_drag_source(Gdk.ModifierType.BUTTON1_MASK, [("text/plain", 0, 1)], Gdk.DragAction.COPY)
        self.connect('cursor-changed', self.cursor_changed)
        #self.connect('drag-data-get', self.drag_data_get)
        self.drag_dest_set(Gtk.DestDefaults.ALL, [], Gdk.DragAction.COPY)
        self.drag_dest_set_target_list(None)
        self.drag_dest_add_text_targets()
        self.connect('drag_data_received', self.drag_data_received)
    def cursor_changed(self, w):
        self.controller.on_layer_changed()
    def drag_data_received(self, widget, context, x, y, selection, info, etime):
        sample, filename = selection.get_text().split("|")
        pad_model = self.controller.get_current_pad_model()
        pad_model.append((sample, KeySampleModel(pad_model.key, sample, filename)))
        self.controller.current_pad.update_label()
        self.controller.on_sample_dragged(self)

####################################################################################################################################################

class LayerEditor(Gtk.VBox):
    def __init__(self, controller, bank_model):
        Gtk.VBox.__init__(self)
        self.table = Gtk.Table(len(self.fields) + 1, 2)
        self.table.set_size_request(240, -1)
        self.controller = controller
        self.bank_model = bank_model
        self.name_widget = Gtk.Label()
        self.table.attach(self.name_widget, 0, 2, 0, 1)
        self.refreshers = []
        for i in range(len(self.fields)):
            self.refreshers.append(self.fields[i].add_row(self.table, i + 1, KeyModelPath(controller), None))
            #self.table.attach(left_label(self.fields[i].label), 0, 1, i + 1, i + 2)
        self.pack_start(self.table, False, False, 0)
        
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
        SliderRow("Effect 1", "effect1", 0, 100),
        SliderRow("Effect 2", "effect2", 0, 100),
        IntSliderRow("Output", "output", 0, 7),
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

class PadButton(Gtk.RadioButton):
    def __init__(self, controller, bank_model, key):
        Gtk.RadioButton.__init__(self, use_underline = False)
        self.set_mode(False)
        self.controller = controller
        self.bank_model = bank_model
        self.key = key
        self.set_size_request(100, 100)
        self.update_label()
        self.drag_dest_set(Gtk.DestDefaults.ALL, [], Gdk.DragAction.COPY)
        self.drag_dest_set_target_list(None)
        self.drag_dest_add_text_targets()
        self.connect('drag_data_received', self.drag_data_received)
        #self.connect('toggled', lambda widget: widget.controller.on_pad_selected(widget) if widget.get_active() else None)
        self.connect('pressed', self.on_clicked)
    def get_key_model(self):
        return self.bank_model[self.key]
    def drag_data_received(self, widget, context, x, y, selection, info, etime):
        sample, filename = selection.get_text().split("|")
        self.get_key_model().clear()
        self.get_key_model().append((sample, KeySampleModel(self.key, sample, filename)))
        self.update_label()
        self.controller.on_sample_dragged(self)
    def update_label(self):
        data = self.get_key_model()
        if data == None:
            self.set_label("-")
        else:
            self.set_label("-")
            self.get_child().set_markup(data.to_markup())
            self.get_child().set_line_wrap(True)
    def on_clicked(self, w):
        cbox.Document.get_scene().send_midi_event(0x90, self.key, 127)
        cbox.Document.get_scene().send_midi_event(0x80, self.key, 127)
        w.controller.on_pad_selected(w)

####################################################################################################################################################

class PadTable(Gtk.Table):
    def __init__(self, controller, bank_model, rows, columns):
        Gtk.Table.__init__(self, rows, columns, True)
        
        self.keys = {}
        group = None
        for r in range(0, rows):
            for c in range(0, columns):
                key = 36 + (rows - r - 1) * columns + c
                b = PadButton(controller, bank_model, key)
                if group is not None:
                    b.set_group(group)
                self.attach(standard_align(b, 0.5, 0.5, 0, 0), c, c + 1, r, r + 1)
                self.keys[key] = b
    def refresh(self):
        for pad in self.keys.values():
            pad.update_label()

####################################################################################################################################################

class FileView(Gtk.TreeView):
    def __init__(self, dirs_model):
        self.is_playing = True
        self.dirs_model = dirs_model
        self.files_model = SampleFilesModel(dirs_model)
        Gtk.TreeView.__init__(self, self.files_model)
        self.insert_column_with_attributes(0, "Name", Gtk.CellRendererText(), text=1)
        self.set_cursor((0,))
        self.enable_model_drag_source(Gdk.ModifierType.BUTTON1_MASK, [], Gdk.DragAction.COPY)
        self.drag_source_add_text_targets()
        self.cursor_changed_handler = self.connect('cursor-changed', self.cursor_changed)
        self.connect('drag-data-get', self.drag_data_get)
        self.connect('row-activated', self.on_row_activated)

    def stop_playing(self):
        if self.is_playing:
            cbox.do_cmd("/scene/instr/_preview_sample/engine/unload", None, [])
            self.is_playing = False

    def start_playing(self, fn):
        self.is_playing = True
        cbox.do_cmd("/scene/instr/_preview_sample/engine/load", None, [fn, -1])
        cbox.do_cmd("/scene/instr/_preview_sample/engine/play", None, [])

    def cursor_changed(self, w):
        if self.files_model.is_refreshing:
            self.stop_playing()
            return

        c = self.get_cursor()
        if c[0] is not None:
            fn = self.files_model[c[0].get_indices()[0]][0]
            if fn.endswith("/"):
                return
            if fn != "":
                self.start_playing(fn)
            else:
                self.stop_playing(fn)
            
    def drag_data_get(self, treeview, context, selection, target_id, etime):
        cursor = treeview.get_cursor()
        if cursor is not None:
            c = cursor[0].get_indices()[0]
            fr = self.files_model[c]
            selection.set_text(str(fr[1]+"|"+fr[0]), -1)
            
    def on_row_activated(self, treeview, path, column):
        c = self.get_cursor()
        fn, label = self.files_model[c[0].get_indices()[0]]
        if fn.endswith("/"):
            self.files_model.refresh(fn)
        
####################################################################################################################################################

class EditorDialog(Gtk.Dialog):
    def __init__(self, parent):
        self.prepare_scene()
        Gtk.Dialog.__init__(self, "Drum kit editor", parent, Gtk.DialogFlags.DESTROY_WITH_PARENT, 
            ())

        self.menu_bar = Gtk.MenuBar()
        self.menu_bar.append(create_menu("_Kit", [
            ("_New", self.on_kit_new),
            ("_Open...", self.on_kit_open),
            ("_Save as...", self.on_kit_save_as),
            ("_Close", lambda w: self.response(Gtk.ResponseType.OK)),
        ]))
        self.menu_bar.append(create_menu("_Layer", [
            ("_Delete", self.on_layer_delete),
        ]))
        self.vbox.pack_start(self.menu_bar, False, False, 0)
        
        self.hbox = Gtk.HBox(spacing = 5)
        
        self.update_source = None
        self.current_pad = None
        self.dirs_model = SampleDirsModel()
        self.bank_model = BankModel()
        self.tree = FileView(self.dirs_model)
        self.layer_list = LayerListView(self)
        self.layer_editor = LayerEditor(self, self.bank_model)
        self.no_sfz_update = False
        
        combo = Gtk.ComboBox(model = self.dirs_model)
        cell = Gtk.CellRendererText()
        combo.pack_start(cell, True)
        combo.add_attribute(cell, 'text', 0)
        combo.connect('changed', lambda combo, tree_model, combo_model: tree_model.refresh(combo_model[combo.get_active()][1] if combo.get_active() >= 0 else None), self.tree.get_model(), combo.get_model())
        combo.set_active(0)
        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.ALWAYS)
        sw.add(self.tree)
        
        left_box = Gtk.VBox(spacing = 5)
        left_box.pack_start(combo, False, False, 0)
        left_box.pack_start(sw, True, True, 5)
        self.hbox.pack_start(left_box, True, True, 0)
        sw.set_size_request(200, -1)
        
        self.pads = PadTable(self, self.bank_model, 4, 4)
        self.hbox.pack_start(self.pads, True, True, 5)
        
        right_box = Gtk.VBox(spacing = 5)
        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.ALWAYS)
        sw.set_size_request(320, 100)
        sw.set_shadow_type(Gtk.ShadowType.ETCHED_IN)
        sw.add(self.layer_list)
        right_box.pack_start(sw, True, True, 0)
        sw = Gtk.ScrolledWindow()
        sw.set_size_request(320, 200)
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.ALWAYS)
        sw.add_with_viewport(self.layer_editor)
        right_box.pack_start(sw, True, True, 0)
        self.hbox.pack_start(right_box, True, True, 0)
        
        self.vbox.pack_start(self.hbox, False, False, 0)
        self.vbox.show_all()
        widget = self.pads.keys[36]
        widget.set_active(True)
        
        self.update_kit()
        
    def prepare_scene(self):
        scene = cbox.Document.get_scene()
        scene_status = scene.status()
        layers = [layer.status().instrument_name for layer in scene_status.layers]
        if '_preview_sample' not in layers:
            scene.add_new_instrument_layer("_preview_sample", "stream_player", pos = 0)
            ps = scene.status().instruments['_preview_sample'][1]
            ps.cmd('/output/1/gain', None, -12.0)
        if '_preview_kit' not in layers:
            scene.add_new_instrument_layer("_preview_kit", "sampler", pos = 1)

    def update_kit(self):
        cbox.do_cmd("/scene/instr/_preview_kit/engine/load_patch_from_string", None, [0, "", self.bank_model.to_sfz(), "Preview"])
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
        try:
            self.no_sfz_update = True
            if self.current_pad is not None:
                self.layer_list.set_model(self.bank_model[self.current_pad.key])
                self.layer_list.set_cursor(0)
            else:
                self.layer_list.set_model(None)
            self.layer_editor.refresh()
        finally:
            self.no_sfz_update = False
        
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
        self.layer_list.set_cursor(0)

    def get_current_pad_model(self):
        return self.current_pad.get_key_model()

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
        dlg = Gtk.FileChooserDialog('Open a pad bank', self, Gtk.FileChooserAction.OPEN,
            (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, Gtk.STOCK_OPEN, Gtk.ResponseType.APPLY))
        dlg.add_filter(standard_filter(["*.sfz", "*.SFZ"], "SFZ files"))
        dlg.add_filter(standard_filter(["*"], "All files"))
        try:
            if dlg.run() == Gtk.ResponseType.APPLY:
                sfz_data = open(dlg.get_filename(), "r").read()
                self.bank_model.from_sfz(sfz_data, dlg.get_current_folder())
            self.pads.refresh()
            self.refresh_layers()
            self.update_kit()
        finally:
            dlg.destroy()

    def on_kit_save_as(self, widget):
        dlg = Gtk.FileChooserDialog('Save a pad bank', self, Gtk.FileChooserAction.SAVE, 
            (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, Gtk.STOCK_SAVE, Gtk.ResponseType.APPLY))
        dlg.add_filter(standard_filter(["*.sfz", "*.SFZ"], "SFZ files"))
        dlg.add_filter(standard_filter(["*"], "All files"))
        try:
            if dlg.run() == Gtk.ResponseType.APPLY:
                open(dlg.get_filename(), "w").write(self.bank_model.to_sfz())
        finally:
            dlg.destroy()
