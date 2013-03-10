from _cbox import *
import struct

class GetUUID:
    def __init__(self):
        def callback(cmd, fb, args):
            if cmd == "/uuid" and len(args) == 1:
                self.uuid = args[0]
            else:
                raise ValueException("Unexpected callback: %s" % cmd)
        self.callback = callback
    def __call__(self, *args):
        self.callback(*args)
    
class GetThings:
    @staticmethod
    def by_uuid(uuid, cmd, anames, args):
        return GetThings(Document.uuid_cmd(uuid, cmd), anames, args)
    def __init__(self, cmd, anames, args):
        for i in anames:
            if i.startswith("*"):
                setattr(self, i[1:], [])
            elif i.startswith("%"):
                setattr(self, i[1:], {})
            else:
                setattr(self, i, None)
        anames = set(anames)
        self.seq = []
        def update_callback(cmd, fb, args):
            self.seq.append((cmd, fb, args))
            cmd = cmd[1:]
            if cmd in anames:
                if len(args) == 1:
                    setattr(self, cmd, args[0])
                else:
                    setattr(self, cmd, args)
            elif "*" + cmd in anames:
                if len(args) == 1:
                    getattr(self, cmd).append(args[0])
                else:
                    getattr(self, cmd).append(args)
            elif "%" + cmd in anames:
                if len(args) == 2:
                    getattr(self, cmd)[args[0]] = args[1]
                else:
                    getattr(self, cmd)[args[0]] = args[1:]
            elif len(args) == 1:
                setattr(self, cmd, args[0])
        do_cmd(cmd, update_callback, args)
    def __str__(self):
        return str(self.seq)

class VarPath:
    def __init__(self, path, args = []):
        self.path = path
        self.args = args
    def plus(self, subpath, *args):
        return VarPath(self.path if subpath is None else self.path + "/" + subpath, self.args + list(args))
    def set(self, *values):
        do_cmd(self.path, None, self.args + list(values))

class Config:
    @staticmethod
    def sections(prefix = ""):
        return [CfgSection(name) for name in GetThings('/config/sections', ['*section'], [str(prefix)]).section]

    @staticmethod
    def keys(section, prefix = ""):
        return GetThings('/config/keys', ['*key'], [str(section), str(prefix)]).key

    @staticmethod
    def get(section, key):
        return GetThings('/config/get', ['value'], [str(section), str(key)]).value

    @staticmethod
    def set(section, key, value):
        do_cmd('/config/set', None, [str(section), str(key), str(value)])

    @staticmethod
    def delete(section, key):
        do_cmd('/config/delete', None, [str(section), str(key)])

    @staticmethod
    def save(filename = None):
        if filename is None:
            do_cmd('/config/save', None, [])
        else:
            do_cmd('/config/save', None, [str(filename)])
            
    @staticmethod
    def add_section(section, content):
        for line in content.splitlines():
            line = line.strip()
            if line == '' or line.startswith('#'):
                continue
            try:
                key, value = line.split("=", 2)
            except ValueError as err:
                raise ValueError("Cannot parse config line '%s'" % line)
            Config.set(section, key.strip(), value.strip())

class Transport:
    @staticmethod
    def seek_ppqn(ppqn):
        do_cmd('/master/seek_ppqn', None, [int(ppqn)])
    @staticmethod
    def seek_samples(samples):
        do_cmd('/master/seek_samples', None, [int(samples)])
    @staticmethod
    def set_tempo(tempo):
        do_cmd('/master/set_tempo', None, [float(tempo)])
    @staticmethod
    def set_timesig(nom, denom):
        do_cmd('/master/set_timesig', None, [int(nom), int(denom)])
    @staticmethod
    def play():
        do_cmd('/master/play', None, [])
    @staticmethod
    def stop():
        do_cmd('/master/stop', None, [])
    @staticmethod
    def panic():
        do_cmd('/master/panic', None, [])
    @staticmethod
    def status():
        return GetThings("/master/status", ['pos', 'pos_ppqn', 'tempo', 'timesig', 'sample_rate', 'playing'], [])
    @staticmethod
    def tell():
        return GetThings("/master/tell", ['pos', 'pos_ppqn', 'playing'], [])

