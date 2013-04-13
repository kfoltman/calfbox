import sys
sys.argv  = []
from gi.repository import GObject, Gdk, Gtk
import math

sys.path = ["./py"] + sys.path

import cbox
from gui_tools import *
import fx_gui
import instr_gui
import drumkit_editor
import drum_pattern_editor

class SceneDialog(SelectObjectDialog):
    title = "Select a scene"
    def __init__(self, parent):
        SelectObjectDialog.__init__(self, parent)
    def update_model(self, model):
        for s in cbox.Config.sections("scene:"):
            title = s["title"]
            model.append((s.name[6:], "Scene", s.name, title))
        for s in cbox.Config.sections("instrument:"):
            title = s["title"]
            model.append((s.name[11:], "Instrument", s.name, title))
        for s in cbox.Config.sections("layer:"):
            title = s["title"]
            model.append((s.name[6:], "Layer", s.name, title))

class AddLayerDialog(SelectObjectDialog):
    title = "Add a layer"
    def __init__(self, parent):
        SelectObjectDialog.__init__(self, parent)
    def update_model(self, model):
        for s in cbox.Config.sections("instrument:"):
            title = s["title"]
            model.append((s.name[11:], "Instrument", s.name, title))
        for s in cbox.Config.sections("layer:"):
            title = s["title"]
            model.append((s.name[6:], "Layer", s.name, title))

class PlayPatternDialog(SelectObjectDialog):
    title = "Play a drum pattern"
    def __init__(self, parent):
        SelectObjectDialog.__init__(self, parent)
    def update_model(self, model):
        model.append((None, "Stop", "", ""))
        for s in cbox.Config.sections("drumpattern:"):
            title = s["title"]
            model.append((s.name[12:], "Pattern", s.name, title))
        for s in cbox.Config.sections("drumtrack:"):
            title = s["title"]
            model.append((s.name[10:], "Track", s.name, title))

in_channels_ls = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
in_channels_ls.append((0, "All"))
out_channels_ls = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
out_channels_ls.append((0, "Same"))
for i in range(1, 17):
    in_channels_ls.append((i, str(i)))
    out_channels_ls.append((i, str(i)))
notes_ls = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
opt_notes_ls = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
opt_notes_ls.append((-1, "N/A"))
for i in range(0, 128):
    notes_ls.append((i, note_to_name(i)))
    opt_notes_ls.append((i, note_to_name(i)))
transpose_ls = Gtk.ListStore(GObject.TYPE_INT, GObject.TYPE_STRING)
for i in range(-60, 61):
    transpose_ls.append((i, str(i)))

class SceneLayersModel(Gtk.ListStore):
    def __init__(self):
        Gtk.ListStore.__init__(self, GObject.TYPE_STRING, GObject.TYPE_BOOLEAN, 
            GObject.TYPE_INT, GObject.TYPE_INT, GObject.TYPE_BOOLEAN,
            GObject.TYPE_INT, GObject.TYPE_INT, GObject.TYPE_INT, GObject.TYPE_INT, GObject.TYPE_STRING)
    #def make_row_item(self, opath, tree_path):
    #    return opath % self[(1 + int(tree_path))]
    def make_row_item(self, opath, tree_path):
        return cbox.Document.uuid_cmd(self[int(tree_path)][-1], opath)
    def refresh(self, scene_status):
        self.clear()
        for layer in scene_status.layers:
            ls = layer.status()
            self.append((ls.instrument_name, ls.enable != 0, ls.in_channel, ls.out_channel, ls.consume != 0, ls.low_note, ls.high_note, ls.fixed_note, ls.transpose, layer.uuid))

