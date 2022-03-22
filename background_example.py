#! /usr/bin/env python3
# -*- coding: utf-8 -*-

from py import cbox
def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))
cbox.init_engine("") #empty string so cbox doesn't look for the .cboxrc file
cbox.start_audio(cmd_dumper)

import time
while True:
    cbox.do_cmd("/on_idle", None, [])
    time.sleep(0.1)
