import cbox
import math
import glib
from gi.repository import GObject, Gdk, Gtk

# Compatibility stuff
Gtk.TreeView.insert_column_with_attributes = lambda self, col, title, renderer, xarg = None, **kwargs: self.insert_column(Gtk.TreeViewColumn(title = title, cell_renderer = renderer, **kwargs), col)

def callback(cmd, cb, args):
    print cmd, cb, args

def bold_label(text, halign = 1):
    l = Gtk.Label()
    l.set_markup("<b>%s</b>" % text)
    l.set_alignment(halign, 0.5)
    return l

def left_label(text):
    l = Gtk.Label(text)
    l.set_alignment(0, 0.5)
    return l

def standard_hslider(adj):
    sc = Gtk.HScale(adjustment = adj)
    sc.set_size_request(160, -1)
    sc.set_value_pos(Gtk.PositionType.RIGHT)
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
ms_format = "%0.1f ms"
lfo_freq_mapper = LogMapper(0.01, 20, "%0.2f")
env_mapper = LogMapper(0.002, 20, "%f")
filter_freq_mapper = LogMapper(20, 20000, "%0.1f Hz")

def standard_mapped_hslider(adj, mapper):
    sc = Gtk.HScale(adjustment = adj)
    sc.set_size_request(160, -1)
    sc.set_value_pos(Gtk.PositionType.RIGHT)
    sc.connect('format-value', lambda scale, value, mapper: mapper.format_value(value), mapper)
    return sc

def standard_align(w, xo, yo, xs, ys):
    a = Gtk.Alignment(xalign = xo, yalign = yo, xscale = xs, yscale = ys)
    a.add(w)
    return a
    
def standard_vscroll_window(width, height, content = None):
    scroller = Gtk.ScrolledWindow()
    scroller.set_size_request(width, height)
    scroller.set_shadow_type(Gtk.ShadowType.NONE);
    scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC);
    if content is not None:
        scroller.add_with_viewport(content)
    return scroller

def standard_combo(model, active_item = None, column = 0, active_item_lookup = None, lookup_column = None, width = None):
    cb = Gtk.ComboBox(model = model)
    if active_item_lookup is not None:
        if lookup_column is None:
            lookup_column = column
        active_item = ls_index(model, active_item_lookup, lookup_column)
    if active_item is not None:
        cb.set_active(active_item)
    cell = Gtk.CellRendererText()
    if width is not None:
        cb.set_size_request(width, -1)
    cb.pack_start(cell, True)
    cb.add_attribute(cell, 'text', column)
    return cb
    
def ls_index(list_store, value, column):
    for i in range(len(list_store)):
        if list_store[i][column] == value:
            return i
    return None

def standard_filter(patterns, name):
    f = Gtk.FileFilter()
    for p in patterns:
        f.add_pattern(p)
    f.set_name(name)
    return f

def checkbox_changed_bool(checkbox, vpath):
    vpath.set(1 if checkbox.get_active() else 0)

def adjustment_changed_int(adjustment, vpath):
    vpath.set(int(adjustment.get_value()))

def adjustment_changed_float(adjustment, vpath):
    vpath.set(float(adjustment.get_value()))

def adjustment_changed_float_mapped(adjustment, vpath, mapper):
    vpath.set(mapper.map(adjustment.get_value()))

def combo_value_changed(combo, vpath, value_offset = 0):
    if combo.get_active() != -1:
        vpath.set(value_offset + combo.get_active())

def combo_value_changed_use_column(combo, vpath, column):
    if combo.get_active() != -1:
        vpath.set(combo.get_model()[combo.get_active()][column])

def tree_toggle_changed_bool(renderer, tree_path, model, opath, column):
    model[int(tree_path)][column] = not model[int(tree_path)][column]
    cbox.do_cmd(model.make_row_item(opath, tree_path), None, [1 if model[int(tree_path)][column] else 0])
        
def tree_combo_changed(renderer, tree_path, new_value, model, opath, column):
    new_value = renderer.get_property('model')[new_value][0]
    model[int(tree_path)][column] = new_value
    cbox.do_cmd(model.make_row_item(opath, tree_path), None, [new_value])
        
