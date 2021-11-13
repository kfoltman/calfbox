#! /usr/bin/env python3
# -*- coding: utf-8 -*-

NUMBER_OF_INSTRUMENTS = 240
"""
2021-11-13 Benchmark:
NumberOfInstruments,StartInSeconds,QuitInSeconds
30, 5, 3
60, 10, 6
120, 21, 12
240, 42, 25

Conclusion: Linear time.
"""

from calfbox import cbox

import atexit
from datetime import datetime

#Capture Ctlr+C / SIGINT and let @atexit handle the rest.
import signal
import sys
def signal_handler(sig, frame):
    sys.exit(0) #atexit will trigger
signal.signal(signal.SIGINT, signal_handler)


def cmd_dumper(cmd, fb, args):
    #print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))
    pass

def stopSession():
    """This got registered with atexit in the nsm new or open callback above.
    will handle all python exceptions, but not segfaults of C modules. """
    print()
    print("Starting Quit through @atexit, stopSession")
    starttime = datetime.now()
    #Don't do that. We are just a client.
    #cbox.Transport.stop()
    #print("@atexit: Calfbox Transport stopped ")
    cbox.stop_audio()
    print("@atexit: Calfbox Audio stopped ")
    cbox.shutdown_engine()
    print("@atexit: Calfbox Engine shutdown ")
    endtime = datetime.now() - starttime
    print (f"Shutdown took {endtime.seconds} seconds for {NUMBER_OF_INSTRUMENTS} instruments")

cbox.init_engine()
cbox.start_audio(cmd_dumper)
atexit.register(stopSession) #this will handle all python exceptions, but not segfaults of C modules.

scenes = {}
jackAudioOutLefts = {}
jackAudioOutRights = {}
outputMergerRouters = {}
routerToGlobalSummingStereoMixers = {}
lmixUuid = cbox.JackIO.create_audio_output('left_mix', "#1")  #add "#1" as second parameter for auto-connection to system out 1
rmixUuid = cbox.JackIO.create_audio_output('right_mix', "#2") #add "#2" as second parameter for auto-connection to system out 2
cboxMidiPortUids = {}
sfzSamplerLayers = {}
instrumentLayers = {}\


print (f"Creating {NUMBER_OF_INSTRUMENTS} instruments")
starttime = datetime.now()

for i in range(NUMBER_OF_INSTRUMENTS):
    scenes[i] = cbox.Document.get_engine().new_scene()
    scenes[i].clear()

    #instrumentLayer = scenes[i].status().layers[0].get_instrument()
    sfzSamplerLayers[i] = scenes[i].add_new_instrument_layer(str(i), "sampler") #"sampler" is the cbox sfz engine
    scenes[i].status().layers[0].get_instrument().engine.load_patch_from_string(0, "", "", "") #fill with null instruments

    jackAudioOutLefts[i] = cbox.JackIO.create_audio_output(str(i) +"_L")
    jackAudioOutRights[i] = cbox.JackIO.create_audio_output(str(i) +"_R")

    outputMergerRouters[i] = cbox.JackIO.create_audio_output_router(jackAudioOutLefts[i], jackAudioOutRights[i])
    outputMergerRouters[i].set_gain(-3.0)
    #instrumentLayer = sfzSamplerLayers[i].get_instrument()
    instrumentLayers[i] = scenes[i].status().layers[0].get_instrument()
    instrumentLayers[i].get_output_slot(0).rec_wet.attach(outputMergerRouters[i]) #output_slot is 0 based and means a pair. Most sfz instrument have only one stereo pair. #TODO: And what if not?

    routerToGlobalSummingStereoMixers[i] = cbox.JackIO.create_audio_output_router(lmixUuid, rmixUuid)
    routerToGlobalSummingStereoMixers[i].set_gain(-3.0)
    instrument = sfzSamplerLayers[i].get_instrument()
    instrument.get_output_slot(0).rec_wet.attach(routerToGlobalSummingStereoMixers[i])

    #Create Midi Input Port
    cboxMidiPortUids[i] = cbox.JackIO.create_midi_input(str(i) + "midi_in")
    cbox.JackIO.set_appsink_for_midi_input(cboxMidiPortUids[i], True) #This sounds like a program wide sink, but it is needed for every port.
    cbox.JackIO.route_midi_input(cboxMidiPortUids[i], scenes[i].uuid) #Route midi input to the scene. Without this we have no sound, but the python processor would still work.

    #Actually load the instrument
    programNumber = 0
    #program = instrumentLayers[i].engine.load_patch_from_tar(programNumber, "bug.tar", f'Saw{1}.sfz', f'Saw{i+1}')  #tar_name, sfz_name, display_name
    program = instrumentLayers[i].engine.load_patch_from_string(programNumber, ".", "", "<group> <region> sample=*saw") #fill with null instruments
    print (f"[{i}]", program.status())
    instrumentLayers[i].engine.set_patch(1, programNumber) #1 is the channel, counting from 1. #TODO: we want this to be on all channels.

endtime = datetime.now() - starttime
print (f"Creation took {endtime.seconds} seconds for {NUMBER_OF_INSTRUMENTS} instruments")

print()
print("Press Ctrl+C for a controlled shutdown.")
print()

while True:
    cbox.call_on_idle(cmd_dumper)
