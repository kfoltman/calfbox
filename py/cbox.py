from _cbox import *
import struct

class GetUUID:
    def __init__(self):
        def callback(cmd, fb, args):
            if cmd == "/uuid" and len(args) == 1:
                self.uuid = args[0]
            else:
                raise ValueException, "Unexpected callback: %s" % cmd
        self.__call__ = callback
    
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
        raise ValueError, "Invalid length of an event (%d)" % len(data)

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
    def map_uuid(uuid):
        if uuid in Document.objmap:
            return Document.objmap[uuid]
        o = Document.classmap[Document.get_obj_class(uuid)](uuid)
        Document.objmap[uuid] = o
        return o

class SetterMaker():
    def __init__(self, obj, path):
        self.obj = obj
        self.path = path
    def set(self, value):
        self.obj.cmd(self.path, None, value)

class DocObj(object):
    def __init__(self, uuid, status_field_list):
        self.uuid = uuid
        self.status_fields = []
        for sf in status_field_list:
            if sf.startswith("="):
                sf = sf[1:]
                self.__dict__['set_' + sf] = SetterMaker(self, "/" + sf).set
            self.status_fields.append(sf)
        
    def cmd(self, cmd, fb, *args):
        do_cmd(Document.uuid_cmd(self.uuid, cmd), fb, list(args))
        
    def cmd_makeobj(self, cmd, *args):
        fb = GetUUID()
        do_cmd(Document.uuid_cmd(self.uuid, cmd), fb, list(args))
        return Document.map_uuid(fb.uuid)
        
    def get_things(self, cmd, fields, *args):
        return GetThings.by_uuid(self.uuid, cmd, fields, list(args))

    def status(self):
        return self.transform_status(self.get_things("/status", self.status_fields))
        
    def transform_status(self, status):
        return status

    def delete(self):
        self.cmd("/delete", None)

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
    
class DocTrack(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["*clip", "=name"])
    def add_clip(self, pos, offset, length, pattern):
        return self.cmd_makeobj("/add_clip", pos, offset, length, pattern.uuid)
    def transform_status(self, status):
        res = DocTrackStatus()
        res.name = status.name
        res.clips = [ClipItem(*c) for c in status.clip]
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
        DocObj.__init__(self, uuid, ["*track", "*pattern", 'loop_start', 'loop_end'])
    def clear(self):
        return self.cmd("/clear", None)
    def set_loop(self, ls, le):
        return self.cmd("/set_loop", None, ls, le)
    def add_track(self):
        return self.cmd_makeobj("/add_track")
    def load_drum_pattern(self, name):
        return self.cmd_makeobj("/load_pattern", name, 1)
    def load_drum_track(self, name):
        return self.cmd_makeobj("/load_track", name, 1)
    def pattern_from_blob(self, blob, length):
        return self.cmd_makeobj("/load_blob", buffer(blob), length)
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
        return res
    def update_playback(self):
        # XXXKF Maybe make it a song-level API instead of global
        do_cmd("/update_playback", None, [])
Document.classmap['cbox_song'] = DocSong

class DocLayer(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["name", "instrument_name", "instrument_uuid", "=enable", "=low_note", "=high_note", "=fixed_note", "=in_channel", "=out_channel", "=aftertouch", "=invert_sustain", "=consume", "=ignore_scene_transpose", "=transpose"])
Document.classmap['cbox_layer'] = DocLayer

#XXXKF not DOMified yet on C side
#class DocInstrument(DocObj):
#    def __init__(self, uuid):
#        DocObj.__init__(self, uuid, ["name"])
#Document.classmap['cbox_instrument'] = DocInstrument

class DocScene(DocObj):
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, ["name", "title", "transpose", "*layer", "%instrument", '*aux'])
    def clear(self):
        self.cmd("/clear", None)
    def load(self, name):
        self.cmd("/load", None, name)
    def load_aux(self, aux):
        return self.cmd_makeobj("/load_aux", aux)
    def delete_aux(self, aux):
        return self.cmd("/delete_aux", None, aux)
    def delete_layer(self, pos):
        self.cmd("/delete_layer", None, 1 + pos)
    def move_layer(self, old_pos, new_pos):
        self.cmd("/move_layer", None, old_pos + 1, new_pos + 1)
        
    def add_layer(self, aux, pos = None):
        if pos is None:
            return self.cmd_makeobj("/add_layer", 0, aux)
        else:
            # Note: The positions in high-level API are zero-based.
            return self.cmd_makeobj("/add_layer", 1 + pos, aux)
    def add_instrument_layer(self, aux, pos = None):
        if pos is None:
            return self.cmd_makeobj("/add_instrument_layer", 0, aux)
        else:
            return self.cmd_makeobj("/add_instrument_layer", 1 + pos, aux)
    def transform_status(self, status):
        status.layers = [Document.map_uuid(i) for i in status.layer]
        delattr(status, 'layer')
        status.auxes = dict([(name, Document.map_uuid(uuid)) for name, uuid in status.aux])
        delattr(status, 'aux')
        status.instruments = status.instrument
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
    def transform_status(self, status):
        status.slot = Document.map_uuid(status.slot_uuid)
        return status
    def get_slot_status(self):
        return self.get_things("/slot/status", ["insert_preset", "insert_engine"])
    
Document.classmap['cbox_aux_bus'] = DocAuxBus