def standard_toggle_renderer(list_model, path, column):
    toggle = Gtk.CellRendererToggle()
    toggle.connect('toggled', tree_toggle_changed_bool, list_model, path, column)
    return toggle

def standard_combo_renderer(list_model, model, path, column):
    combo = Gtk.CellRendererCombo()
    combo.set_property('model', model)
    combo.set_property('editable', True)
    combo.set_property('has-entry', False)
    combo.set_property('text-column', 1)
    combo.set_property('mode', Gtk.CellRendererMode.EDITABLE)
    combo.connect('changed', tree_combo_changed, list_model, path, column)
    return combo

def add_display_row(t, row, label, path, values, item):
    t.attach(bold_label(label), 0, 1, row, row+1, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
    w = left_label(getattr(values, item))
    t.attach(w, 1, 2, row, row+1, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
    return w
    
def set_timer(widget, time, func, *args):
    refresh_id = glib.timeout_add(time, func, *args)
    widget.connect('destroy', lambda obj, id: glib.source_remove(id), refresh_id)

def create_menu(title, items):
    menuitem = Gtk.MenuItem.new_with_mnemonic(title)
    if items is not None:
        menu = Gtk.Menu()
        menuitem.set_submenu(menu)
        for label, meth in items:
            mit = Gtk.MenuItem.new_with_mnemonic(label)
            if meth is None:
                mit.set_sensitive(False)
            else:
                mit.connect('activate', meth)
            menu.append(mit)
    return menuitem

def note_to_name(note):
    if note < 0:
        return "N/A"
    n = note % 12
    return ("C C#D D#E F F#G G#A A#B "[n * 2 : n * 2 + 2]) + str((note / 12) - 2)

#################################################################################################################################

class TableRowWidget:
    def __init__(self, label, name, **kwargs):
        self.label = label
        self.name = name
        self.kwargs = kwargs
    def get_with_default(self, name, def_value):
        return self.kwargs[name] if name in self.kwargs else def_value
    def create_label(self):
        return bold_label(self.label)
    def has_attr(self, values):
        if type(values) is dict:
            return self.name in values
        return hasattr(values, self.name)
    def get_attr(self, values):
        if type(values) is dict:
            return values[self.name]
        return getattr(values, self.name)
    def get_value(self, values, vpath):
        if len(vpath.args) == 1:
            return self.get_attr(values)[vpath.args[0]]
        return self.get_attr(values)
    def add_row(self, table, row, vpath, values):
        table.attach(self.create_label(), 0, 1, row, row + 1, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        widget, refresher = self.create_widget(vpath)
        table.attach(widget, 1, 2, row, row + 1, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        if values is not None:
            refresher(values)
        return refresher
    def update_sensitive(self, widget, values):
        sensitive = (values is not None) and (self.get_with_default('setter', 0) is not None) and self.has_attr(values)
        widget.set_sensitive(sensitive)
        return sensitive

class SliderRow(TableRowWidget):
    def __init__(self, label, name, minv, maxv, **kwargs):
        TableRowWidget.__init__(self, label, name, **kwargs)
        self.minv = minv
        self.maxv = maxv
    def create_widget(self, vpath):
        setter = self.get_with_default('setter', adjustment_changed_float)
        step = self.kwargs['step'] if 'step' in self.kwargs else 1
        adj = Gtk.Adjustment(self.minv, self.minv, self.maxv, step, 6, 0)
        slider = standard_hslider(adj)
        if 'digits' in self.kwargs:
            slider.set_digits(self.kwargs['digits'])
        if setter is not None:
            adj.connect("value_changed", setter, vpath.plus(self.name))
        else:
            slider.set_sensitive(False)
        def refresher(values):
            if self.update_sensitive(slider, values):
                adj.set_value(self.get_value(values, vpath))
        return (slider, refresher)

class IntSliderRow(SliderRow):
    def __init__(self, label, name, minv, maxv, **kwargs):
        SliderRow.__init__(self, label, name, minv, maxv, setter = adjustment_changed_int, **kwargs)
    def create_widget(self, vpath):
        (slider, refresher) = SliderRow.create_widget(self, vpath)
        slider.connect('change-value', self.on_change_value)
        slider.set_digits(0)
        return slider, refresher
    def on_change_value(self, range, scroll, value):
        range.set_value(int(value))
        return True
        

class MappedSliderRow(TableRowWidget):
    def __init__(self, label, name, mapper, **kwargs):
        TableRowWidget.__init__(self, label, name, **kwargs)
        self.mapper = mapper
    def create_widget(self, vpath):
        setter = self.get_with_default('setter', adjustment_changed_float_mapped)
        adj = Gtk.Adjustment(0, 0, 100, 1, 6, 0)
        slider = standard_mapped_hslider(adj, self.mapper)
        if setter is not None:
            adj.connect("value_changed", setter, vpath.plus(self.name), self.mapper)
        else:
            slider.set_sensitive(False)
        def refresher(values):
            if self.update_sensitive(slider, values):
                adj.set_value(self.mapper.unmap(self.get_value(values, vpath)))
        return (slider, refresher)

class CheckBoxRow(TableRowWidget):
    def create_widget(self, vpath):
        widget = Gtk.CheckButton(self.label)
        widget.connect("clicked", checkbox_changed_bool, vpath.plus(self.name))
        def refresher(values):
            if self.update_sensitive(widget, values):
                widget.set_active(self.get_value(values, vpath) > 0)
        return (widget, refresher)

#################################################################################################################################

class SelectObjectDialog(Gtk.Dialog):
    def __init__(self, parent):
        Gtk.Dialog.__init__(self, self.title, parent, Gtk.DialogFlags.MODAL, 
            (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, Gtk.STOCK_OK, Gtk.ResponseType.OK))
        self.set_default_response(Gtk.ResponseType.OK)
        model = Gtk.ListStore(GObject.TYPE_STRING, GObject.TYPE_STRING, GObject.TYPE_STRING, GObject.TYPE_STRING)
        
        self.update_model(model)
                
        scroll = Gtk.ScrolledWindow()
        scenes = Gtk.TreeView(model)
        scenes.insert_column_with_attributes(0, "Name", Gtk.CellRendererText(), text=0)
        scenes.insert_column_with_attributes(1, "Title", Gtk.CellRendererText(), text=3)
        scenes.insert_column_with_attributes(2, "Type", Gtk.CellRendererText(), text=1)
        scenes.get_column(0).set_property('min_width', 150)
        scenes.get_column(1).set_property('min_width', 300)
        scenes.get_column(2).set_property('min_width', 150)
        scroll.add(scenes)
        self.vbox.pack_start(scroll, False, False, 0)
        scenes.show()
        scroll.set_size_request(640, 500)
        scroll.show()
        self.scenes = scenes
        self.scenes.connect('cursor-changed', lambda w: self.update_default_button())
        self.scenes.connect('row-activated', lambda w, path, column: self.response(Gtk.ResponseType.OK))
        self.scenes.set_cursor((0,), scenes.get_column(0))
        self.scenes.grab_focus()
        self.update_default_button()

    def get_selected_object(self):
        return self.scenes.get_model()[self.scenes.get_cursor()[0].get_indices()[0]]
    def update_default_button(self):
        self.set_response_sensitive(Gtk.ResponseType.OK, self.scenes.get_cursor()[1] is not None)

#################################################################################################################################

class SaveConfigObjectDialog(Gtk.Dialog):
    def __init__(self, parent, title):
        Gtk.Dialog.__init__(self, title, parent, Gtk.DialogFlags.MODAL, 
            (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, Gtk.STOCK_OK, Gtk.ResponseType.OK))
        self.set_default_response(Gtk.ResponseType.OK)
        
        l = Gtk.Label()
        l.set_text_with_mnemonic("_Name")
        e = Gtk.Entry()
        self.entry = e
        e.set_activates_default(True)
        e.connect('changed', self.on_entry_changed)
        row = Gtk.HBox()
        row.pack_start(l, False, False)
        row.pack_start(e, True, True)
        row.show_all()
        self.vbox.pack_start(row)
        e.grab_focus()
        self.on_entry_changed(e)
    def get_name(self):
        return self.entry.get_text()
    def on_entry_changed(self, w):
        self.set_response_sensitive(Gtk.ResponseType.OK, w.get_text() != '')
