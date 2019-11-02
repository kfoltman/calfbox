from ctypes import *
import numbers
import os

def cbox_uuid_to_str(uuid_ptr):
    uuid_str = create_string_buffer(40)
    cb.cbox_uuid_tostring(uuid_ptr, uuid_str)
    return uuid_str.value.decode()

c_int32_p = POINTER(c_int32)
c_double_p = POINTER(c_double)
GQuark = c_int32

class GError(Structure):
    _fields_ = [
        ( 'domain', GQuark ),
        ( 'code', c_int ),
        ( 'message', c_char_p ),
    ]
GErrorPtr = POINTER(GError)
GErrorPtrPtr = POINTER(GErrorPtr)

class OscCommand(Structure):
    _fields_ = [
        ( 'command', c_char_p ),
        ( 'arg_types', c_char_p ),
        ( 'arg_values', POINTER(c_void_p) ),
    ]

class CboxBlob(Structure):
    _fields_ = [
        ( 'data', c_void_p ),
        ( 'size', c_ulong ),
    ]
    def bytes(self):
        return string_at(self.data, self.size)
    def __repr__(self):
        return repr(self.bytes())
CboxBlobPtr = POINTER(CboxBlob)

class CboxObjHdr(Structure):
    _fields_ = [
        ( 'class_ptr', c_void_p ),
        ( 'owner', c_void_p ),
        ( 'link_in_document', c_void_p ),
        ( 'cbox_uuid', c_ubyte * 16 ),
        ( 'stamp', c_uint64 ),
    ]

PROCESS_CMD_FUNC = CFUNCTYPE(c_bool, c_void_p, c_void_p, POINTER(OscCommand), GErrorPtrPtr)

class CmdTarget(Structure):
    _fields_ = [
        ( 'user_data', c_void_p ),
        ( 'process_cmd', PROCESS_CMD_FUNC ),
    ]
CmdTargetPtr = POINTER(CmdTarget)

class PyCmdTarget(CmdTarget):
    def __init__(self):
        def process_cmd_func(cmd_target, fb_target, command, error_ptr_ptr):
            cmd_target = CmdTargetPtr.from_address(cmd_target)
            fb_target = CmdTargetPtr.from_address(fb_target) if fb_target else None
            cmd = command.contents
            argtypes = cmd.arg_types.decode()
            data = cmd.arg_values
            args = [None] * len(argtypes)
            for i in range(len(argtypes)):
                argtype = argtypes[i]
                if argtype == 's':
                    args[i] = cast(data[i], c_char_p).value.decode()
                elif argtype == 'i':
                    args[i] = cast(data[i], c_int32_p).contents.value
                elif argtype == 'f':
                    args[i] = cast(data[i], c_double_p).contents.value
                elif argtype == 'u':
                    args[i] = cbox_uuid_to_str(data[i])
                elif argtype == 'b':
                    args[i] = cast(data[i], CboxBlobPtr).contents
                elif argtype == 'o':
                    args[i] = cbox_uuid_to_str(cast(data[i], POINTER(CboxObjHdr)).contents.cbox_uuid)
                #elif argtype == 'N':
                #    args[i] = None
            try:
                self.process(cmd.command.decode(), args)
                return True
            except Exception as e:
                cb.g_error_set(1, 1, str(e))
                return False
        self.process_cmd = PROCESS_CMD_FUNC(process_cmd_func)
        self.user_data = 0
    def process(self, cmd, args):
        print ("%s(%s)" % (cmd, repr(args)))

def find_calfbox():
    cb = cdll.LoadLibrary("")
    if not hasattr(cb, 'cbox_embed_get_cmd_root'):
        cb = None
        fn = os.getenv("CALFBOX_BINARY", None)
        if fn is not None:
            cb = cdll.LoadLibrary(fn)
        else:
            for pdir in os.getenv('PATH').split(os.pathsep):
                fn = os.path.join(pdir, 'calfbox')
                if os.path.exists(fn):
                    cb = cdll.LoadLibrary(fn)
                    break
        if not cb:
            raise IOError("Calfbox executable not found in PATH")
    return cb

cb = find_calfbox()
cb.cbox_embed_get_cmd_root.restype = CmdTargetPtr

class CalfboxException(Exception):
    pass

def convert_exception(cls, gptr):
    msg = gptr.contents.message
    cb.g_error_free(gptr)
    raise cls(msg.decode())
class WrapCmdTarget(PyCmdTarget):
    def __init__(self, fb):
        PyCmdTarget.__init__(self)
        self.fb = fb
    def process(self, cmd, args):
        self.fb(cmd, None, args)
    
def init_engine(config=""):
    gptr = GErrorPtr()
    if not cb.cbox_embed_init_engine(config, byref(gptr)):
        convert_exception(CalfboxException, gptr)
def start_audio(cmd_dumper=None):
    gptr = GErrorPtr()
    target = PyCmdTarget()
    # XXXKF pass the callback
    if not cb.cbox_embed_start_audio(byref(target), byref(gptr)):
        convert_exception(CalfboxException, gptr)
def stop_audio():
    gptr = GErrorPtr()
    if not cb.cbox_embed_stop_audio(byref(gptr)):
        convert_exception(CalfboxException, gptr)
def shutdown_engine():
    gptr = GErrorPtr()
    if not cb.cbox_embed_shutdown_engine(byref(gptr)):
        convert_exception(CalfboxException, gptr)
def do_cmd_on(target, cmd, fb, args):
    gptr = GErrorPtr()
    ocmd = OscCommand()
    ocmd.command = cmd.encode()
    acnt = len(args)
    arg_types = create_string_buffer(acnt)
    arg_values = (c_void_p * acnt)()
    arg_space = (c_uint8 * (8 * acnt))()
    # Scratch space for string encoding
    tmp = []
    for i in range(len(args)):
        a = args[i]
        t = type(a)
        if isinstance(a, numbers.Number):
            if t is float:
                arg_types[i] = b'f'
                arg_values[i] = addressof(arg_space) + 8 * i
                cast(arg_values[i], c_double_p)[i] = c_double(a)
            else:
                arg_types[i] = b'i'
                arg_values[i] = addressof(arg_space) + 8 * i
                cast(arg_values[i], c_int32_p)[i] = c_int32(a)
        elif t == str:
            tmp.append(create_string_buffer(a.encode()))
            arg_types[i] = b's'
            arg_values[i] = addressof(tmp[-1])
        elif t == bytearray:
            tmp.append(CboxBlob(cast((c_byte * len(a)).from_buffer(a), c_void_p), len(a)))
            arg_types[i] = b'b'
            arg_values[i] = addressof(tmp[-1])
        else:
            arg_types[i] = b'N'
    ocmd.arg_types = cast(arg_types, c_char_p)
    ocmd.arg_values = arg_values
    if fb is not None:
        res = target.contents.process_cmd(target, byref(WrapCmdTarget(fb)), ocmd, gptr)
    else:
        res = target.contents.process_cmd(target, None, ocmd, gptr)
    if not res:
        if gptr.contents:
            raise Exception(gptr.contents.message)
        else:
            raise Exception("Unknown error")
        
def do_cmd(cmd, fb, args):
    do_cmd_on(cb.cbox_embed_get_cmd_root(), cmd, fb, args)
