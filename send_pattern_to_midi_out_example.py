#! /usr/bin/env python3
# -*- coding: utf-8 -*-

from calfbox import cbox

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine()
cbox.start_audio(cmd_dumper)

outportname = "CboxSendPattern"
cboxMidiOutUuid = cbox.JackIO.create_midi_output(outportname) #Add a named midi out port
cbox.JackIO.rename_midi_output(cboxMidiOutUuid, outportname) #For good measure.
outputScene = cbox.Document.get_engine().new_scene() #Create a new scene that will play the pattern. The pattern is not saved in the scene, it is not a track or so.
outputScene.clear() #For good measure.
outputScene.add_new_midi_layer(cboxMidiOutUuid) #Connect the scene to our midi output port. Without this there will be no midi out.


# Send 8 pitches 0x90 with velocity 0
# Create a binary blob that contains the MIDI events
pblob = bytes()
for pitch in range(0,8):
    # note on
    pblob += cbox.Pattern.serialize_event(1, 0x90, pitch, 0) #tick in pattern, midi, pitch, velocity
# Create a new pattern object using events from the blob
allNoteOnZeroPattern = cbox.Document.get_song().pattern_from_blob(pblob, 0) #0 ticks.


print ("\nThis example sends midi events from a pattern without any tracks. Rolling transport or not doesn't matter.")
print("Ready!")
counter = 0 #To add delay
while True:
    cbox.call_on_idle(cmd_dumper)
    if counter > 10**5 * 4 :
        print ("Send pattern")
        outputScene.play_pattern(allNoteOnZeroPattern, 150.0) #150 tempo
        counter = 0
    counter += 1
