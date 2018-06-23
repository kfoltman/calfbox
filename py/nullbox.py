class NullCalfbox(str): #iterable
    """A drop-in replacement for calfboxs python module.
    Replace your
        from calfbox import cbox
    with
        from calfbox.nullbox import cbox"""

    def __init__(self, *args):
        self.client_name = ""
        self.pos_ppqn = 0

    def __getattr__(self, *args):
        return __class__()

    def __call__(self, *args):
        return __class__()

    def serialize_event(self, *args):
        return b''

cbox = NullCalfbox("fake cbox null client")