class JackIO:
    @staticmethod
    def status():
        return GetThings("/io/status", ['client_name', 'audio_inputs', 'audio_outputs', 'buffer_size'], [])
    def create_midi_output(name, autoconnect_spec = None):
        do_cmd("/io/create_midi_output", None, [name, autoconnect_spec if autoconnect_spec is not None else ''])

def call_on_idle(callback = None):
    do_cmd("/on_idle", callback, [])
        
class CfgSection:
    def __init__(self, name):
        self.name = name
        
    def __getitem__(self, key):
        return Config.get(self.name, key)

    def __setitem__(self, key, value):
        Config.set(self.name, key, value)
        
    def __delitem__(self, key):
        Config.delete(self.name, key)
        
    def keys(self, prefix = ""):
        return Config.keys(self.name, prefix)
        

class Pattern:
    @staticmethod
    def get_pattern():
        pat_data = GetThings("/get_pattern", ['pattern'], []).pattern
        if pat_data is not None:
            pat_blob, length = pat_data
            pat_data = []
            ofs = 0
            while ofs < len(pat_blob):
                data = list(struct.unpack_from("iBBbb", pat_blob, ofs))
                data[1:2] = []
                pat_data.append(tuple(data))
                ofs += 8
            return pat_data, length
        return None
        
    @staticmethod
    def serialize_event(time, *data):
        if len(data) >= 1 and len(data) <= 3:
            return struct.pack("iBBbb"[0:2 + len(data)], int(time), len(data), *[int(v) for v in data])
        raise ValueError("Invalid length of an event (%d)" % len(data))

class Document:
    classmap = {}
    objmap = {}
    @staticmethod
    def dump():
        do_cmd("/doc/dump", None, [])
    @staticmethod
    def uuid_cmd(uuid, cmd):
        return "/doc/uuid/%s%s" % (uuid, cmd)
    @staticmethod
    def get_uuid(path):
        return GetThings("%s/get_uuid" % path, ["uuid"], []).uuid
    @staticmethod
    def get_obj_class(uuid):
        return GetThings(Document.uuid_cmd(uuid, "/get_class_name"), ["class_name"], []).class_name
    @staticmethod
    def get_song():
        return Document.map_uuid(Document.get_uuid("/song"))
    @staticmethod
    def get_scene():
        return Document.map_uuid(Document.get_uuid("/scene"))
    @staticmethod
    def get_rt():
        return Document.map_uuid(Document.get_uuid("/rt"))
    @staticmethod
    def new_scene(srate, bufsize):
        fb = GetUUID()
        do_cmd("/new_scene", fb, [int(srate), int(bufsize)])
        return Document.map_uuid(fb.uuid)
    @staticmethod
    def map_uuid(uuid):
        if uuid in Document.objmap:
            return Document.objmap[uuid]
        try:
            oclass = Document.get_obj_class(uuid)
        except Exception as e:
            print ("Note: Cannot get class for " + uuid)
            Document.dump()
            raise
        o = Document.classmap[oclass](uuid)
        Document.objmap[uuid] = o
        if hasattr(o, 'init_object'):
            o.init_object()
        return o

class SetterMaker():
    def __init__(self, obj, path):
        self.obj = obj
        self.path = path
    def set(self, value):
        self.obj.cmd(self.path, None, value)
    def set2(self, key, value):
        self.obj.cmd(self.path, None, key, value)

