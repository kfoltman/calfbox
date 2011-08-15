#from gui_tools import *
import gnomecanvas
import gtk
import pygtk
import gobject
import gui_tools

PPQN = 48

def standard_filter(patterns, name):
    f = gtk.FileFilter()
    for p in patterns:
        f.add_pattern(p)
    f.set_name(name)
    return f

class NoteModel(object):
    def __init__(self, pos, channel, row, vel, len = 1):
        self.pos = int(pos)
        self.channel = int(channel)
        self.row = int(row)
        self.vel = int(vel)
        self.len = int(len)
        self.item = None
        self.selected = False
    def __str__(self):
        return "pos=%s row=%s vel=%s len=%s" % (self.pos, self.row, self.vel, self.len)

# This is stupid and needs rewriting using a faster data structure
class DrumPatternModel(gobject.GObject):
    def __init__(self, beats, bars):
        gobject.GObject.__init__(self)
        self.ppqn = PPQN
        self.beats = beats
        self.bars = bars
        self.notes = []
        
    def import_data(self, data):
        self.clear()
        active_notes = {}
        for t in data:
            cmd = t[1] & 0xF0
            if len(t) == 4 and (cmd == 0x90) and (t[3] > 0):
                note = NoteModel(t[0], (t[1] & 15) + 1, t[2], t[3])
                active_notes[t[2]] = note
                self.add_note(note)
            if len(t) == 4 and ((cmd == 0x90 and t[3] == 0) or cmd == 0x80):
                if t[2] in active_notes:
                    active_notes[t[2]].len = t[0] - active_notes[t[2]].pos
                    del active_notes[t[2]]
        end = self.get_length()
        for n in active_notes.values():
            n.len = end - n.pos

    def clear(self):
        self.notes = []
        self.changed()

    def add_note(self, note, send_changed = True):
        self.notes.append(note)
        if send_changed:
            self.changed()
        
    def remove_note(self, pos, row, channel):
        self.notes = [note for note in self.notes if note.pos != pos or note.row != row or (channel is not None and note.channel != channel)]
        self.changed()
        
    def set_note_vel(self, note, vel):
        note.vel = int(vel)
        self.changed()
            
    def has_note(self, pos, row, channel):
        for n in self.notes:
            if n.pos == pos and n.row == row and (channel is None or n.channel == channel):
                return True
        return False
            
    def get_note(self, pos, row, channel):
        for n in self.notes:
            if n.pos == pos and n.row == row and (channel is None or n.channel == channel):
                return n
        return None
            
    def items(self):
        return self.notes
        
    def get_length(self):
        return self.beats * self.bars * self.ppqn
        
    def changed(self):
        self.emit('changed')

    def delete_selected(self):
        self.notes = [note for note in self.notes if not note.selected]
        self.changed()
        
    def group_set(self, **vals):
        for n in self.notes:
            if not n.selected:
                continue
            for k, v in vals.iteritems():
                setattr(n, k, v)
        self.changed()

    def transpose_selected(self, amount):
        for n in self.notes:
            if not n.selected:
                continue
            if n.row + amount < 0 or n.row + amount > 127:
                continue
            n.row += amount
        self.changed()

gobject.type_register(DrumPatternModel)
gobject.signal_new("changed", DrumPatternModel, gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ())

channel_ls = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
for ch in range(1, 17):
    channel_ls.append((str(ch), ch))
snap_settings_ls = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
for row in [("1/4", PPQN), ("1/8", PPQN / 2), ("1/8T", PPQN/3), ("1/16", PPQN/4), ("1/16T", PPQN/6), ("1/32", PPQN/8), ("1/32T", PPQN/12), ("1/64", PPQN/4)]:
    snap_settings_ls.append(row)
length_settings_ls = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
for row in [("1/2", PPQN), ("1/4", PPQN), ("1/4T", PPQN * 2 / 3), ("1/8", PPQN / 2), ("1/8T", PPQN/3), ("1/16", PPQN/4), ("1/16T", PPQN/6), ("1/32", PPQN/8), ("1/32T", PPQN/12), ("1/64", PPQN/4), ("Hit", 1)]:
    length_settings_ls.append(row)

