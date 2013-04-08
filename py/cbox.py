from _cbox import *
import struct
import traceback

type_wrapper_debug = False

###############################################################################
# Ugly internals. Please skip this section for your own sanity.
###############################################################################

class GetUUID:
    """An object that calls a C layer command, receives a /uuid callback from it
    and stores the passed UUID in its uuid attribute.
    
    Example use: GetUUID('/command', arg1, arg2...).uuid
    """
    def __init__(self, cmd, *cmd_args):
        def callback(cmd, fb, args):
            if cmd == "/uuid" and len(args) == 1:
                self.uuid = args[0]
            else:
                raise ValueException("Unexpected callback: %s" % cmd)
        self.callback = callback
        self.uuid = None
        do_cmd(cmd, self, list(cmd_args))
    def __call__(self, *args):
        self.callback(*args)
    
class GetThings:
    """A generic callback object that receives various forms of information from
    C layer and converts then into object's Python attributes.
    
    This is an obsolete interface, to be replaced by GetUUID or metaclass
    based type-safe autoconverter. However, there are still some cases that
    aren't (yet) handled by either.
    """
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

class PropertyDecorator(object):
    """Abstract property decorator."""
    def __init__(self, base):
        self.base = base
    def get_base(self):
        return self.base
    def map_cmd(self, cmd):
        return cmd

class AltPropName(PropertyDecorator):
    """Command-name-changing property decorator. Binds a property to the
    specified /path, different from the default one, which based on property name,
    with -s and -es suffix removed for lists and dicts."""
    def __init__(self, alt_name, base):
        PropertyDecorator.__init__(self, base)
        self.alt_name = alt_name
    def map_cmd(self, cmd):
        return self.alt_name
    def execute(self, property, proptype, klass):
        pass

class SettableProperty(PropertyDecorator):
    """Decorator that creates a setter method for the property."""
    def execute(self, property, proptype, klass):
        if type(proptype) is dict:
            setattr(klass, 'set_' + property, lambda self, key, value: self.cmd('/' + property, None, key, proptype(value)))
        elif type(proptype) is bool:
            setattr(klass, 'set_' + property, lambda self, value: self.cmd('/' + property, None, 1 if value else 0))
        else:
            setattr(klass, 'set_' + property, lambda self, value: self.cmd('/' + property, None, proptype(value)))        

def new_get_things(obj, cmd, settermap, args):
    """Call C command with arguments 'args', populating a return object obj
    using settermap to interpret callback commands and initialise the return
    object."""
    def update_callback(cmd2, fb, args2):
        try:
            if cmd2 in settermap:
                settermap[cmd2](obj, args2)
        except Exception as error:
            traceback.print_exc()
            raise
    # Set initial values for the properties (None or empty dict/list)
    for setterobj in settermap.values():
        setattr(obj, setterobj.property, setterobj.init_value())
    # Call command and apply callback commands via setters to the object
    do_cmd(cmd, update_callback, args)
    strmethod = lambda self: (str(self.__class__) + ":" + " ".join(["%s=%s" % (v.property, repr(getattr(self, v.property))) for v in settermap.values()]))
    obj.__str__ = strmethod.__get__(obj, obj.__class__)
    return obj

def _convert_single(t, value):
    """Convert a single value to a specified type (basic type or wrapper class)"""
    if type(t) is CboxObjMetaclass:
        if type(value) is list:
            assert len(value) == 1, "Value is a list: %s, not single item of type %s" % (value, t)
            value = Document.map_uuid(value[0])
        else:
            value = Document.map_uuid(value)
        assert isinstance(value, t), "Value is %s, not %s" % (type(value), t)
        return value
    if type(value) is list:
        return t(*value)
    return t(value)

def _convert_tuple(conversions, value):
    """Convert a tuple of values to a specified type."""
    assert len(conversions) == len(value), "Conversion list is %s, value list is %s" % (repr(conversions), repr(value))
    return tuple([_convert_single(conversions[i], value[i]) for i in range(len(conversions))])

