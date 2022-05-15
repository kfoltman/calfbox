#! /usr/bin/env python3
# -*- coding: utf-8 -*-
"""
This is a minimal calfbox python example. It is meant as a starting
point to find bugs and test performance.

Copyright, Nils Hilbricht, Germany ( https://www.hilbricht.net )

This code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

import atexit
from pprint import pprint
from calfbox import cbox


cbox.init_engine("")
cbox.Config.set("io", "outputs", 8)
NAME = "Cbox Interactive"
cbox.Config.set("io", "client_name", NAME)

cbox.start_audio()
scene = cbox.Document.get_engine().new_scene()
scene.clear()


print("Setting a pretty name directly on our client")
cbox.JackIO.Metadata.client_set_property("http://jackaudio.org/metadata/pretty-name", NAME+" Pretty Client Name")

print("Setting nonsense meta data to our first two ports and midi port")
cbox.JackIO.Metadata.set_property("Cbox Interactive:out_1", "foo", "bar")
cbox.JackIO.Metadata.set_property("Cbox Interactive:out_1", "faz", "baz")
cbox.JackIO.Metadata.set_property("Cbox Interactive:out_2", "rolf", "hello")
cbox.JackIO.Metadata.set_property("Cbox Interactive:out_2", "rolf", "hello")
cbox.JackIO.Metadata.set_property("Cbox Interactive:midi", "wolf", "world", "stryng")
cbox.JackIO.Metadata.set_property("Cbox Interactive:midi", "asd", "qwe", "")


print ("Setting port order for all 8 ports")

portOrderDict = {
    "Cbox Interactive:out_1": 50,
    "Cbox Interactive:out_2": 40,
    "Cbox Interactive:out_3": 3,
    "Cbox Interactive:out_4": 5,
    "Cbox Interactive:out_5": 7,
    "Cbox Interactive:out_6": 999,
    "Cbox Interactive:out_7": 4,
    "Cbox Interactive:out_8": 4,
    }

try:
    cbox.JackIO.Metadata.set_all_port_order(portOrderDict)
    print ("Test to catch non-unique indices failed!. Quitting")
    quit()
except ValueError as e:
    print ("Caught expected ValueError for double index entry.\nAdjusting value and try again to set all ports.")

portOrderDict["Cbox Interactive:out_8"] = 0
cbox.JackIO.Metadata.set_all_port_order(portOrderDict)

print("List of all metadata follows")
pprint (cbox.JackIO.Metadata.get_all_properties())

print()
print ("Now check your port order in QJackCtl or similar. Press [Return] to quit")
input() #wait for key to confirm order visually in qjackctl

print("Removing the pretty name from our client")
cbox.JackIO.Metadata.client_remove_property("http://jackaudio.org/metadata/pretty-name")

print("Second time. This will fail: Removing the pretty name from our client")
try:
    cbox.JackIO.Metadata.client_remove_property("http://jackaudio.org/metadata/pretty-name")
except Exception as e:
    print ("Caught expected error:", e)

quit()

def exit_handler():
    #Restore initial state and stop the engine
    cbox.Transport.stop()
    cbox.stop_audio()
    cbox.shutdown_engine()
atexit.register(exit_handler)