class DrumEditorToolbox(gtk.HBox):
    def __init__(self, canvas):
        gtk.HBox.__init__(self, spacing = 5)
        self.canvas = canvas
        self.vel_adj = gtk.Adjustment(100, 1, 127, 1, 10, 0)
        self.pack_start(gtk.Label("Channel:"), False, False)
        self.channel_setting = gui_tools.standard_combo(channel_ls, active_item_lookup = self.canvas.channel, lookup_column = 1)
        self.channel_setting.connect('changed', lambda w: self.canvas.set_channel(w.get_model()[w.get_active()][1]))
        self.pack_start(self.channel_setting, False, True)
        self.pack_start(gtk.Label("Snap:"), False, False)
        self.snap_setting = gui_tools.standard_combo(snap_settings_ls, active_item_lookup = self.canvas.grid_unit, lookup_column = 1)
        self.snap_setting.connect('changed', lambda w: self.canvas.set_grid_unit(w.get_model()[w.get_active()][1]))
        self.pack_start(self.snap_setting, False, True)
        self.pack_start(gtk.Label("Length:"), False, False)
        self.length_setting = gui_tools.standard_combo(length_settings_ls, active_item_lookup = self.canvas.note_length, lookup_column = 1)
        self.length_setting.connect('changed', lambda w: self.canvas.set_note_length(w.get_model()[w.get_active()][1]))
        self.pack_start(self.length_setting, False, True)
        self.pack_start(gtk.Label("Velocity:"), False, False)
        self.pack_start(gtk.SpinButton(self.vel_adj, 0, 0), False, False)
        button = gtk.Button("Load")
        button.connect('clicked', self.load_pattern)
        self.pack_start(button, True, True)
        button = gtk.Button("Save")
        button.connect('clicked', self.save_pattern)
        self.pack_start(button, True, True)
        button = gtk.Button("Double")
        button.connect('clicked', self.double_pattern)
        self.pack_start(button, True, True)
        self.pack_start(gtk.Label("--"), False, False)
    def load_pattern(self, w):
        dlg = gtk.FileChooserDialog('Open a drum pattern', self.get_toplevel(), gtk.FILE_CHOOSER_ACTION_OPEN,
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_OPEN, gtk.RESPONSE_APPLY))
        dlg.add_filter(standard_filter(["*.cbdp"], "Drum patterns"))
        dlg.add_filter(standard_filter(["*"], "All files"))
        try:
            if dlg.run() == gtk.RESPONSE_APPLY:
                pattern = self.canvas.pattern
                f = file(dlg.get_filename(), "r")
                pattern.clear()
                pattern.beats, pattern.bars = [int(v) for v in f.readline().strip().split(";")]
                for line in f.readlines():
                    line = line.strip()
                    if not line.startswith("n:"):
                        pos, row, vel = line.split(";")
                        row = int(row) + 36
                        channel = 10
                        len = 1
                    else:
                        pos, channel, row, vel, len = line[2:].split(";")
                    self.canvas.pattern.add_note(NoteModel(pos, channel, row, vel, len), send_changed = False)
                f.close()
                self.canvas.pattern.changed()
                self.canvas.update_grid()
                self.canvas.update_notes()
        finally:
            dlg.destroy()
    def save_pattern(self, w):
        dlg = gtk.FileChooserDialog('Save a drum pattern', self.get_toplevel(), gtk.FILE_CHOOSER_ACTION_SAVE, 
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_SAVE, gtk.RESPONSE_APPLY))
        dlg.add_filter(standard_filter(["*.cbdp"], "Drum patterns"))
        dlg.add_filter(standard_filter(["*"], "All files"))
        dlg.set_filename("pattern.cbdp")
        try:
            if dlg.run() == gtk.RESPONSE_APPLY:            
                pattern = self.canvas.pattern
                f = file(dlg.get_filename(), "w")
                f.write("%s;%s\n" % (pattern.beats, pattern.bars))
                for i in self.canvas.pattern.items():
                    f.write("n:%s;%s;%s;%s;%s\n" % (i.pos, i.channel, i.row, i.vel, i.len))
                f.close()
        finally:
            dlg.destroy()
    def double_pattern(self, w):
        len = self.canvas.pattern.get_length()
        self.canvas.pattern.bars *= 2
        new_notes = []
        for note in self.canvas.pattern.items():
            new_notes.append(NoteModel(note.pos + len, note.channel, note.row, note.vel, note.len))
        for note in new_notes:
            self.canvas.pattern.add_note(note, send_changed = False)
        self.canvas.pattern.changed()
        self.canvas.update_size()
        self.canvas.update_grid()
        self.canvas.update_notes()

