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

    def __init__(self, *args):
        self.client_name = ""
        self.pos_ppqn = 0

    def __getattr__(self, *args):
        return __class__()

    def __call__(self, *args):
        return __class__()

    def __getitem__(self, key):
        return __class__()

    def serialize_event(self, *args):
        return b''

import sys
import calfbox.nullbox
sys.modules["calfbox"] = sys.modules["calfbox.nullbox"]
import calfbox

cbox = NullCalfbox("fake cbox null client")
