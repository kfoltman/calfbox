#! /usr/bin/env python3
# -*- coding: utf-8 -*-

import importlib

class NullCalfbox(str): #iterable
    """A drop-in replacement for calfboxs python module.
    Use this for testing and development.

    At the start of your program, first file, insert:
        import prefix.calfbox.nullbox
     or
        from SOMETHING import nullbox

    All further
        from prefix.calfbox import cbox
    will use the null module.

    Even additional
        import prefix.calfbox
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

try:
    import nullbox
except ModuleNotFoundError:
    from . import nullbox

for key, value in sys.modules.items():
    if "nullbox" in key:
        r = key
        break
else:
    raise ValueError("Nullbox Module not found")

#r is the actual name of the calfbox parent modul. We cannot assume it to be "calfbox".
calfboxModuleName = r[:-len(".nullbox")] #remove suffix
sys.modules[calfboxModuleName] = sys.modules[r] #e.g. sys.modules["calfbox"] is now nullbox

#Hack 'from prefix.calfbox import cbox'
importlib.import_module(calfboxModuleName) #Imported once here, all modules will import this variant later.
#import calfbox
cbox = NullCalfbox("fake cbox null client")

#Hack direct call 'import cbox'
sys.modules["cbox"] = cbox
import cbox #Imported once here, all modules will import this variant later.