class SceneLayersView(Gtk.TreeView):
    def __init__(self, model):
        Gtk.TreeView.__init__(self, model)
        self.enable_model_drag_source(Gdk.ModifierType.BUTTON1_MASK, [("text/plain", 0, 1)], Gdk.DragAction.MOVE)
        self.enable_model_drag_dest([("text/plain", Gtk.TargetFlags.SAME_APP | Gtk.TargetFlags.SAME_WIDGET, 1)], Gdk.DragAction.MOVE)
        self.connect('drag_data_get', self.drag_data_get)
        self.connect('drag_data_received', self.drag_data_received)
        self.insert_column_with_attributes(0, "On?", standard_toggle_renderer(model, "/enable", 1), active=1)
        self.insert_column_with_attributes(1, "Name", Gtk.CellRendererText(), text=0)
        self.insert_column_with_data_func(2, "In Ch#", standard_combo_renderer(model, in_channels_ls, "/in_channel", 2), lambda column, cell, model, iter, data: cell.set_property('text', "%s" % model[iter][2] if model[iter][2] != 0 else 'All'), None)
        self.insert_column_with_data_func(3, "Out Ch#", standard_combo_renderer(model, out_channels_ls, "/out_channel", 3), lambda column, cell, model, iter, data: cell.set_property('text', "%s" % model[iter][3] if model[iter][3] != 0 else 'Same'), None)
        self.insert_column_with_attributes(4, "Eat?", standard_toggle_renderer(model, "/consume", 4), active=4)
        self.insert_column_with_data_func(5, "Lo N#", standard_combo_renderer(model, notes_ls, "/low_note", 5), lambda column, cell, model, iter, data: cell.set_property('text', note_to_name(model[iter][5])), None)
        self.insert_column_with_data_func(6, "Hi N#", standard_combo_renderer(model, notes_ls, "/high_note", 6), lambda column, cell, model, iter, data: cell.set_property('text', note_to_name(model[iter][6])), None)
        self.insert_column_with_data_func(7, "Fix N#", standard_combo_renderer(model, opt_notes_ls, "/fixed_note", 7), lambda column, cell, model, iter, data: cell.set_property('text', note_to_name(model[iter][7])), None)
        self.insert_column_with_attributes(8, "Transpose", standard_combo_renderer(model, transpose_ls, "/transpose", 8), text=8)
    def drag_data_get(self, treeview, context, selection, target_id, etime):
        cursor = treeview.get_cursor()
        if cursor is not None:
            selection.set('text/plain', 8, str(cursor[0][0]))
    def drag_data_received(self, treeview, context, x, y, selection, info, etime):
        src_row = int(selection.data)
        dest_row_info = treeview.get_dest_row_at_pos(x, y)
        if dest_row_info is not None:
            dest_row = dest_row_info[0][0]
            #print src_row, dest_row, dest_row_info[1]
            scene = cbox.Document.get_scene()
            scene.move_layer(src_row, dest_row)
            self.get_model().refresh(scene.status())

class SceneAuxBusesModel(Gtk.ListStore):
    def __init__(self):
        Gtk.ListStore.__init__(self, GObject.TYPE_STRING, GObject.TYPE_STRING)
    def refresh(self, scene_status):
        self.clear()
        for aux_name, aux_obj in scene_status.auxes.items():
            slot = aux_obj.slot.status()
            self.append((slot.insert_preset, slot.insert_engine))

class SceneAuxBusesView(Gtk.TreeView):
    def __init__(self, model):
        Gtk.TreeView.__init__(self, model)
        self.insert_column_with_attributes(0, "Name", Gtk.CellRendererText(), text=0)
        self.insert_column_with_attributes(1, "Engine", Gtk.CellRendererText(), text=1)
    def get_current_row(self):
        if self.get_cursor()[0] is None:
            return None, None
        row = self.get_cursor()[0][0]
        return row + 1, self.get_model()[row]

class StatusBar(Gtk.Statusbar):
    def __init__(self):
        Gtk.Statusbar.__init__(self)
        self.sample_rate_label = Gtk.Label("")
        self.pack_start(self.sample_rate_label, False, False, 2)
        self.status = self.get_context_id("Status")
        self.sample_rate = self.get_context_id("Sample rate")
        self.push(self.status, "")
        self.push(self.sample_rate, "-")
    def update(self, status, sample_rate):
        self.pop(self.status)
        self.push(self.status, status)
        self.sample_rate_label.set_text("%s Hz" % sample_rate)

