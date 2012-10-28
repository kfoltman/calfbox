import cbox
from gui_tools import *

class StreamWindow(Gtk.VBox):
    def __init__(self, instrument, path):
        Gtk.Widget.__init__(self)
        self.path = path
        
        panel = Gtk.VBox(spacing=5)
        
        self.filebutton = Gtk.FileChooserButton("Streamed file")
        self.filebutton.set_action(Gtk.FileChooserAction.OPEN)
        self.filebutton.set_local_only(True)
        self.filebutton.set_filename(cbox.GetThings("%s/status" % self.path, ['filename'], []).filename)
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV", "*.ogg", "*.OGG", "*.flac", "*.FLAC"], "All loadable audio files"))
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV"], "RIFF WAVE files"))
        self.filebutton.add_filter(standard_filter(["*.ogg", "*.OGG"], "OGG container files"))
        self.filebutton.add_filter(standard_filter(["*.flac", "*.FLAC"], "FLAC files"))
        self.filebutton.add_filter(standard_filter(["*"], "All files"))
        self.filebutton.connect('file-set', self.file_set)
        panel.pack_start(self.filebutton, False, False, 5)

        self.adjustment = Gtk.Adjustment()
        self.adjustment_handler = self.adjustment.connect('value-changed', self.pos_slider_moved)
        self.progress = standard_hslider(self.adjustment)
        panel.pack_start(self.progress, False, False, 5)

        self.play_button = Gtk.Button.new_with_mnemonic("_Play")
        self.rewind_button = Gtk.Button.new_with_mnemonic("_Rewind")
        self.stop_button = Gtk.Button.new_with_mnemonic("_Stop")
        buttons = Gtk.HBox(spacing = 5)
        buttons.add(self.play_button)
        buttons.add(self.rewind_button)
        buttons.add(self.stop_button)
        panel.pack_start(buttons, False, False, 5)
        
        self.add(panel)
        self.play_button.connect('clicked', lambda x: cbox.do_cmd("%s/play" % self.path, None, []))
        self.rewind_button.connect('clicked', lambda x: cbox.do_cmd("%s/seek" % self.path, None, [0]))
        self.stop_button.connect('clicked', lambda x: cbox.do_cmd("%s/stop" % self.path, None, []))
        set_timer(self, 30, self.update)

    def update(self):
        attribs = cbox.GetThings("%s/status" % self.path, ['filename', 'pos', 'length', 'playing'], [])
        self.progress.set_sensitive(attribs.length is not None)
        if attribs.length is not None:
            try:
                self.adjustment.handler_block(self.adjustment_handler)
                self.adjustment.set_properties(value = attribs.pos, lower = 0, upper = attribs.length)
                #self.adjustment.set_all(attribs.pos, 0, attribs.length, 44100, 44100 * 10, 0)
            finally:
                self.adjustment.handler_unblock(self.adjustment_handler)
        return True
        
    def pos_slider_moved(self, adjustment):
        cbox.do_cmd("%s/seek" % self.path, None, [int(adjustment.get_value())])
        
    def file_set(self, button):
        cbox.do_cmd("%s/load" % self.path, None, [button.get_filename(), -1])
        

class WithPatchTable:
    def __init__(self, attribs):
        self.patches = Gtk.ListStore(GObject.TYPE_STRING, GObject.TYPE_INT)
        self.patch_combos = []
        
        self.table = Gtk.Table(2, 16)
        self.table.set_col_spacings(5)

        for i in range(16):
            self.table.attach(bold_label("Channel %s" % (1 + i)), 0, 1, i, i + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
            cb = standard_combo(self.patches, None)
            cb.connect('changed', self.patch_combo_changed, i + 1)
            self.table.attach(cb, 1, 2, i, i + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
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
        
class FluidsynthWindow(Gtk.VBox, WithPatchTable):
    def __init__(self, instrument, path):
        Gtk.VBox.__init__(self)
        self.path = path
        
        attribs = cbox.GetThings("%s/status" % self.path, ['%patch', 'polyphony'], [])

        panel = Gtk.VBox(spacing=5)
        table = Gtk.Table(2, 1)
        IntSliderRow("Polyphony", "polyphony", 2, 256).add_row(table, 0, cbox.VarPath(self.path), attribs)
        panel.pack_start(table, False, False, 5)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True, 5)
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

class SamplerWindow(Gtk.VBox, WithPatchTable):
    def __init__(self, instrument, path):
        Gtk.VBox.__init__(self)
        self.path = path
        
        attribs = cbox.GetThings("%s/status" % self.path, ['%patch', 'polyphony', 'active_voices'], [])

        panel = Gtk.VBox(spacing=5)
        table = Gtk.Table(2, 2)
        table.set_col_spacings(5)
        IntSliderRow("Polyphony", "polyphony", 1, 128).add_row(table, 0, cbox.VarPath(self.path), attribs)
        self.voices_widget = add_display_row(table, 1, "Voices in use", cbox.VarPath(self.path), attribs, "active_voices")
        panel.pack_start(table, False, False, 5)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True, 5)
        self.add(panel)
        load_button = Gtk.Button("_Load")
        load_button.connect('clicked', self.load)
        panel.pack_start(load_button, False, True, 5)
        set_timer(self, 200, self.voices_update)
        
    def load(self, event):
        d = LoadProgramDialog(self.get_toplevel())
        response = d.run()
        try:
            if response == Gtk.RESPONSE_OK:
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