class SetterWithConversion:
    """A setter object class that sets a specific property to a typed value or a tuple of typed value."""
    def __init__(self, property, conversions):
        self.property = property
        self.conversions = conversions
    def init_value(self):
        return None
    def __call__(self, obj, args):
        # print ("Setting attr %s on object %s" % (self.property, obj))
        setattr(obj, self.property, _convert_tuple(self.conversions, args)[0])

class ListAdderWithConversion:
    """A setter object class that adds a tuple filled with type-converted arguments of the
    callback to a list. E.g. ListAdderWithConversion('foo', (int, int))(obj, [1,2])
    adds a tuple: (int(1), int(2)) to the list obj.foo"""

    def __init__(self, property, types):
        self.property = property
        self.types = types
    def init_value(self):
        return []
    def __call__(self, obj, args):
        if type(self.types) is tuple:
            getattr(obj, self.property).append(_convert_tuple(self.types, args))
        else:
            getattr(obj, self.property).append(_convert_single(self.types, args))

class DictAdderWithConversion:
    """A setter object class that adds a tuple filled with type-converted
    arguments of the callback to a dictionary under a key passed as first argument
    i.e. DictAdderWithConversion('foo', str, (int, int))(obj, ['bar',1,2]) adds
    a tuple: (int(1), int(2)) under key 'bar' to obj.foo"""

    def __init__(self, property, keytype, types):
        self.property = property
        self.keytype = keytype
        self.types = types
    def init_value(self):
        return {}
    def __call__(self, obj, args):
        if type(self.types) is tuple:
            getattr(obj, self.property)[_convert_single(self.keytype, args[0])] = _convert_tuple(self.types, args[1:])
        else:
            getattr(obj, self.property)[_convert_single(self.keytype, args[0])] = _convert_single(self.types, args[1])

class CboxObjMetaclass(type):
    """Metaclass that creates Python wrapper classes for various C-side objects.
    This class is responsible for automatically marshalling and type-checking/converting
    fields of Status inner class on status() calls."""
    def __new__(cls, name, bases, namespace, **kwds):
        status_class = namespace['Status']
        status_fields = []
        all_decorators = {}
        prop_types = {}
        settermap = {}
        if type_wrapper_debug:
            print ("Wrapping type: %s" % name)
            print ("-----")
        for prop in dir(status_class):
            if prop.startswith("__"):
                continue
            value = getattr(status_class, prop)
            decorators = []
            propcmd = '/' + prop
            if type(value) is list or type(value) is dict:
                if propcmd.endswith('s'):
                    if propcmd.endswith('es'):
                        propcmd = propcmd[:-2]
                    else:
                        propcmd = propcmd[:-1]
            while isinstance(value, PropertyDecorator):
                decorators.append(value)
                propcmd = value.map_cmd(propcmd)
                value = value.get_base()
            if type(value) in [type, CboxObjMetaclass]:
                if type_wrapper_debug:
                    print ("%s is type %s" % (prop, repr(value)))
                status_fields.append(prop)
                settermap[propcmd] = SetterWithConversion(prop, (value, ))
            elif type(value) is dict:
                assert(len(value) == 1)
                value = list(value.items())[0]
                if type_wrapper_debug:
                    print ("%s is type: %s -> %s" % (prop, repr(value[0]), repr(value[1])))
                settermap[propcmd] = DictAdderWithConversion(prop, value[0], value[1])
            elif type(value) is list:
                assert(len(value) == 1)
                if type_wrapper_debug:
                    print ("%s is array of %s" % (prop, repr(value)))
                settermap[propcmd] = ListAdderWithConversion(prop, value[0])
            elif type(value) is tuple:
                if type_wrapper_debug:
                    print ("%s is a tuple: %s" % (prop, repr(value)))
                settermap[propcmd] = SetterWithConversion(prop, value)
            else:
                raise ValueError("Don't know what to do with %s property '%s' of type %s" % (name, prop, repr(value)))
            all_decorators[prop] = decorators
            prop_types[prop] = value
        result = type.__new__(cls, name, bases, namespace, **kwds)
        result.status_field_list = status_fields
        result.settermap = settermap
        for propname, decorators in all_decorators.items():
            for decorator in decorators:
                decorator.execute(propname, prop_types[propname], result)
        result.status = lambda self: new_get_things(self.Status(), self.path + '/status', self.settermap, [])

        if type_wrapper_debug:
            print ("")
        return result