class MainWindow(Gtk.Window):
    def __init__(self):
        Gtk.Window.__init__(self, Gtk.WindowType.TOPLEVEL)
        self.vbox = Gtk.VBox(spacing = 5)
        self.add(self.vbox)
        self.create()
        set_timer(self, 30, self.update)
        self.drum_pattern_editor = None
        self.drumkit_editor = None

    def create(self):
        self.menu_bar = Gtk.MenuBar()
        
        self.menu_bar.append(create_menu("_Scene", [
            ("_Load", self.load_scene),
            ("_Quit", self.quit),
        ]))
        self.menu_bar.append(create_menu("_Layer", [
            ("_Add", self.layer_add),
            ("_Remove", self.layer_remove),
        ]))
        self.menu_bar.append(create_menu("_AuxBus", [
            ("_Add", self.aux_bus_add),
            ("_Edit", self.aux_bus_edit),
            ("_Remove", self.aux_bus_remove),
        ]))
        self.menu_bar.append(create_menu("_Tools", [
            ("_Drum Kit Editor", self.tools_drumkit_editor),
            ("_Play Drum Pattern", self.tools_play_drum_pattern),
            ("_Edit Drum Pattern", self.tools_drum_pattern_editor),
            ("_Un-zombify", self.tools_unzombify),
            ("_Object list", self.tools_object_list),
            ("_Wave bank dump", self.tools_wave_bank_dump),
        ]))
        
        self.vbox.pack_start(self.menu_bar, False, False, 0)
        rt_status = cbox.Document.get_rt().status()
        scene = cbox.Document.get_scene()
        self.nb = Gtk.Notebook()
        self.vbox.add(self.nb)
        self.nb.append_page(self.create_master(scene), Gtk.Label("Master"))
        self.status_bar = StatusBar()
        self.vbox.pack_start(self.status_bar, False, False, 0)
        self.create_instrument_pages(scene.status(), rt_status)

    def create_master(self, scene):
        scene_status = scene.status()
        self.master_info = left_label("")
        self.timesig_info = left_label("")
        
        t = Gtk.Table(3, 8)
        t.set_col_spacings(5)
        t.set_row_spacings(5)
        
        t.attach(bold_label("Scene"), 0, 1, 0, 1, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        self.scene_label = left_label(scene_status.name)
        t.attach(self.scene_label, 1, 3, 0, 1, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)

        self.title_label = left_label(scene_status.title)
        t.attach(bold_label("Title"), 0, 1, 1, 2, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        t.attach(self.title_label, 1, 3, 1, 2, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        
        t.attach(bold_label("Play pos"), 0, 1, 2, 3, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        t.attach(self.master_info, 1, 3, 2, 3, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        
        t.attach(bold_label("Time sig"), 0, 1, 3, 4, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        t.attach(self.timesig_info, 1, 2, 3, 4, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        hb = Gtk.HButtonBox()
        b = Gtk.Button("Play")
        b.connect('clicked', lambda w: cbox.Transport.play())
        hb.pack_start(b, False, False, 5)
        b = Gtk.Button("Stop")
        b.connect('clicked', lambda w: cbox.Transport.stop())
        hb.pack_start(b, False, False, 5)
        b = Gtk.Button("Rewind")
        b.connect('clicked', lambda w: cbox.Transport.seek_ppqn(0))
        hb.pack_start(b, False, False, 5)
        b = Gtk.Button("Panic")
        b.connect('clicked', lambda w: cbox.Transport.panic())
        hb.pack_start(b, False, False, 5)
        t.attach(hb, 2, 3, 3, 4, Gtk.AttachOptions.EXPAND, Gtk.AttachOptions.SHRINK)
        
        t.attach(bold_label("Tempo"), 0, 1, 4, 5, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        self.tempo_adj = Gtk.Adjustment(40, 40, 300, 1, 5, 0)
        self.tempo_adj.connect('value_changed', adjustment_changed_float, cbox.VarPath("/master/set_tempo"))
        t.attach(standard_hslider(self.tempo_adj), 1, 3, 4, 5, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)

        t.attach(bold_label("Transpose"), 0, 1, 5, 6, Gtk.AttachOptions.SHRINK | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        self.transpose_adj = Gtk.Adjustment(scene_status.transpose, -24, 24, 1, 5, 0)
        self.transpose_adj.connect('value_changed', adjustment_changed_int, cbox.VarPath('/scene/transpose'))
        t.attach(standard_align(Gtk.SpinButton(adjustment = self.transpose_adj), 0, 0, 0, 0), 1, 3, 5, 6, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        
        self.layers_model = SceneLayersModel()
        self.layers_view = SceneLayersView(self.layers_model)
        t.attach(standard_vscroll_window(-1, 160, self.layers_view), 0, 3, 6, 7, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        
        self.auxes_model = SceneAuxBusesModel()
        self.auxes_view = SceneAuxBusesView(self.auxes_model)
        t.attach(standard_vscroll_window(-1, 120, self.auxes_view), 0, 3, 7, 8, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
        
        self.layers_model.refresh(scene_status)
        self.auxes_model.refresh(scene_status)
        
        return t
        
    def quit(self, w):
        self.destroy()
        
    def load_scene(self, w):
        d = SceneDialog(self)
        response = d.run()
        try:
            if response == Gtk.ResponseType.OK:
                scene = cbox.Document.get_scene()
                item_name, item_type, item_key, item_label = d.get_selected_object()
                if item_type == 'Scene':
                    scene.load(item_name)
                elif item_type == 'Layer':
                    scene.clear()
                    scene.add_layer(item_name)
                elif item_type == 'Instrument':
                    scene.clear()
                    scene.add_instrument_layer(item_name)
                scene_status = scene.status()
                self.scene_label.set_text(scene_status.name)
                self.title_label.set_text(scene_status.title)
                self.refresh_instrument_pages(scene_status)
        finally:
            d.destroy()

    def layer_add(self, w):
        d = AddLayerDialog(self)
        response = d.run()
        try:
            if response == Gtk.ResponseType.OK:
                scene = cbox.Document.get_scene()
                item_name, item_type, item_key, item_label = d.get_selected_object()
                if item_type == 'Layer':
                    scene.add_layer(item_name)
                elif item_type == 'Instrument':
                    scene.add_instrument_layer(item_name)
                self.refresh_instrument_pages()
        finally:
            d.destroy()

    def layer_remove(self, w):
        if self.layers_view.get_cursor()[0] is not None:
            pos = self.layers_view.get_cursor()[0][0]
            cbox.Document.get_scene().delete_layer(pos)
            self.refresh_instrument_pages()
            
    def aux_bus_add(self, w):
        d = fx_gui.LoadEffectDialog(self)
        response = d.run()
        try:
            cbox.do_cmd("/scene/load_aux", None, [d.get_selected_object()[0]])
            self.refresh_instrument_pages()
        finally:
            d.destroy()
    def aux_bus_remove(self, w):
        rowid, row = self.auxes_view.get_current_row()
        if rowid is None:
            return
        cbox.do_cmd("/scene/delete_aux", None, [row[0]])
        self.refresh_instrument_pages()
        
    def aux_bus_edit(self, w):
        rowid, row = self.auxes_view.get_current_row()
        if rowid is None:
            return
        wclass = fx_gui.effect_window_map[row[1]]
        popup = wclass("Aux: %s" % row[0], self, "/scene/aux/%s/slot/engine" % row[0])
        popup.show_all()
        popup.present()
        
    def tools_unzombify(self, w):
        cbox.do_cmd("/rt/cycle", None, [])

    def tools_drumkit_editor(self, w):
        if self.drumkit_editor is None:
            self.drumkit_editor = drumkit_editor.EditorDialog(self)
            self.refresh_instrument_pages()
            self.drumkit_editor.connect('destroy', self.on_drumkit_editor_destroy)
            self.drumkit_editor.show_all()
        self.drumkit_editor.present()
        
    def on_drumkit_editor_destroy(self, w):
        self.drumkit_editor = None
        
    def tools_object_list(self, w):
        cbox.Document.dump()

    def tools_wave_bank_dump(self, w):
        for w in cbox.get_thing('/waves/list', '/waveform', [str]):
            info = cbox.GetThings("/waves/info", ["filename", "name", "bytes", "loop"], [w])
            print("%s: %d bytes, loop = %s" % (info.filename, info.bytes, info.loop))

    def tools_play_drum_pattern(self, w):
        d = PlayPatternDialog(self)
        response = d.run()
        try:
            if response == Gtk.ResponseType.OK:
                row = d.get_selected_object()
                if row[1] == 'Pattern':
                    song = cbox.Document().get_song()
                    song.loop_single_pattern(lambda: song.load_drum_pattern(row[0]))
                elif row[1] == 'Track':
                    song = cbox.Document().get_song()
                    song.loop_single_pattern(lambda: song.load_drum_track(row[0]))
                elif row[1] == 'Stop':
                    song = cbox.Document().get_song()
                    song.clear()
                    song.update_playback()
                tracks = song.status().tracks
                if len(tracks):
                    for track_item in tracks:
                        track_item.track.set_external_output(cbox.Document.get_scene().uuid)
                song.update_playback()

        finally:
            d.destroy()

    def tools_drum_pattern_editor(self, w):
        if self.drum_pattern_editor is None:
            length = drum_pattern_editor.PPQN * 4
            pat_data = cbox.Pattern.get_pattern()
            if pat_data is not None:
                pat_data, length = pat_data
            self.drum_pattern_editor = drum_pattern_editor.DrumSeqWindow(length, pat_data)
            self.drum_pattern_editor.set_title("Drum pattern editor")
            self.drum_pattern_editor.show_all()
            self.drum_pattern_editor.connect('destroy', self.on_drum_pattern_editor_destroy)
            self.drum_pattern_editor.pattern.connect('changed', self.on_drum_pattern_changed)
            self.drum_pattern_editor.pattern.changed()
        self.drum_pattern_editor.present()
        
    def on_drum_pattern_changed(self, pattern):
        data = bytearray()
        for i in pattern.items():
            ch = i.channel - 1
            data += cbox.Pattern.serialize_event(int(i.pos), 0x90 + ch, int(i.row), int(i.vel))
            if i.len > 1:
                data += cbox.Pattern.serialize_event(int(i.pos + i.len - 1), 0x80 + ch, int(i.row), int(i.vel))
            else:
                data += cbox.Pattern.serialize_event(int(i.pos + 1), 0x80 + ch, int(i.row), int(i.vel))

        length = pattern.get_length()

        song = cbox.Document().get_song()
        song.loop_single_pattern(lambda: song.pattern_from_blob(data, length))

    def on_drum_pattern_editor_destroy(self, w):
        self.drum_pattern_editor = None

    def refresh_instrument_pages(self, scene_status = None):
        self.delete_instrument_pages()
        rt_status = cbox.Document.get_rt().status()
        if scene_status is None:
            scene_status = cbox.Document.get_scene().status()
        self.layers_model.refresh(scene_status)
        self.auxes_model.refresh(scene_status)
        self.create_instrument_pages(scene_status, rt_status)
        self.nb.show_all()
        self.title_label.set_text(scene_status.title)

    def create_instrument_pages(self, scene_status, rt_status):
        self.path_widgets = {}
        self.path_popups = {}
        self.fx_choosers = {}
        
        outputs_ls = Gtk.ListStore(GObject.TYPE_STRING, GObject.TYPE_INT)
        for out in range(0, rt_status.audio_channels[1]//2):
            outputs_ls.append(("Out %s/%s" % (out * 2 + 1, out * 2 + 2), out))
            
        auxbus_ls = Gtk.ListStore(GObject.TYPE_STRING, GObject.TYPE_STRING)
        auxbus_ls.append(("", ""))
        for bus_name in scene_status.auxes.keys():
            auxbus_ls.append(("Aux: %s" % bus_name, bus_name))
            
        for iname, (iengine, iobj) in scene_status.instruments.items():
            ipath = "/scene/instr/%s" % iname
            idata = iobj.status()
            #attribs = cbox.GetThings("/scene/instr_info", ['engine', 'name'], [i])
            #markup += '<b>Instrument %d:</b> engine %s, name %s\n' % (i, attribs.engine, attribs.name)
            b = Gtk.VBox(spacing = 5)
            b.set_border_width(5)
            b.pack_start(Gtk.Label("Engine: %s" % iengine), False, False, 5)
            b.pack_start(Gtk.HSeparator(), False, False, 5)
            t = Gtk.Table(1 + idata.outputs, 7)
            t.set_col_spacings(5)
            t.attach(bold_label("Instr. output", 0.5), 0, 1, 0, 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
            t.attach(bold_label("Send to", 0.5), 1, 2, 0, 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
            t.attach(bold_label("Gain [dB]", 0.5), 2, 3, 0, 1, 0, Gtk.AttachOptions.SHRINK)
            t.attach(bold_label("Effect", 0.5), 3, 4, 0, 1, 0, Gtk.AttachOptions.SHRINK)
            t.attach(bold_label("Preset", 0.5), 4, 7, 0, 1, 0, Gtk.AttachOptions.SHRINK)
            b.pack_start(t, False, False, 5)
            
            y = 1
            for o in range(1, idata.outputs + 1):
                is_aux = o >= idata.aux_offset
                if not is_aux:
                    opath = "%s/output/%s" % (ipath, o)
                    output_name = "Out %s" % o
                else:
                    opath = "%s/aux/%s" % (ipath, o - idata.aux_offset + 1)
                    output_name = "Aux %s" % (o - idata.aux_offset + 1)
                odata = cbox.GetThings(opath + "/status", ['gain', 'output', 'bus', 'insert_engine', 'insert_preset', 'bypass'], [])
                engine = odata.insert_engine
                preset = odata.insert_preset
                bypass = odata.bypass
                
                t.attach(Gtk.Label(output_name), 0, 1, y, y + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
                
                if not is_aux:
                    cb = standard_combo(outputs_ls, odata.output - 1)
                    cb.connect('changed', combo_value_changed, cbox.VarPath(opath + '/output'), 1)
                else:
                    cb = standard_combo(auxbus_ls, ls_index(auxbus_ls, odata.bus, 1))
                    cb.connect('changed', combo_value_changed_use_column, cbox.VarPath(opath + '/bus'), 1)
                t.attach(cb, 1, 2, y, y + 1, Gtk.AttachOptions.SHRINK, Gtk.AttachOptions.SHRINK)
                                    
                adj = Gtk.Adjustment(odata.gain, -96, 24, 1, 6, 0)
                adj.connect('value_changed', adjustment_changed_float, cbox.VarPath(opath + '/gain'))
                t.attach(standard_hslider(adj), 2, 3, y, y + 1, Gtk.AttachOptions.EXPAND | Gtk.AttachOptions.FILL, Gtk.AttachOptions.SHRINK)
                
                chooser = fx_gui.InsertEffectChooser(opath, "%s: %s" % (iname, output_name), engine, preset, bypass, self)
                self.fx_choosers[opath] = chooser
                t.attach(chooser.fx_engine, 3, 4, y, y + 1, 0, Gtk.AttachOptions.SHRINK)
                t.attach(chooser.fx_preset, 4, 5, y, y + 1, 0, Gtk.AttachOptions.SHRINK)
                t.attach(chooser.fx_edit, 5, 6, y, y + 1, 0, Gtk.AttachOptions.SHRINK)
                t.attach(chooser.fx_bypass, 6, 7, y, y + 1, 0, Gtk.AttachOptions.SHRINK)
                y += 1
            if iengine in instr_gui.instrument_window_map:
                b.pack_start(Gtk.HSeparator(), False, False, 5)
                b.pack_start(instr_gui.instrument_window_map[iengine](iname, iobj), True, True, 5)
            self.nb.append_page(b, Gtk.Label(iname))
        self.update()
        
    def delete_instrument_pages(self):
        while self.nb.get_n_pages() > 1:
            self.nb.remove_page(self.nb.get_n_pages() - 1)
            
    def update(self):
        cbox.call_on_idle()
        master = cbox.Transport.status()
        if master.tempo is not None:
            self.master_info.set_markup('%s (%s)' % (master.pos, master.pos_ppqn))
            self.timesig_info.set_markup("%s/%s" % tuple(master.timesig))
            self.tempo_adj.set_value(master.tempo)
        state = cbox.Document.get_rt().status().state
        self.status_bar.update(state[1], master.sample_rate)
        return True

def do_quit(window):
    Gtk.main_quit()

w = MainWindow()
w.set_title("My UI")
w.show_all()
w.connect('destroy', do_quit)

Gtk.main()