class DrumCanvasCursor(object):
    def __init__(self, canvas):
        self.canvas = canvas
        self.frame = self.canvas.root().add(gnomecanvas.CanvasRect, x1 = -6, y1 = -6, x2 = 6, y2 = 6, outline_color = "gray")
        self.frame.hide()
        self.vel = self.canvas.root().add(gnomecanvas.CanvasText, x = 0, y = 0, fill_color = "blue")
        self.vel.hide()
        self.rubberband = False
        self.rubberband_origin = None
        self.rubberband_current = None

    def hide(self):
        self.frame.hide()
        self.vel.hide()
        
    def show(self):
        self.frame.show()
        self.vel.show()
        
    def set_note(self, note):
        if note is None:
            self.vel.set(text = "")
        else:
            self.vel.set(text = "[%s] %s" % (note.channel, note.vel))
            
    def move(self, pulse, row):
        x = self.canvas.pulse_to_screen_x(pulse)
        y = self.canvas.row_to_screen_y(row) + self.canvas.row_height / 2
        self.frame.set(x1 = x - 6, x2 = x + 6, y1 = y - 6, y2 = y + 6)
        cy = y - self.canvas.row_height * 1.5 if y >= self.canvas.rows * self.canvas.row_height / 2 else y + self.canvas.row_height * 1.5
        self.vel.set(x = x, y = cy)
        
    def start_rubberband(self, x, y):
        self.rubberband = True
        self.rubberband_origin = (x, y)
        self.rubberband_current = (x, y)
        self.update_rubberband_frame()
        self.frame.show()
        
    def update_rubberband(self, x, y):
        self.rubberband_current = (x, y)
        self.update_rubberband_frame()
        
    def end_rubberband(self, x, y):
        self.rubberband_current = (x, y)
        self.update_rubberband_frame()
        self.frame.hide()
        self.rubberband = False
        
    def cancel_rubberband(self):
        self.rubberband = False
        
    def update_rubberband_frame(self):
        self.frame.set(x1 = self.rubberband_origin[0], y1 = self.rubberband_origin[1], x2 = self.rubberband_current[0], y2 = self.rubberband_current[1])
        
    def get_rubberband_box(self):
        x1, y1 = self.rubberband_origin
        x2, y2 = self.rubberband_current
        return (min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2))