class NonDocObj(object):
    def __init__(self, path, status_field_list):
        self.path = path
        self.status_fields = []
        for sf in status_field_list:
            if sf.startswith("="):
                sf = sf[1:]
                if sf.startswith("%"):
                    sf2 = sf[1:]
                    self.__dict__['set_' + sf2] = SetterMaker(self, "/" + sf2).set2
                else:
                    self.__dict__['set_' + sf] = SetterMaker(self, "/" + sf).set
            self.status_fields.append(sf)

    def cmd(self, cmd, fb = None, *args):
        do_cmd(self.path + cmd, fb, list(args))

    def cmd_makeobj(self, cmd, *args):
        fb = GetUUID()
        do_cmd(self.path + cmd, fb, list(args))
        return Document.map_uuid(fb.uuid)

    def get_things(self, cmd, fields, *args):
        return GetThings(self.path + cmd, fields, list(args))

    def make_path(self, path):
        return self.path + path

    def status(self):
        return self.transform_status(self.get_things("/status", self.status_fields))
        
    def transform_status(self, status):
        return status

class DocObj(NonDocObj):
    def __init__(self, uuid, status_field_list):
        NonDocObj.__init__(self, Document.uuid_cmd(uuid, ''), status_field_list)
        self.uuid = uuid

    def delete(self):
        self.cmd("/delete")

class DocPattern(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["event_count", "loop_end", "name"])
    def set_name(self, name):
        self.cmd("/name", None, name)
Document.classmap['cbox_midi_pattern'] = DocPattern
        
class ClipItem:
    def __init__(self, pos, offset, length, pattern, clip):
        self.pos = pos
        self.offset = offset
        self.length = length
        self.pattern = Document.map_uuid(pattern)
        self.clip = Document.map_uuid(clip)
    def __str__(self):
        return "pos=%d offset=%d length=%d pattern=%s clip=%s" % (self.pos, self.offset, self.length, self.pattern.uuid, self.clip.uuid)
    def __eq__(self, other):
        return str(self) == str(other)

class DocTrackClip(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["pos", "offset", "length", "pattern", "uuid"])
    def transform_status(self, status):
        return ClipItem(status.pos, status.offset, status.length, status.pattern, status.uuid)

Document.classmap['cbox_track_item'] = DocTrackClip
        
class DocTrackStatus:
    name = None
    clips = None
    external_output = None
    
class DocTrack(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["*clip", "=name", "=external_output"])
    def add_clip(self, pos, offset, length, pattern):
        return self.cmd_makeobj("/add_clip", int(pos), int(offset), int(length), pattern.uuid)
    def transform_status(self, status):
        res = DocTrackStatus()
        res.name = status.name
        res.clips = [ClipItem(*c) for c in status.clip]
        res.external_output = status.external_output
        return res
Document.classmap['cbox_track'] = DocTrack

class TrackItem:
    def __init__(self, name, count, track):
        self.name = name
        self.count = count
        self.track = Document.map_uuid(track)

class PatternItem:
    def __init__(self, name, length, pattern):
        self.name = name
        self.length = length
        self.pattern = Document.map_uuid(pattern)

class DocSongStatus:
    tracks = None
    patterns = None

class DocSong(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["*track", "*pattern", "*mti", 'loop_start', 'loop_end'])
    def clear(self):
        return self.cmd("/clear", None)
    def set_loop(self, ls, le):
        return self.cmd("/set_loop", None, int(ls), int(le))
    def set_mti(self, pos, tempo = None, timesig_nom = None, timesig_denom = None):
        self.cmd("/set_mti", None, int(pos), float(tempo) if tempo is not None else -1.0, int(timesig_nom) if timesig_nom is not None else -1, int(timesig_denom) if timesig_denom else -1)
    def add_track(self):
        return self.cmd_makeobj("/add_track")
    def load_drum_pattern(self, name):
        return self.cmd_makeobj("/load_pattern", name, 1)
    def load_drum_track(self, name):
        return self.cmd_makeobj("/load_track", name, 1)
    def pattern_from_blob(self, blob, length):
        return self.cmd_makeobj("/load_blob", bytearray(blob), int(length))
    def loop_single_pattern(self, loader):
        self.clear()
        track = self.add_track()
        pat = loader()
        length = pat.status().loop_end
        track.add_clip(0, 0, length, pat)
        self.set_loop(0, length)
        self.update_playback()
        
    def transform_status(self, status):
        res = DocSongStatus()
        res.tracks = [TrackItem(*t) for t in status.track]
        res.patterns = [PatternItem(*t) for t in status.pattern]
        res.mtis = [tuple(t) for t in status.mti]
        return res
    def update_playback(self):
        # XXXKF Maybe make it a song-level API instead of global
        do_cmd("/update_playback", None, [])