class NonDocObj(object, metaclass = CboxObjMetaclass):
    """Root class for all wrapper classes that wrap objects that don't have 
    their own identity/UUID.
    This covers various singletons and inner objects (e.g. engine in instrument)."""
    class Status:
        pass
    def __init__(self, path, status_field_list = None):
        if status_field_list is None:
            status_field_list = self.status_field_list
        self.path = path
        self.status_fields = []
        for sf in status_field_list:
            self.status_fields.append(sf)

    def cmd(self, cmd, fb = None, *args):
        do_cmd(self.path + cmd, fb, list(args))

    def cmd_makeobj(self, cmd, *args):
        return Document.map_uuid(GetUUID(self.path + cmd, *args).uuid)

    def get_things(self, cmd, fields, *args):
        return GetThings(self.path + cmd, fields, list(args))

    def make_path(self, path):
        return self.path + path

    #def status(self):
    #    return self.transform_status(self.get_things("/status", self.status_fields))
        
    def transform_status(self, status):
        return status

class DocObj(NonDocObj):
    """Root class for all wrapper classes that wrap first-class document objects."""
    class Status:
        pass
    def __init__(self, uuid, status_field_list = None):
        NonDocObj.__init__(self, Document.uuid_cmd(uuid, ''), status_field_list)
        self.uuid = uuid

    def delete(self):
        self.cmd("/delete")

class VarPath:
    def __init__(self, path, args = []):
        self.path = path
        self.args = args
    def plus(self, subpath, *args):
        return VarPath(self.path if subpath is None else self.path + "/" + subpath, self.args + list(args))
    def set(self, *values):
        do_cmd(self.path, None, self.args + list(values))

###############################################################################
# And those are the proper user-accessible objects.
###############################################################################

class Config:
    """INI file manipulation class."""
    @staticmethod
    def sections(prefix = ""):
        """Return a list of configuration sections."""
        return [CfgSection(name) for name in GetThings('/config/sections', ['*section'], [str(prefix)]).section]

    @staticmethod
    def keys(section, prefix = ""):
        """Return a list of configuration keys in a section, with optional prefix filtering."""
        return GetThings('/config/keys', ['*key'], [str(section), str(prefix)]).key

    @staticmethod
    def get(section, key):
        """Return a string value of a given key."""
        return GetThings('/config/get', ['value'], [str(section), str(key)]).value

    @staticmethod
    def set(section, key, value):
        """Set a string value for a given key."""
        do_cmd('/config/set', None, [str(section), str(key), str(value)])

    @staticmethod
    def delete(section, key):
        """Delete a given key."""
        do_cmd('/config/delete', None, [str(section), str(key)])

    @staticmethod
    def save(filename = None):
        """Save config, either into current INI file or some other file."""
        if filename is None:
            do_cmd('/config/save', None, [])
        else:
            do_cmd('/config/save', None, [str(filename)])
            
    @staticmethod
    def add_section(section, content):
        """Populate a config section based on a string with key=value lists.
        This is a toy/debug function, it doesn't handle any edge cases."""
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
    @staticmethod
    def ppqn_to_samples(pos_ppqn):
        return GetThings("/master/ppqn_to_samples", ['value'], [pos_ppqn]).value
    @staticmethod
    def samples_to_ppqn(pos_samples):
        return GetThings("/master/samples_to_ppqn", ['value'], [pos_samples]).value