class DrumCanvas(gnomecanvas.Canvas):
    def __init__(self, rows, pattern):
        gnomecanvas.Canvas.__init__(self)
        self.rows = rows
        self.pattern = pattern
        self.row_height = 24
        self.grid_unit = PPQN / 4 # unit in pulses
        self.note_length = PPQN / 4 # unit in pulses
        self.zoom_in = 2
        self.instr_width = 120
        self.edited_note = None
        self.orig_velocity = None
        self.orig_y = None
        self.channel = 10
        
        self.update_size()
        
        self.grid = self.root().add(gnomecanvas.CanvasGroup, x = self.instr_width)
        self.update_grid()

        self.notes = self.root().add(gnomecanvas.CanvasGroup, x = self.instr_width)
        self.update_notes()

        self.names = self.root().add(gnomecanvas.CanvasGroup, x = 0)
        self.update_names()
        
        self.cursor = DrumCanvasCursor(self)
        self.cursor.show()
        
        self.connect('event', self.on_grid_event)

        self.toolbox = DrumEditorToolbox(self)
        
    def calc_size(self):
        return (self.instr_width + self.pattern.get_length() * self.zoom_in + 1, self.rows * self.row_height + 1)
        
    def set_grid_unit(self, grid_unit):
        self.grid_unit = grid_unit
        self.update_grid()
        
    def set_note_length(self, length):
        self.note_length = length
        
    def set_channel(self, channel):
        self.channel = channel
        self.update_notes()
        
    def update_size(self):
        sx, sy = self.calc_size()
        self.set_scroll_region(0, 0, sx, self.rows * self.row_height)
        self.set_size_request(sx, sy)
    
    def update_names(self):
        for i in self.names.item_list:
            i.destroy()
        for i in range(0, self.rows):
            self.names.add(gnomecanvas.CanvasText, text = gui_tools.note_to_name(i), x = self.instr_width - 10, y = (i + 0.5) * self.row_height, anchor = gtk.ANCHOR_E, size_points = 10, font = "Sans", size_set = True)
        
    def update_grid(self):
        for i in self.grid.item_list:
            i.destroy()
        bg = self.grid.add(gnomecanvas.CanvasRect, x1 = 0, y1 = 0, x2 = self.pattern.get_length() * self.zoom_in, y2 = self.rows * self.row_height, fill_color = "white")
        bar_fg = "blue"
        beat_fg = "darkgray"
        grid_fg = "lightgray"
        row_grid_fg = "lightgray"
        row_ext_fg = "black"
        for i in range(0, self.rows + 1):
            color = row_ext_fg if (i == 0 or i == self.rows) else row_grid_fg
            self.grid.add(gnomecanvas.CanvasLine, points = [0, i * self.row_height, self.pattern.get_length() * self.zoom_in, i * self.row_height], fill_color = color)
        for i in range(0, self.pattern.get_length() + 1, self.grid_unit):
            color = grid_fg
            if i % self.pattern.ppqn == 0:
                color = beat_fg
            if (i % (self.pattern.ppqn * self.pattern.beats)) == 0:
                color = bar_fg
            self.grid.add(gnomecanvas.CanvasLine, points = [i * self.zoom_in, 1, i * self.zoom_in, self.rows * self.row_height - 1], fill_color = color)
            
    def update_notes(self):
        for i in self.notes.item_list:
            i.destroy()
        for item in self.pattern.items():
            x = self.pulse_to_screen_x(item.pos) - self.instr_width
            y = self.row_to_screen_y(item.row + 0.5)
            if item.channel == self.channel:
                fill_color = 0xC0C0C0FF - int(item.vel * 1.5) * 0x00010100
                outline_color = 0x808080FF
                if item.selected:
                    outline_color = 0xFF8080FF
            else:
                fill_color = 0xE0E0E0FF
                outline_color = 0xE0E0E0FF
            if item.len > 1:
                x2 = self.pulse_to_screen_x(item.pos + item.len) - self.pulse_to_screen_x(item.pos)
                item.item = self.notes.add(gnomecanvas.CanvasPolygon, points = [-4, 0, 0, -5, x2 / 2, -5, x2, 0, x2 / 2, 5, 0, 5], fill_color_rgba = fill_color, outline_color_rgba = outline_color)
            else:
                item.item = self.notes.add(gnomecanvas.CanvasPolygon, points = [-4, 0, 0, -5, 5, 0, 0, 5], fill_color_rgba = fill_color, outline_color_rgba = outline_color)
            item.item.move(x, y)

    def set_selection_from_rubberband(self):
        sx, sy, ex, ey = self.cursor.get_rubberband_box()
        for item in self.pattern.items():
            x = self.pulse_to_screen_x(item.pos)
            y = self.row_to_screen_y(item.row + 0.5)
            item.selected = (x >= sx and x <= ex and y >= sy and y <= ey)
        self.update_notes()

    def on_grid_event(self, item, event):
        if event.type == gtk.gdk.KEY_PRESS:
            return self.on_key_press(item, event)
        if event.type in [gtk.gdk.BUTTON_PRESS, gtk.gdk._2BUTTON_PRESS, gtk.gdk.LEAVE_NOTIFY, gtk.gdk.MOTION_NOTIFY, gtk.gdk.BUTTON_RELEASE]:
            return self.on_mouse_event(item, event)

    def on_key_press(self, item, event):
        keyval, state = event.keyval, event.state
        kvname = gtk.gdk.keyval_name(keyval)
        if kvname == 'Delete':
            self.pattern.delete_selected()
            self.update_notes()
        elif kvname == 'c':
            self.pattern.group_set(channel = self.channel)
            self.update_notes()
        elif kvname == 'l':
            self.pattern.group_set(len = self.note_length)
            self.update_notes()
        elif kvname == 'v':
            self.pattern.group_set(vel = int(self.toolbox.vel_adj.get_value()))
            self.update_notes()
        elif kvname == 'plus':
            self.pattern.transpose_selected(1)
            self.update_notes()
        elif kvname == 'minus':
            self.pattern.transpose_selected(-1)
            self.update_notes()
        #else:
        #    print kvname

    def on_mouse_event(self, item, event):
        ex, ey = self.window_to_world(event.x, event.y)
        column = self.screen_x_to_column(ex)
        row = self.screen_y_to_row(ey)
        pulse = column * self.grid_unit
        epulse = (ex - self.instr_width) / self.zoom_in
        unit = self.grid_unit * self.zoom_in
        if self.cursor.rubberband:
            if event.type == gtk.gdk.MOTION_NOTIFY:
                self.cursor.update_rubberband(ex, ey)
                return
            if event.type == gtk.gdk.BUTTON_RELEASE and event.button == 1:
                self.cursor.end_rubberband(ex, ey)
                self.set_selection_from_rubberband()
                return
            return
        if event.type == gtk.gdk.BUTTON_PRESS:
            self.grab_focus()
            if ((event.state & gtk.gdk.MOD1_MASK) != 0) and event.button == 1:
                self.cursor.start_rubberband(ex, ey)
                return
            if pulse < 0 or pulse >= self.pattern.get_length():
                return
            note = self.pattern.get_note(pulse, row, self.channel)
            if note is not None:
                if event.button == 3:
                    vel = int(self.toolbox.vel_adj.get_value())
                    self.pattern.set_note_vel(note, vel)
                    self.cursor.set_note(note)
                    self.update_notes()
                    return
                self.toolbox.vel_adj.set_value(note.vel)
            else:
                note = NoteModel(pulse, self.channel, row, int(self.toolbox.vel_adj.get_value()), self.note_length)
                self.pattern.add_note(note)
            self.edited_note = note
            self.orig_velocity = note.vel
            self.orig_y = ey
            self.grab_add()
            self.cursor.set_note(note)
            self.update_notes()
            return
        if event.type == gtk.gdk._2BUTTON_PRESS:
            if pulse < 0 or pulse >= self.pattern.get_length():
                return
            if self.pattern.has_note(pulse, row, self.channel):
                self.pattern.remove_note(pulse, row, self.channel)
            self.cursor.set_note(None)
            self.update_notes()
            if self.edited_note is not None:
                self.grab_remove()
                self.edited_note = None
            return
        if event.type == gtk.gdk.LEAVE_NOTIFY and self.edited_note is None:
            self.cursor.hide()
            return
        if event.type == gtk.gdk.MOTION_NOTIFY and self.edited_note is None:
            if pulse < 0 or pulse >= self.pattern.get_length():
                self.cursor.hide()
                return
            
            if abs(pulse - epulse) > 5:
                self.cursor.hide()
                return
            note = self.pattern.get_note(column * self.grid_unit, row, self.channel)
            self.cursor.move(pulse, row)
            self.cursor.set_note(note)
            self.cursor.show()
            return
        if event.type == gtk.gdk.MOTION_NOTIFY and self.edited_note is not None:
            vel = int(self.orig_velocity - self.snap(ey - self.orig_y) / 2)
            if vel < 1: vel = 1
            if vel > 127: vel = 127
            self.pattern.set_note_vel(self.edited_note, vel)
            self.toolbox.vel_adj.set_value(vel)
            self.cursor.set_note(self.edited_note)
            self.update_notes()
            self.update_now()
        if event.type == gtk.gdk.BUTTON_RELEASE and self.edited_note is not None:
            self.edited_note = None
            self.grab_remove()
            
    def screen_x_to_column(self, x):
        unit = self.grid_unit * self.zoom_in
        return int((x - self.instr_width + unit / 2) / unit)
        
    def screen_y_to_row(self, y):
        return int((y - 1) / self.row_height)
        
    def pulse_to_screen_x(self, pulse):
        return pulse * self.zoom_in + self.instr_width
        
    def column_to_screen_x(self, column):
        unit = self.grid_unit * self.zoom_in
        return column * unit + self.instr_width
        
    def row_to_screen_y(self, row):
        return row * self.row_height + 1
        
    def snap(self, val):
        if val > -10 and val < 10:
            return 0
        if val >= 10:
            return val - 10
        if val <= -10:
            return val + 10
        assert False

class DrumSeqWindow(gtk.Window):
    def __init__(self, length, pat_data):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.vbox = gtk.VBox(spacing = 5)
        self.pattern = DrumPatternModel(4, length / (4 * PPQN))
        if pat_data is not None:
            self.pattern.import_data(pat_data)

        self.canvas = DrumCanvas(128, self.pattern)
        sw = gtk.ScrolledWindow()
        sw.set_size_request(640, 400)
        sw.add_with_viewport(self.canvas)
        self.vbox.pack_start(sw, True, True)
        self.vbox.pack_start(self.canvas.toolbox, False, False)
        self.add(self.vbox)

if __name__ == "__main__":
    w = DrumSeqWindow()
    w.set_title("Drum pattern editor")
    w.show_all()
    w.connect('destroy', lambda w: gtk.main_quit())

    gtk.main()