Document.classmap['cbox_song'] = DocSong

class DocLayer(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["name", "instrument_name", "instrument_uuid", "=enable", "=low_note", "=high_note", "=fixed_note", "=in_channel", "=out_channel", "=aftertouch", "=invert_sustain", "=consume", "=ignore_scene_transpose", "=transpose"])
    def get_instrument(self):
        return Document.map_uuid(self.status().instrument_uuid)
Document.classmap['cbox_layer'] = DocLayer

class SamplerEngine(NonDocObj):
    def __init__(self, path):
        NonDocObj.__init__(self, path, ['polyphony', 'active_voices', '%volume', '%patch', '%pan'])
    def load_patch_from_cfg(self, patch_no, cfg_section, display_name):
        return self.cmd_makeobj("/load_patch", int(patch_no), cfg_section, display_name)
    def load_patch_from_string(self, patch_no, sample_dir, sfz_data, display_name):
        return self.cmd_makeobj("/load_patch_from_string", int(patch_no), sample_dir, sfz_data, display_name)
    def load_patch_from_file(self, patch_no, sfz_name, display_name):
        return self.cmd_makeobj("/load_patch_from_file", int(patch_no), sfz_name, display_name)
    def set_patch(self, channel, patch_no):
        self.cmd("/set_patch", None, int(channel), int(patch_no))
    def get_unused_program(self):
        return self.get_things("/get_unused_program", ['program_no']).program_no
    def set_polyphony(self, polyphony):
        self.cmd("/polyphony", None, int(polyphony))
    def get_patches(self):
        return self.get_things("/patches", ['%patch']).patch
    def transform_status(self, status):
        status.patches = status.patch
        return status

class FluidsynthEngine(NonDocObj):
    def __init__(self, path):
        NonDocObj.__init__(self, path, ['polyphony', 'soundfont', '%patch'])
    def load_soundfont(self, filename):
        return self.cmd_makeobj("/load_soundfont", filename)
    def set_patch(self, channel, patch_no):
        self.cmd("/set_patch", None, int(channel), int(patch_no))
    def set_polyphony(self, polyphony):
        self.cmd("/polyphony", None, int(polyphony))
    def get_patches(self):
        return self.get_things("/patches", ['%patch']).patch
    def transform_status(self, status):
        status.patches = status.patch
        return status

class StreamPlayerEngine(NonDocObj):
    def __init__(self, path):
        NonDocObj.__init__(self, path, ['filename', 'pos', 'length', 'playing'])
    def play(self):
        self.cmd('/play')
    def stop(self):
        self.cmd('/stop')
    def seek(self, place):
        self.cmd('/seek', None, int(place))
    def load(self, filename, loop_start = -1):
        self.cmd('/load', None, filename, int(loop_start))
    def unload(self, filename, loop_start = -1):
        self.cmd('/unload')

class TonewheelOrganEngine(NonDocObj):
    def __init__(self, path):
        NonDocObj.__init__(self, path, ['=%upper_drawbar', '=%lower_drawbar', '=%pedal_drawbar', 
            '=upper_vibrato', '=lower_vibrato', '=vibrato_mode', '=vibrato_chorus', 
            '=percussion_enable', '=percussion_3rd'])
        
engine_classes = {
    'sampler' : SamplerEngine,
    'fluidsynth' : FluidsynthEngine,
    'stream_player' : StreamPlayerEngine,
    'tonewheel_organ' : TonewheelOrganEngine,
}

