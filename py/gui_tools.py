import cbox
import math
import glib
import gobject
import pygtk
import gtk

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
ms_format = "%0.1f ms"
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

def checkbox_changed_bool(checkbox, path, *items):
    cbox.do_cmd(path, None, list(items) + [1 if checkbox.get_active() else 0])

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

def tree_toggle_changed_bool(renderer, tree_path, model, opath, column):
    model[int(tree_path)][column] = not model[int(tree_path)][column]
    cbox.do_cmd(opath % (1 + int(tree_path)), None, [1 if model[int(tree_path)][column] else 0])
        
def tree_combo_changed(renderer, tree_path, new_value, model, opath, column):
    new_value = renderer.get_property('model')[new_value][0]
    model[int(tree_path)][column] = new_value
    cbox.do_cmd(opath % (1 + int(tree_path)), None, [new_value])
        
def standard_toggle_renderer(list_model, path, column):
    toggle = gtk.CellRendererToggle()
    toggle.connect('toggled', tree_toggle_changed_bool, list_model, path, column)
    return toggle

def standard_combo_renderer(list_model, model, path, column):
    combo = gtk.CellRendererCombo()
    combo.set_property('model', model)
    combo.set_property('editable', True)
    combo.set_property('has-entry', False)
    combo.set_property('text-column', 1)
    combo.set_property('mode', gtk.CELL_RENDERER_MODE_EDITABLE)
    combo.connect('changed', tree_combo_changed, list_model, path, column)
    return combo

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
    
def set_timer(widget, time, func, *args):
    refresh_id = glib.timeout_add(time, func, *args)
    widget.connect('destroy', lambda obj, id: glib.source_remove(id), refresh_id)

#################################################################################################################################

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

#################################################################################################################################

class SelectObjectDialog(gtk.Dialog):
    def __init__(self, parent):
        gtk.Dialog.__init__(self, self.title, parent, gtk.DIALOG_MODAL, 
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_OK, gtk.RESPONSE_OK))
        self.set_default_response(gtk.RESPONSE_OK)
        model = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_STRING, gobject.TYPE_STRING, gobject.TYPE_STRING)
        
        self.update_model(model)
                
        scenes = gtk.TreeView(model)
        scenes.insert_column_with_attributes(0, "Name", gtk.CellRendererText(), text=0)
        scenes.insert_column_with_attributes(1, "Title", gtk.CellRendererText(), text=3)
        scenes.insert_column_with_attributes(2, "Type", gtk.CellRendererText(), text=1)
        self.vbox.pack_start(scenes)
        scenes.show()
        scenes.grab_focus()
        self.scenes = scenes
        self.scenes.connect('row-activated', lambda w, path, column: self.response(gtk.RESPONSE_OK))
    def get_selected_object(self):
        return self.scenes.get_model()[self.scenes.get_cursor()[0][0]]