# Currently responsible for both JACK and USB I/O - not all functionality is
# supported by both.
class JackIO:
    AUDIO_TYPE = "32 bit float mono audio"
    MIDI_TYPE = "8 bit raw midi"
    PORT_IS_SINK = 0x1
    PORT_IS_SOURCE = 0x2
    PORT_IS_PHYSICAL = 0x4
    PORT_CAN_MONITOR = 0x8
    PORT_IS_TERMINAL = 0x10
    @staticmethod
    def status():
        # Some of these only make sense for 
        return GetThings("/io/status", ['client_type', 'client_name', 'audio_inputs', 'audio_outputs', 'buffer_size', '*midi_output', '*midi_input', 'sample_rate', 'output_resolution'], [])
    @staticmethod
    def create_midi_input(name, autoconnect_spec = None):
        uuid = GetUUID("/io/create_midi_input", name).uuid
        if autoconnect_spec is not None and autoconnect_spec != '':
            JackIO.autoconnect(uuid, autoconnect_spec)
        return uuid
    @staticmethod
    def create_midi_output(name, autoconnect_spec = None):
        uuid = GetUUID("/io/create_midi_output", name).uuid
        if autoconnect_spec is not None and autoconnect_spec != '':
            JackIO.autoconnect(uuid, autoconnect_spec)
        return uuid
    @staticmethod
    def autoconnect_midi_output(uuid, autoconnect_spec = None):
        if autoconnect_spec is not None:
            do_cmd("/io/autoconnect", None, [uuid, autoconnect_spec])
        else:
            do_cmd("/io/autoconnect", None, [uuid, ''])
    autoconnect_midi_input = autoconnect_midi_output
    @staticmethod
    def rename_midi_output(uuid, new_name):
        do_cmd("/io/rename_midi_port", None, [uuid, new_name])
    rename_midi_input = rename_midi_output
    @staticmethod
    def disconnect_midi_output(uuid):
        do_cmd("/io/disconnect_midi_port", None, [uuid])
    disconnect_midi_input = disconnect_midi_output
    @staticmethod
    def disconnect_midi_output(uuid):
        do_cmd("/io/disconnect_midi_output", None, [uuid])
    @staticmethod
    def delete_midi_input(uuid):
        do_cmd("/io/delete_midi_input", None, [uuid])
    @staticmethod
    def delete_midi_output(uuid):
        do_cmd("/io/delete_midi_output", None, [uuid])
    @staticmethod
    def port_connect(pfrom, pto):
        do_cmd("/io/port_connect", None, [pfrom, pto])
    @staticmethod
    def port_disconnect(pfrom, pto):
        do_cmd("/io/port_disconnect", None, [pfrom, pto])
    @staticmethod
    def get_ports(name_mask = ".*", type_mask = ".*", flag_mask = 0):
        return GetThings("/io/get_ports", ['*port'], [name_mask, type_mask, int(flag_mask)]).port

def call_on_idle(callback = None):
    do_cmd("/on_idle", callback, [])
        
def get_new_events():
    return GetThings("/on_idle", ['seq'], []).seq
    
def send_midi_event(*data, output = None):
    do_cmd('/send_event_to', None, [output if output is not None else ''] + list(data))
        
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
    """Document singleton."""
    classmap = {}
    objmap = {}
    @staticmethod
    def dump():
        """Print all objects in the documents to stdout. Only used for debugging."""
        do_cmd("/doc/dump", None, [])
    @staticmethod
    def uuid_cmd(uuid, cmd):
        """Internal: execute a given request on an object with specific UUID."""
        return "/doc/uuid/%s%s" % (uuid, cmd)
    @staticmethod
    def get_uuid(path):
        """Internal: retrieve an UUID of an object that has specified path."""
        return GetUUID('%s/get_uuid' % path).uuid
    @staticmethod
    def get_obj_class(uuid):
        """Internal: retrieve an internal class type of an object that has specified path."""
        return GetThings(Document.uuid_cmd(uuid, "/get_class_name"), ["class_name"], []).class_name
    @staticmethod
    def get_song():
        """Retrieve the current song object of a given document. Each document can
        only have one current song."""
        return Document.map_uuid(Document.get_uuid("/song"))
    @staticmethod
    def get_scene():
        """Retrieve the current scene object of a given document. Each document can
        only have one current scene."""
        return Document.map_uuid(Document.get_uuid("/scene"))
    @staticmethod
    def get_rt():
        """Retrieve the RT singleton. RT is an object used to communicate between
        realtime and user thread, and is currently also used to access the audio
        engine."""
        return Document.map_uuid(Document.get_uuid("/rt"))
    @staticmethod
    def new_scene(srate, bufsize):
        """Create a new scene object. This new scene object cannot be used for
        audio playback - that's only allowed for main document scene."""
        return Document.map_uuid(GetUUID('/new_scene', int(srate), int(bufsize)).uuid)
    @staticmethod
    def map_uuid(uuid):
        """Create or retrieve a Python-side accessor proxy for a C-side object."""
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

