import cbox
from gui_tools import *

class StreamWindow(Gtk.VBox):
    def __init__(self, instrument, iobj):
        Gtk.Widget.__init__(self)
        self.engine = iobj.engine
        self.path = self.engine.path
        
        panel = Gtk.VBox(spacing=5)
        
        self.filebutton = Gtk.FileChooserButton("Streamed file")
        self.filebutton.set_action(Gtk.FileChooserAction.OPEN)
        self.filebutton.set_local_only(True)
        self.filebutton.set_filename(self.engine.status().filename)
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV", "*.ogg", "*.OGG", "*.flac", "*.FLAC"], "All loadable audio files"))
        self.filebutton.add_filter(standard_filter(["*.wav", "*.WAV"], "RIFF WAVE files"))
        self.filebutton.add_filter(standard_filter(["*.ogg", "*.OGG"], "OGG container files"))
        self.filebutton.add_filter(standard_filter(["*.flac", "*.FLAC"], "FLAC files"))
        self.filebutton.add_filter(standard_filter(["*"], "All files"))
        self.filebutton.connect('file-set', self.file_set)
        hpanel = Gtk.HBox(spacing = 5)
        hpanel.pack_start(Gtk.Label.new_with_mnemonic("_Play file:"), False, False, 5)
        hpanel.pack_start(self.filebutton, True, True, 5)
        panel.pack_start(hpanel, False, False, 5)

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
        self.play_button.connect('clicked', lambda x: self.engine.play())
        self.rewind_button.connect('clicked', lambda x: self.engine.seek(0))
        self.stop_button.connect('clicked', lambda x: self.engine.stop())
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
        self.engine.seek(adjustment.get_value())
        
    def file_set(self, button):
        self.engine.load(button.get_filename())
        

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
        patches = self.engine.get_patches()
        ch_patches = self.engine.status().patches
        self.mapping = {}
        for id in patches:
            self.mapping[id] = len(self.mapping)
            self.patches.append((self.fmt_patch_name(patches[id], id), id))
        self.patch_combo_update()

    def patch_combo_changed(self, combo, channel):
        if combo.get_active() == -1:
            return
        self.engine.set_patch(channel, self.patches[combo.get_active()][1])

    def patch_combo_update(self):
        patch = self.engine.status().patches
        for i in range(16):
            cb = self.patch_combos[i]
            old_patch_index = cb.get_active() if cb.get_active() >= 0 else -1
            patch_id = patch[i + 1][0]
            current_patch_index = self.mapping[patch_id] if (patch_id >= 0 and patch_id in self.mapping) else -1
            if old_patch_index != current_patch_index:
                cb.set_active(current_patch_index)
        #self.status_label.set_markup(s)
        return True
        
    def fmt_patch_name(self, patch, id):
        return "%s (%s)" % (patch, id)
        
