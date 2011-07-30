#from gui_tools import *
import gnomecanvas
import gtk
import pygtk
import gobject

PPQN = 48

def standard_filter(patterns, name):
    f = gtk.FileFilter()
    for p in patterns:
        f.add_pattern(p)
    f.set_name(name)
    return f

class DrumNoteModel(object):
    def __init__(self, pos, row, vel):
        self.pos = int(pos)
        self.row = int(row)
        self.vel = int(vel)
        self.item = None
    def __str__(self):
        return "pos=%s row=%s vel=%s" % (self.pos, self.row, self.vel)

# This is stupid and needs rewriting using a faster data structure
class DrumPatternModel(gobject.GObject):
    def __init__(self, beats, bars):
        gobject.GObject.__init__(self)
        self.ppqn = PPQN
        self.beats = beats
        self.bars = bars
        self.notes = []

    def clear(self):
        self.notes = []
        self.changed()

    def add_note(self, note):
        self.notes.append(note)
        self.changed()
        
    def remove_note(self, pos, row):
        self.notes = [note for note in self.notes if note.pos != pos or note.row != row]
        self.changed()
        
    def set_note_vel(self, note, vel):
        note.vel = int(vel)
        self.changed()
            
    def has_note(self, pos, row):
        for n in self.notes:
            if n.pos == pos and n.row == row:
                return True
        return False
            
    def get_note(self, pos, row):
        for n in self.notes:
            if n.pos == pos and n.row == row:
                return n
        return None
            
    def items(self):
        return self.notes
        
    def get_length(self):
        return self.beats * self.bars * self.ppqn
        
    def changed(self):
        self.emit('changed')

gobject.type_register(DrumPatternModel)
gobject.signal_new("changed", DrumPatternModel, gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ())

class DrumEditorToolbox(gtk.HBox):
    def __init__(self, canvas):
        gtk.HBox.__init__(self, spacing = 5)
        self.canvas = canvas
        self.vel_adj = gtk.Adjustment(100, 1, 127, 1, 10, 0)
        for label, unit in [("1/4", PPQN), ("1/8", PPQN / 2), ("1/8T", PPQN/3), ("1/16", PPQN/4), ("1/16T", PPQN/6)]:
            button = gtk.Button(label)
            button.connect('clicked', lambda w, unit: self.canvas.set_grid_unit(unit), unit)
            self.pack_start(button, True, True)
        self.pack_start(gtk.Label("Velocity:"), False, False)
        self.pack_start(gtk.SpinButton(self.vel_adj, 0, 0), False, False)
        self.cur_vel_label = gtk.Label("")
        self.cur_vel_label.set_width_chars(3)
        self.pack_start(self.cur_vel_label, False, False)
        button = gtk.Button("Load")
        button.connect('clicked', self.load_pattern)
        self.pack_start(button, True, True)
        button = gtk.Button("Save")
        button.connect('clicked', self.save_pattern)
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
                    self.canvas.pattern.add_note(DrumNoteModel(*line.strip().split(";")))
                f.close()
                self.canvas.update_grid()
                self.canvas.update_notes()
        finally:
            dlg.destroy()
    def save_pattern(self, w):
        dlg = gtk.FileChooserDialog('Save a drum pattern', self.get_toplevel(), gtk.FILE_CHOOSER_ACTION_SAVE, 
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_SAVE, gtk.RESPONSE_APPLY))
        dlg.add_filter(standard_filter(["*.cbdp"], "Drum patterns"))
        dlg.add_filter(standard_filter(["*"], "All files"))
        try:
            if dlg.run() == gtk.RESPONSE_APPLY:            
                pattern = self.canvas.pattern
                f = file(dlg.get_filename(), "w")
                f.write("%s;%s\n" % (pattern.beats, pattern.bars))
                for i in self.canvas.pattern.items():
                    f.write("%s;%s;%s\n" % (i.pos, i.row, i.vel))
                f.close()
        finally:
            dlg.destroy()