class DocInstrument(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["name", "outputs", "aux_offset", "engine"])
    def init_object(self):
        engine = self.status().engine
        if engine in engine_classes:
            self.engine = engine_classes[engine]("/doc/uuid/" + self.uuid + "/engine")
Document.classmap['cbox_instrument'] = DocInstrument

class DocScene(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["name", "title", "transpose", "*layer", "*instrument", '*aux'])
    def clear(self):
        self.cmd("/clear", None)
    def load(self, name):
        self.cmd("/load", None, name)
    def load_aux(self, aux):
        return self.cmd_makeobj("/load_aux", aux)
    def delete_aux(self, aux):
        return self.cmd("/delete_aux", None, aux)
    def delete_layer(self, pos):
        self.cmd("/delete_layer", None, int(1 + pos))
    def move_layer(self, old_pos, new_pos):
        self.cmd("/move_layer", None, int(old_pos + 1), int(new_pos + 1))
        
    def add_layer(self, aux, pos = None):
        if pos is None:
            return self.cmd_makeobj("/add_layer", 0, aux)
        else:
            # Note: The positions in high-level API are zero-based.
            return self.cmd_makeobj("/add_layer", int(1 + pos), aux)
    def add_instrument_layer(self, name, pos = None):
        if pos is None:
            return self.cmd_makeobj("/add_instrument_layer", 0, name)
        else:
            return self.cmd_makeobj("/add_instrument_layer", int(1 + pos), name)
    def add_new_instrument_layer(self, name, engine, pos = None):
        if pos is None:
            return self.cmd_makeobj("/add_new_instrument_layer", 0, name, engine)
        else:
            return self.cmd_makeobj("/add_new_instrument_layer", int(1 + pos), name, engine)
    def transform_status(self, status):
        status.layers = [Document.map_uuid(i) for i in status.layer]
        delattr(status, 'layer')
        status.auxes = dict([(name, Document.map_uuid(uuid)) for name, uuid in status.aux])
        delattr(status, 'aux')
        status.instruments = dict([(name, (engine, Document.map_uuid(uuid))) for name, engine, uuid in status.instrument])
        delattr(status, 'instrument')
        return status
Document.classmap['cbox_scene'] = DocScene

class DocRt(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["name", "instrument_name", "instrument_uuid", "=enable", "=low_note", "=high_note", "=fixed_note", "=in_channel", "=out_channel", "=aftertouch", "=invert_sustain", "=consume", "=ignore_scene_transpose"])
Document.classmap['cbox_rt'] = DocRt

class DocAuxBus(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["name"])
    #def transform_status(self, status):
    #    status.slot = Document.map_uuid(status.slot_uuid)
    #    return status
    def get_slot_engine(self):
        return self.cmd_makeobj("/slot/engine/get_uuid")
    def get_slot_status(self):
        return self.get_things("/slot/status", ["insert_preset", "insert_engine"])
Document.classmap['cbox_aux_bus'] = DocAuxBus

class DocModule(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, [])
Document.classmap['cbox_module'] = DocModule
    
class SamplerProgram(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, [])
    def get_regions(self):
        return map(Document.map_uuid, self.get_things("/regions", ['*region']).region)
    def get_groups(self):
        g = self.get_things("/groups", ['*group', 'default_group'])
        return [Document.map_uuid(g.default_group)] + list(map(Document.map_uuid, g.group))
    def new_group(self):
        return self.cmd_makeobj("/new_group")
Document.classmap['sampler_program'] = SamplerProgram

class SamplerLayer(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ['parent_program', 'parent_group'])
    def get_children(self):
        return map(Document.map_uuid, self.get_things("/get_children", ['*region']).region)
    def as_string(self):
        return self.get_things("/as_string", ['value']).value
    def as_string_full(self):
        return self.get_things("/as_string_full", ['value']).value
    def set_param(self, key, value):
        self.cmd("/set_param", None, key, str(value))
    def new_region(self):
        return self.cmd_makeobj("/new_region")
Document.classmap['sampler_layer'] = SamplerLayer

