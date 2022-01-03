class NullCalfbox(str): #iterable
    """A drop-in replacement for calfboxs python module.
    Use this for testing and development.

    At the start of your program, first file, insert:
        import calfbox.nullbox

    All further
        from calfbox import cbox
    will use the null module.

    Even additional
        import calfbox
    will use the nullbox module.
    """

    def __init__(self, *args, **kwargs):
        self.client_name = ""
        self.pos_ppqn = 0
        self.ignore_program_changes = False
        self.patch = {i:(0, "nullbox") for i in range(1,17)} #catches status().patch
        self.frame_rate = 48000
        self.frame = 0

    def __getattr__(self, *args, **kwargs):
        return __class__()

    def __call__(self, *args, **kwargs):
        return __class__()

    def __getitem__(self, key):
        return __class__()

    def serialize_event(self, *args, **kwargs):
        return b''

    def get_patches(self, *args):
        """sf2 compatibility"""
        return {
            0 : "nullbox",
            }

    def set_ignore_program_changes(self, state):
        self.ignore_program_changes = state


    #Operators

    def __and__(self, *args):
        return 1

    __add__ = __sub__ = __mul__ = __floordiv__ = __div__ = __truediv__ = __mod__ = __divmod__ = __pow__ = __lshift__ = __rshift__ = __or__ = __xor__ = __ror__ = __ior__ = __rand__ = __iand__ = __rxor__ = __ixor__ = __invert__ = __and__


import sys
import calfbox.nullbox
sys.modules["calfbox"] = sys.modules["calfbox.nullbox"]
import calfbox

cbox = NullCalfbox("fake cbox null client")