class DocPattern(DocObj):
    class Status:
        event_count = int
        loop_end = int
        name = str
    def __init__(self, uuid):
        DocObj.__init__(self, uuid)
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
    class Status:
        pos = int
        offset = int
        length = int
        pattern = DocPattern
    def __init__(self, uuid):
        DocObj.__init__(self, uuid)
    def transform_status(self, status):
        return ClipItem(status.pos, status.offset, status.length, status.pattern, status.uuid)
Document.classmap['cbox_track_item'] = DocTrackClip
        
class DocTrackStatus:
    name = None
    clips = None
    external_output = None
    
class DocTrack(DocObj):
    class Status:
        clips = [ClipItem]
        name = SettableProperty(str)
        external_output = SettableProperty(int)
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

class MtiItem:
    def __init__(self, pos, tempo, timesig_nom, timesig_denom):
        self.pos = pos
        self.tempo = tempo
        self.timesig_nom = timesig_nom
        self.timesig_denom = timesig_denom
    def __eq__(self, o):
        return self.pos == o.pos and self.tempo == o.tempo and self.timesig_nom == o.timesig_nom and self.timesig_denom == o.timesig_denom

class DocSongStatus:
    tracks = None
    patterns = None

class DocSong(DocObj):
    class Status:
        tracks = [TrackItem]
        patterns = [PatternItem]
        mtis = [MtiItem]
        loop_start = int
        loop_end = int
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

class DocInstrument(DocObj):
    class Status:
        name = str
        outputs = int
        aux_offset = int
        engine = str
    def init_object(self):
        engine = self.status().engine
        if engine in engine_classes:
            self.engine = engine_classes[engine]("/doc/uuid/" + self.uuid + "/engine")
Document.classmap['cbox_instrument'] = DocInstrument

class DocLayer(DocObj):
    class Status:
        name = str
        instrument_name = str
        instrument = AltPropName('/instrument_uuid', DocInstrument)
        enable = SettableProperty(bool)
        low_note = SettableProperty(int)
        high_note = SettableProperty(int)
        fixed_note = SettableProperty(int)
        in_channel = SettableProperty(int)
        out_channel = SettableProperty(int)
        aftertouch = SettableProperty(bool)
        invert_sustain = SettableProperty(bool)
        consume = SettableProperty(bool)
        ignore_scene_transpose = SettableProperty(bool)
        ignore_program_changes = SettableProperty(bool)
        transpose = SettableProperty(int)
    def get_instrument(self):
        return self.status().instrument
Document.classmap['cbox_layer'] = DocLayer