class TonewheelOrganWindow(Gtk.VBox):
    combos = [
        (1, 'Upper', 'upper_vibrato', [(0, 'Off'), (1, 'On')]),
        (1, 'Lower', 'lower_vibrato', [(0, 'Off'), (1, 'On')]),
        (1, 'Mode', 'vibrato_mode', [(0, '1'), (1, '2'), (2, '3')]),
        (1, 'Chorus', 'vibrato_chorus', [(0, 'Off'), (1, 'On')]),
        (2, 'Enable', 'percussion_enable', [(0, 'Off'), (1, 'On')]),
        (2, 'Harmonic', 'percussion_3rd', [(0, '2nd'), (1, '3rd')]),
    ]
    def __init__(self, instrument, path):
        Gtk.VBox.__init__(self)
        self.path = path
        panel = Gtk.VBox(spacing=10)
        table = Gtk.Table(4, 10)
        table.props.row_spacing = 10
        table.set_col_spacings(5)
        self.drawbars = {}
        self.hboxes = {}
        self.hboxes[1] = Gtk.HBox(spacing = 10)
        self.hboxes[1].pack_start(Gtk.Label('Vibrato: '), False, False, 5)
        self.hboxes[2] = Gtk.HBox(spacing = 10)
        self.hboxes[2].pack_start(Gtk.Label('Percussion: '), False, False, 5)
        self.combos = {}
        for row, name, flag, options in TonewheelOrganWindow.combos:
            label = Gtk.Label(name)
            self.hboxes[row].pack_start(label, False, False, 5)
            model = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
            for oval, oname in options:
                model.append((oval, oname))
            combo = standard_combo(model, column = 1)
            self.hboxes[row].pack_start(combo, False, False, 5)
            combo.update_handler = combo.connect('changed', lambda w, path: cbox.do_cmd(path, None, [w.get_model()[w.get_active()][0]]), path + '/' + flag)
            self.combos[flag] = combo
        panel.pack_start(self.hboxes[1], False, False, 5)
        panel.pack_start(self.hboxes[2], False, False, 5)
        table.attach(Gtk.Label("Upper"), 0, 1, 0, 1)
        table.attach(Gtk.Label("Lower"), 0, 1, 1, 2)
        for i in range(9):
            slider = Gtk.VScale(Gtk.Adjustment(0, 0, 8, 1, 1))
            slider.props.digits = 0
            table.attach(slider, i + 1, i + 2, 0, 1)
            self.drawbars['u%d' % i] = slider.get_adjustment()
            slider.get_adjustment().connect('value-changed', lambda adj, path, drawbar: cbox.do_cmd(path + '/upper_drawbar', None, [drawbar, int(adj.get_value())]), self.path, i)
            slider = Gtk.VScale(Gtk.Adjustment(0, 0, 8, 1, 1))
            slider.props.digits = 0
            table.attach(slider, i + 1, i + 2, 1, 2)
            self.drawbars['l%d' % i] = slider.get_adjustment()
            slider.get_adjustment().connect('value-changed', lambda adj, path, drawbar: cbox.do_cmd(path + '/lower_drawbar', None, [drawbar, int(adj.get_value())]), self.path, i)
        panel.add(table)
        self.add(panel)
        self.refresh()
        
    def refresh(self):
        attribs = cbox.GetThings("%s/status" % self.path, ['%upper_drawbar', '%lower_drawbar', '%pedal_drawbar'], [])
        for i in range(9):
            self.drawbars['u%d' % i].set_value(attribs.upper_drawbar[i])
            self.drawbars['l%d' % i].set_value(attribs.lower_drawbar[i])
        for row, name, flag, options in TonewheelOrganWindow.combos:
            combo = self.combos[flag]
            combo.handler_block(combo.update_handler)
            combo.set_active(ls_index(combo.get_model(), getattr(attribs, flag), 0))
            combo.handler_unblock(combo.update_handler)
        
instrument_window_map = {
    'stream_player' : StreamWindow,
    'fluidsynth' : FluidsynthWindow,
    'sampler' : SamplerWindow,
    'tonewheel_organ' : TonewheelOrganWindow
}