class DrumCanvas(gnomecanvas.Canvas):
    def __init__(self, rows, pattern):
        gnomecanvas.Canvas.__init__(self)
        self.rows = rows
        self.pattern = pattern
        self.row_height = 24
        self.grid_unit = PPQN / 4 # unit in pulses
        self.zoom_in = 1
        self.instr_width = 120
        self.edited_note = None
        self.orig_velocity = None
        self.orig_y = None
        
        sx, sy = self.calc_size()
        self.set_scroll_region(0, 0, sx, sy)
        self.set_size_request(sx, sy)
        
        self.grid = self.root().add(gnomecanvas.CanvasGroup, x = self.instr_width)
        self.update_grid()

        self.notes = self.root().add(gnomecanvas.CanvasGroup, x = self.instr_width)
        self.update_notes()

        self.names = self.root().add(gnomecanvas.CanvasGroup, x = 0)
        self.update_names()
        
        self.connect('event', self.on_grid_event)
        self.cursor = self.root().add(gnomecanvas.CanvasRect, x1 = -5, y1 = -5, x2 = 5, y2 = 5, outline_color = "red")
        self.cursor.hide()

        self.toolbox = DrumEditorToolbox(self)
        
    def calc_size(self):
        return (self.instr_width + self.pattern.get_length() * self.zoom_in + 1, self.rows * self.row_height + 1)
        
    def set_grid_unit(self, grid_unit):
        self.grid_unit = grid_unit
        self.update_grid()        
        
    def update_names(self):
        for i in self.names.item_list:
            i.destroy()
        for i in range(0, self.rows):
            self.names.add(gnomecanvas.CanvasText, text = "Pad %s" % i, x = self.instr_width - 10, y = (i + 0.5) * self.row_height, anchor = gtk.ANCHOR_E, size_points = 10, font = "Sans", size_set = True)
        
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
            item.item = self.notes.add(gnomecanvas.CanvasRect, x1 = x - 5, y1 = y - 5, x2 = x + 5, y2 = y + 5, fill_color_rgba = (item.vel << 25))
        
    def on_grid_event(self, item, event):
        print event
        if event.type in [gtk.gdk.BUTTON_PRESS, gtk.gdk._2BUTTON_PRESS, gtk.gdk.LEAVE_NOTIFY, gtk.gdk.MOTION_NOTIFY, gtk.gdk.BUTTON_RELEASE]:
            ex, ey = self.window_to_world(event.x, event.y)
        unit = self.grid_unit * self.zoom_in
        if event.type == gtk.gdk.BUTTON_PRESS:
            column = self.screen_x_to_column(ex)
            row = self.screen_y_to_row(ey)
            if column < 0:
                return
            pulse = column * self.grid_unit
            if pulse >= self.pattern.get_length():
                return
            note = self.pattern.get_note(pulse, row)
            if note is not None:
                if event.button == 3:
                    self.pattern.set_note_vel(note, int(self.toolbox.vel_adj.get_value()))
                    self.update_vel_label(note)
                    self.update_notes()
                    return
                self.toolbox.vel_adj.set_value(note.vel)
            else:
                note = DrumNoteModel(pulse, row, int(self.toolbox.vel_adj.get_value()))
                self.pattern.add_note(note)
            self.edited_note = note
            self.orig_velocity = note.vel
            self.orig_y = ey
            self.grab_add()
            self.update_notes()
            self.update_vel_label(note)
            return
        if event.type == gtk.gdk._2BUTTON_PRESS:
            column = self.screen_x_to_column(ex)
            row = self.screen_y_to_row(ey)
            if column < 0:
                return
            pulse = column * self.grid_unit
            if pulse >= self.pattern.get_length():
                return
            if self.pattern.has_note(pulse, row):
                self.pattern.remove_note(pulse, row)
            self.update_notes()
            self.update_vel_label(None)
            return
        if event.type == gtk.gdk.LEAVE_NOTIFY:
            self.cursor.hide()
            self.update_vel_label(None)
            return
        if event.type == gtk.gdk.MOTION_NOTIFY and self.edited_note is None:
            if ex < self.instr_width - unit / 2:
                self.cursor.hide()
                return
            column = self.screen_x_to_column(ex)
            row = self.screen_y_to_row(ey)
            pulse = column * self.grid_unit
            if pulse >= self.pattern.get_length():
                self.cursor.hide()
                return
            
            x = self.column_to_screen_x(column)
            y = self.row_to_screen_y(row + 0.5)
            if abs(ex - x) > 5:
                self.cursor.hide()
                return
            self.cursor.set(x1 = x - 5, x2 = x + 5, y1 = y - 5, y2 = y + 5)
            self.cursor.show()
            note = self.pattern.get_note(column * self.grid_unit, row)
            self.update_vel_label(note)
            return
        if event.type == gtk.gdk.MOTION_NOTIFY and self.edited_note is not None:
            vel = int(self.orig_velocity - self.snap(ey - self.orig_y) / 2)
            if vel < 1: vel = 1
            if vel > 127: vel = 127
            self.pattern.set_note_vel(self.edited_note, vel)
            self.toolbox.vel_adj.set_value(vel)
            self.update_notes()
            self.update_vel_label(self.edited_note)
        if event.type == gtk.gdk.BUTTON_RELEASE and self.edited_note is not None:
            self.edited_note = None
            self.grab_remove()
            
    def screen_x_to_column(self, x):
        unit = self.grid_unit * self.zoom_in
        return int((x - self.instr_width + unit / 2) / unit)
        
    def screen_y_to_row(self, y):
        return int((y - 1) / self.row_height)
        
    def pulse_to_screen_x(self, pulse):
        return pulse + self.instr_width
        
    def column_to_screen_x(self, column):
        unit = self.grid_unit * self.zoom_in
        return column * unit + self.instr_width
        
    def row_to_screen_y(self, row):
        return row * self.row_height + 1
        
    def update_vel_label(self, note):
        if note is None:
            self.toolbox.cur_vel_label.set_text("")
        else:
            self.toolbox.cur_vel_label.set_text(str(note.vel))
            
    def snap(self, val):
        if val > -10 and val < 10:
            return 0
        if val >= 10:
            return val - 10
        if val <= -10:
            return val + 10
        assert False

class DrumSeqWindow(gtk.Window):
    def __init__(self):
        gtk.Window.__init__(self, gtk.WINDOW_TOPLEVEL)
        self.vbox = gtk.VBox(spacing = 5)
        self.pattern = DrumPatternModel(4, 1)
        ppqn = PPQN

        self.canvas = DrumCanvas(16, self.pattern)
        self.vbox.pack_start(self.canvas, True, True)
        self.vbox.pack_start(self.canvas.toolbox, False, False)
        self.add(self.vbox)

if __name__ == "__main__":
    w = DrumSeqWindow()
    w.set_title("Drum pattern editor")
    w.show_all()
    w.connect('destroy', lambda w: gtk.main_quit())

    gtk.main()