class SamplerEngine(NonDocObj):
    class Status(object):
        """Maximum number of voices playing at the same time."""
        polyphony = int
        """Current number of voices playing."""
        active_voices = int
        """GM volume (14-bit) per MIDI channel."""
        volume = {int:int}
        """GM pan (14-bit) per MIDI channel."""
        pan = {int:int}
        """Current number of voices playing per MIDI channel."""
        channel_voices = {int:int}
        """MIDI channel -> (program number, program name)"""
        patches = {int:(int, str)}

    def load_patch_from_cfg(self, patch_no, cfg_section, display_name):
        """Load a sampler program from an 'spgm:' config section."""
        return self.cmd_makeobj("/load_patch", int(patch_no), cfg_section, display_name)
        
    def load_patch_from_string(self, patch_no, sample_dir, sfz_data, display_name):
        """Load a sampler program from a string, using given filesystem path for sample directory."""
        return self.cmd_makeobj("/load_patch_from_string", int(patch_no), sample_dir, sfz_data, display_name)
        
    def load_patch_from_file(self, patch_no, sfz_name, display_name):
        """Load a sampler program from a filesystem file."""
        return self.cmd_makeobj("/load_patch_from_file", int(patch_no), sfz_name, display_name)
        
    def set_patch(self, channel, patch_no):
        """Select patch identified by patch_no in a specified MIDI channel."""
        self.cmd("/set_patch", None, int(channel), int(patch_no))
    def get_unused_program(self):
        """Returns first program number that has no program associated with it."""
        return self.get_things("/get_unused_program", ['program_no']).program_no
    def set_polyphony(self, polyphony):
        """Set a maximum number of voices that can be played at a given time."""
        self.cmd("/polyphony", None, int(polyphony))
    def get_patches(self):
        """Return a map of program identifiers to program objects."""
        return self.get_things("/patches", ['%patch']).patch
    def transform_status(self, status):
        status.patches = status.patch
        return status

class FluidsynthEngine(NonDocObj):
    class Status:
        polyphony = int
        soundfont = str
        patch = {int: int}
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
    class Status:
        filename = str
        pos = int
        length = int
        playing = int
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
    class Status:
        upper_drawbar = SettableProperty({int: int})
        lower_drawbar = SettableProperty({int: int})
        pedal_drawbar = SettableProperty({int: int})
        upper_vibrato = SettableProperty(bool)
        lower_vibrato = SettableProperty(bool)
        vibrato_mode = SettableProperty(int)
        vibrato_chorus = SettableProperty(int)
        percussion_enable = SettableProperty(bool)
        percussion_3rd = SettableProperty(bool)
        
engine_classes = {
    'sampler' : SamplerEngine,
    'fluidsynth' : FluidsynthEngine,
    'stream_player' : StreamPlayerEngine,
    'tonewheel_organ' : TonewheelOrganEngine,
}

class DocAuxBus(DocObj):
    class Status:
        name = str
    #def transform_status(self, status):
    #    status.slot = Document.map_uuid(status.slot_uuid)
    #    return status
    def get_slot_engine(self):
        return self.cmd_makeobj("/slot/engine/get_uuid")
    def get_slot_status(self):
        return self.get_things("/slot/status", ["insert_preset", "insert_engine"])
Document.classmap['cbox_aux_bus'] = DocAuxBus

class DocScene(DocObj):
    class Status:
        name = str
        title = str
        transpose = int
        layers = [DocLayer]
        instruments = {str: (str, DocInstrument)}
        auxes = {str: (str, str, DocAuxBus)}
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
Document.classmap['cbox_scene'] = DocScene

class DocRt(DocObj):
    class Status:
        audio_channels = (int, int)
        state = (int, str)
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, [])
Document.classmap['cbox_rt'] = DocRt

class DocModule(DocObj):
    class Status:
        pass
    def __init__(self, uuid):
        DocObj.__init__(self, uuid, [])
Document.classmap['cbox_module'] = DocModule
    
class SamplerProgram(DocObj):
    class Status:
        name = str
        sample_dir = str
        program_no = int
        in_use = int
    def get_regions(self):
        return map(Document.map_uuid, self.get_things("/regions", ['*region']).region)
    def get_groups(self):
        g = self.get_things("/groups", ['*group', 'default_group'])
        return [Document.map_uuid(g.default_group)] + list(map(Document.map_uuid, g.group))
    def get_control_inits(self):
        return self.get_things("/control_inits", ['*control_init']).control_init
    def new_group(self):
        return self.cmd_makeobj("/new_group")
    def add_control_init(self, controller, value):
        return self.cmd("/add_control_init", None, controller, value)
    # which = -1 -> remove all controllers with that number from the list
    def delete_control_init(self, controller, which = 0):
        return self.cmd("/delete_control_init", None, controller, which)
Document.classmap['sampler_program'] = SamplerProgram

class SamplerLayer(DocObj):
    class Status:
        parent_program = SamplerProgram
        parent_group = DocObj
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

