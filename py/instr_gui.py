import cbox
from gui_tools import *

class StreamWindow(gtk.VBox):
    def __init__(self, instrument, path):
        gtk.Widget.__init__(self)
        self.path = path
        
        panel = gtk.VBox(spacing=5)
        
        self.filebutton = gtk.FileChooserButton("Streamed file")
        self.filebutton.set_action(gtk.FILE_CHOOSER_ACTION_OPEN)
        self.filebutton.set_local_only(True)
        self.filebutton.set_filename(cbox.GetThings("%s/status" % self.path, ['filename'], []).filename)
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV", "*.ogg", "*.OGG", "*.flac", "*.FLAC"], "All loadable audio files"))
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV"], "RIFF WAVE files"))
        self.filebutton.add_filter(standard_filter(["*.ogg", "*.OGG"], "OGG container files"))
        self.filebutton.add_filter(standard_filter(["*.flac", "*.FLAC"], "FLAC files"))
        self.filebutton.add_filter(standard_filter(["*"], "All files"))
        self.filebutton.connect('file-set', self.file_set)
        panel.pack_start(self.filebutton, False, False)

        self.adjustment = gtk.Adjustment()
        self.adjustment_handler = self.adjustment.connect('value-changed', self.pos_slider_moved)
        self.progress = standard_hslider(self.adjustment)
        panel.pack_start(self.progress, False, False)

        self.play_button = gtk.Button(label = "_Play")
        self.rewind_button = gtk.Button(label = "_Rewind")
        self.stop_button = gtk.Button(label = "_Stop")
        buttons = gtk.HBox(spacing = 5)
        buttons.add(self.play_button)
        buttons.add(self.rewind_button)
        buttons.add(self.stop_button)
        panel.pack_start(buttons, False, False)
        
        self.add(panel)
        self.play_button.connect('clicked', lambda x: cbox.do_cmd("%s/play" % self.path, None, []))
        self.rewind_button.connect('clicked', lambda x: cbox.do_cmd("%s/seek" % self.path, None, [0]))
        self.stop_button.connect('clicked', lambda x: cbox.do_cmd("%s/stop" % self.path, None, []))
        set_timer(self, 30, self.update)

    def update(self):
        attribs = cbox.GetThings("%s/status" % self.path, ['filename', 'pos', 'length', 'playing'], [])
        self.progress.set_sensitive(attribs.length is not None)
        self.adjustment.handler_block(self.adjustment_handler)
        if attribs.length is not None:
            try:
                self.adjustment.set_all(attribs.pos, 0, attribs.length, 44100, 44100 * 10, 0)
            finally:
                self.adjustment.handler_unblock(self.adjustment_handler)
        return True
        
    def pos_slider_moved(self, adjustment):
        cbox.do_cmd("%s/seek" % self.path, None, [int(adjustment.get_value())])
        
    def file_set(self, button):
        cbox.do_cmd("%s/load" % self.path, None, [button.get_filename(), -1])
        

class WithPatchTable:
    def __init__(self, attribs):
        self.patches = gtk.ListStore(gobject.TYPE_STRING, gobject.TYPE_INT)
        self.patch_combos = []
        
        self.table = gtk.Table(2, 16)
        self.table.set_col_spacings(5)

        for i in range(16):
            self.table.attach(bold_label("Channel %s" % (1 + i)), 0, 1, i, i + 1, gtk.SHRINK, gtk.SHRINK)
            cb = standard_combo(self.patches, None)
            cb.connect('changed', self.patch_combo_changed, i + 1)
            self.table.attach(cb, 1, 2, i, i + 1, gtk.SHRINK, gtk.SHRINK)
            self.patch_combos.append(cb)

        self.update_model()
        set_timer(self, 500, self.patch_combo_update)

    def update_model(self):
        self.patches.clear()
        patches = cbox.GetThings("%s/patches" % self.path, ["%patch"], []).patch
        ch_patches = cbox.GetThings("%s/status" % self.path, ["%patch"], []).patch
        self.mapping = {}
        for id in patches:
            self.mapping[id] = len(self.mapping)
            self.patches.append(("%s (%s)" % (patches[id], id), id))
        self.patch_combo_update()

    def patch_combo_changed(self, combo, channel):
        if combo.get_active() == -1:
            return
        cbox.do_cmd(self.path + "/set_patch", None, [int(channel), int(self.patches[combo.get_active()][1])])

    def patch_combo_update(self):
        patch = cbox.GetThings("%s/status" % self.path, ['%patch'], []).patch
        for i in range(16):
            cb = self.patch_combos[i]
            old_patch_index = cb.get_active() if cb.get_active() >= 0 else -1
            current_patch_index = self.mapping[patch[i + 1][0]] if patch[i + 1][0] >= 0 else -1
            if old_patch_index != current_patch_index:
                cb.set_active(current_patch_index)
        #self.status_label.set_markup(s)
        return True
        
class FluidsynthWindow(gtk.VBox, WithPatchTable):
    def __init__(self, instrument, path):
        gtk.VBox.__init__(self)
        self.path = path
        
        attribs = cbox.GetThings("%s/status" % self.path, ['%patch', 'polyphony'], [])

        panel = gtk.VBox(spacing=5)
        table = gtk.Table(2, 1)
        IntSliderRow("Polyphony", "polyphony", 2, 256).add_row(table, 0, cbox.VarPath(self.path), attribs)
        panel.pack_start(table, False, False)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True)
        self.add(panel)

class LoadProgramDialog(SelectObjectDialog):
    title = "Load a sampler program"
    def __init__(self, parent):
        SelectObjectDialog.__init__(self, parent)
    def update_model(self, model):
        for s in cbox.Config.sections("spgm:"):
            title = s["title"]
            if s["sfz"] == None:
                model.append((s.name[5:], "Program", s.name, title))
            else:
                model.append((s.name[5:], "SFZ", s.name, title))    

class SamplerWindow(gtk.VBox, WithPatchTable):
    def __init__(self, instrument, path):
        gtk.VBox.__init__(self)
        self.path = path
        
        attribs = cbox.GetThings("%s/status" % self.path, ['%patch', 'polyphony', 'active_voices'], [])

        panel = gtk.VBox(spacing=5)
        table = gtk.Table(2, 2)
        table.set_col_spacings(5)
        IntSliderRow("Polyphony", "polyphony", 1, 128).add_row(table, 0, cbox.VarPath(self.path), attribs)
        self.voices_widget = add_display_row(table, 1, "Voices in use", cbox.VarPath(self.path), attribs, "active_voices")
        panel.pack_start(table, False, False)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True)
        self.add(panel)
        load_button = gtk.Button("_Load")
        load_button.connect('clicked', self.load)
        panel.pack_start(load_button, False, True)
        set_timer(self, 200, self.voices_update)
        
    def load(self, event):
        d = LoadProgramDialog(self.get_toplevel())
        response = d.run()
        try:
            if response == gtk.RESPONSE_OK:
                scene = d.get_selected_object()
                pgm_id = cbox.GetThings("%s/get_unused_program" % self.path, ['program_no'], []).program_no
                cbox.do_cmd("%s/load_patch" % self.path, None, [pgm_id, scene[2], scene[2][5:]])
                self.update_model()
        finally:
            d.destroy()        
        
    def voices_update(self):
        attribs = cbox.GetThings("%s/status" % self.path, ['active_voices'], [])
        self.voices_widget.set_text(str(attribs.active_voices))
        return True

instrument_window_map = {
    'stream_player' : StreamWindow,
    'fluidsynth' : FluidsynthWindow,
    'sampler' : SamplerWindow
}