class FluidsynthWindow(Gtk.VBox, WithPatchTable):
    def __init__(self, instrument, iobj):
        Gtk.VBox.__init__(self)
        self.engine = iobj.engine
        self.path = self.engine.path
        print (iobj.path)
        
        attribs = iobj.status()

        panel = Gtk.VBox(spacing=5)
        table = Gtk.Table(2, 1)
        IntSliderRow("Polyphony", "polyphony", 2, 256).add_row(table, 0, cbox.VarPath(self.path), attribs)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True, 5)

        hpanel = Gtk.HBox(spacing = 5)
        self.filebutton = Gtk.FileChooserButton("Soundfont")
        self.filebutton.set_action(Gtk.FileChooserAction.OPEN)
        self.filebutton.set_local_only(True)
        self.filebutton.set_filename(cbox.GetThings("%s/status" % self.path, ['soundfont'], []).soundfont)
        self.filebutton.add_filter(standard_filter(["*.sf2", "*.SF2"], "SF2 Soundfonts"))
        self.filebutton.add_filter(standard_filter(["*"], "All files"))
        hpanel.pack_start(Gtk.Label.new_with_mnemonic("_Load SF2:"), False, False, 5)
        hpanel.pack_start(self.filebutton, True, True, 5)
        unload = Gtk.Button.new_with_mnemonic("_Unload")
        hpanel.pack_start(unload, False, False, 5)
        unload.connect('clicked', self.unload)
        panel.pack_start(hpanel, False, False, 5)

        self.filebutton.connect('file-set', self.file_set)

        self.add(panel)
    def file_set(self, button):
        self.engine.load_soundfont(button.get_filename())
        self.update_model()
    def unload(self, button):
        self.filebutton.set_filename('')

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
    def __init__(self, instrument, iobj):
        Gtk.VBox.__init__(self)
        self.engine = iobj.engine
        self.path = self.engine.path
        
        iattribs = iobj.status()
        attribs = self.engine.status()

        panel = Gtk.VBox(spacing=5)
        table = Gtk.Table(2, 2)
        table.set_col_spacings(5)
        IntSliderRow("Polyphony", "polyphony", 1, 128).add_row(table, 0, cbox.VarPath(self.path), attribs)
        self.voices_widget = add_display_row(table, 1, "Voices in use", cbox.VarPath(self.path), attribs, "active_voices")
        panel.pack_start(table, False, False, 5)
        
        WithPatchTable.__init__(self, attribs)
        panel.pack_start(standard_vscroll_window(-1, 160, self.table), True, True, 5)
        self.add(panel)
        hpanel = Gtk.HBox(spacing = 5)

        hpanel.pack_start(Gtk.Label.new_with_mnemonic("Add from _SFZ:"), False, False, 5)
        self.filebutton = Gtk.FileChooserButton("Soundfont")
        self.filebutton.set_action(Gtk.FileChooserAction.OPEN)
        self.filebutton.set_local_only(True)
        #self.filebutton.set_filename(cbox.GetThings("%s/status" % self.path, ['soundfont'], []).soundfont)
        self.filebutton.add_filter(standard_filter(["*.sfz", "*.SFZ"], "SFZ Programs"))
        self.filebutton.add_filter(standard_filter(["*"], "All files"))
        self.filebutton.connect('file-set', self.load_sfz)
        hpanel.pack_start(self.filebutton, False, True, 5)
        
        load_button = Gtk.Button.new_with_mnemonic("Add from _config")
        load_button.connect('clicked', self.load_config)
        hpanel.pack_start(load_button, False, True, 5)
        panel.pack_start(hpanel, False, False, 5)
        
        set_timer(self, 200, self.voices_update)
        self.output_model = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
        for i in range(iattribs.outputs):
            self.output_model.append((i + 1, "Out %d" % (i + 1)))
            
        self.polyphony_labels = {}
        self.output_combos = {}
        for i in range(16):
            button = Gtk.Button("Dump SFZ")
            button.connect("clicked", self.dump_sfz, i + 1)
            self.table.attach(button, 2, 3, i, i + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
            label = Gtk.Label("")
            self.table.attach(label, 3, 4, i, i + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
            self.polyphony_labels[i + 1] = label
            combo = standard_combo(self.output_model, column = 1)
            combo.connect('changed', self.output_combo_changed, i + 1)
            self.table.attach(combo, 4, 5, i, i + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
            self.output_combos[i + 1] = combo
        self.output_combo_update()

    def output_combo_update(self):
        output = self.engine.status().output
        for i in range(16):
            cb = self.output_combos[i + 1]
            old_channel_index = cb.get_active() if cb.get_active() >= 0 else -1
            if old_channel_index != output[1 + i]:
                cb.set_active(output[1 + i])
        #self.status_label.set_markup(s)
        return True

    def output_combo_changed(self, combo, channel):
        if combo.get_active() == -1:
            return
        self.engine.set_output(channel, combo.get_active())
    
    def dump_sfz(self, w, channel):
        attribs = cbox.GetThings("%s/status" % self.path, ['%patch', 'polyphony', 'active_voices'], [])
        prog_no, patch_name = attribs.patch[channel]
        pname, uuid, in_use_cnt = cbox.GetThings("%s/patches" % self.path, ['%patch'], []).patch[prog_no]
        print ("UUID=%s" % uuid)
        patch = cbox.Document.map_uuid(uuid)
        groups = patch.get_groups()
        for r in groups[0].get_children():
            print ("<region> %s" % (r.as_string()))
        for grp in patch.get_groups()[1:]:
            print ("<group> %s" % (grp.as_string()))
            for r in grp.get_children():
                print ("<region> %s" % (r.as_string()))
        
    def load_config(self, event):
        d = LoadProgramDialog(self.get_toplevel())
        response = d.run()
        try:
            if response == Gtk.ResponseType.OK:
                scene = d.get_selected_object()
                pgm_id = self.engine.get_unused_program()
                self.engine.load_patch_from_cfg(pgm_id, scene[2], scene[2][5:])
                self.update_model()
        finally:
            d.destroy()        
        
    def load_sfz(self, button):
        pgm_id = self.engine.get_unused_program()
        self.engine.load_patch_from_file(pgm_id, self.filebutton.get_filename(), self.filebutton.get_filename())
        self.update_model()
        
    def voices_update(self):
        status = self.engine.status()
        self.voices_widget.set_text("%s voices, %s waiting, %s pipes" % (status.active_voices, status.active_prevoices, status.active_pipes))
        for i in range(16):
            self.polyphony_labels[i + 1].set_text("%d voices, %d waiting" % (status.channel_voices[i + 1], status.channel_prevoices[i + 1]))
            
        return True

    def fmt_patch_name(self, patch, id):
        return "%s (%s)" % (patch[0], id)
        
class TonewheelOrganWindow(Gtk.VBox):
    combos = [
        (1, 'Upper', 'upper_vibrato', [(0, 'Off'), (1, 'On')]),
        (1, 'Lower', 'lower_vibrato', [(0, 'Off'), (1, 'On')]),
        (1, 'Mode', 'vibrato_mode', [(0, '1'), (1, '2'), (2, '3')]),
        (1, 'Chorus', 'vibrato_chorus', [(0, 'Off'), (1, 'On')]),
        (2, 'Enable', 'percussion_enable', [(0, 'Off'), (1, 'On')]),
        (2, 'Harmonic', 'percussion_3rd', [(0, '2nd'), (1, '3rd')]),
    ]
    def __init__(self, instrument, iobj):
        Gtk.VBox.__init__(self)
        self.engine = iobj.engine
        self.path = self.engine.path
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
            combo.update_handler = combo.connect('changed', lambda w, setter: setter(w.get_model()[w.get_active()][0]), getattr(self.engine, 'set_' + flag))
            self.combos[flag] = combo
        panel.pack_start(self.hboxes[1], False, False, 5)
        panel.pack_start(self.hboxes[2], False, False, 5)
        table.attach(Gtk.Label("Upper"), 0, 1, 0, 1)
        table.attach(Gtk.Label("Lower"), 0, 1, 1, 2)
        for i in range(9):
            slider = Gtk.VScale(adjustment = Gtk.Adjustment(0, 0, 8, 1, 1))
            slider.props.digits = 0
            table.attach(slider, i + 1, i + 2, 0, 1)
            self.drawbars['u%d' % i] = slider.get_adjustment()
            slider.get_adjustment().connect('value-changed', lambda adj, drawbar: self.engine.set_upper_drawbar(drawbar, int(adj.get_value())), i)
            slider = Gtk.VScale(adjustment = Gtk.Adjustment(0, 0, 8, 1, 1))
            slider.props.digits = 0
            table.attach(slider, i + 1, i + 2, 1, 2)
            self.drawbars['l%d' % i] = slider.get_adjustment()
            slider.get_adjustment().connect('value-changed', lambda adj, drawbar: self.engine.set_lower_drawbar(drawbar, int(adj.get_value())), i)
        panel.add(table)
        self.add(panel)
        self.refresh()
        
    def refresh(self):
        attribs = self.engine.status()
        for i in range(9):
            self.drawbars['u%d' % i].set_value(attribs.upper_drawbar[i])
            self.drawbars['l%d' % i].set_value(attribs.lower_drawbar[i])
        for row, name, flag, options in TonewheelOrganWindow.combos:
            combo = self.combos[flag]
            combo.handler_block(combo.update_handler)
            combo.set_active(ls_index(combo.get_model(), getattr(attribs, flag), 0))
            combo.handler_unblock(combo.update_handler)

class JackInputWindow(Gtk.VBox):
    def __init__(self, instrument, iobj):
        Gtk.VBox.__init__(self)
        print (iobj.status())
        self.engine = iobj.engine
        self.path = self.engine.path
        table = Gtk.Table(2, 2)
        table.props.row_spacing = 10
        no_inputs = cbox.JackIO.status().audio_inputs
        model = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
        model.append((0, "Unconnected"))
        for i in range(no_inputs):
            model.append((1 + i, "Input#%s" % (1 + i)))
        self.combos = []
        for i, name in ((0, "Left"), (1, "Right")):
            table.attach(Gtk.Label(name), 0, 1, i, i + 1)
            combo = standard_combo(model, column = 1)
            table.attach(combo, 1, 2, i, i + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
            combo.update_handler = combo.connect('changed', self.update_inputs)
            self.combos.append(combo)
        self.pack_start(table, False, False, 5)
        self.refresh()

    def update_inputs(self, w):
        def to_base1(value):
            return -1 if value <= 0 else value
        left = to_base1(self.combos[0].get_active())
        right = to_base1(self.combos[1].get_active())
        self.engine.cmd("/inputs", None, left, right)

    def refresh(self):
        inputs = self.engine.status().inputs
        self.combos[0].set_active(max(0, inputs[0]))
        self.combos[1].set_active(max(0, inputs[1]))

instrument_window_map = {
    'stream_player' : StreamWindow,
    'fluidsynth' : FluidsynthWindow,
    'sampler' : SamplerWindow,
    'tonewheel_organ' : TonewheelOrganWindow
}

if int(cbox.Config.get("io", "use_usb") or "0") == 0 and cbox.JackIO.status().audio_inputs > 0:
    instrument_window_map['jack_input'] = JackInputWindow
