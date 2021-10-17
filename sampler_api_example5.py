from calfbox import cbox
from pprint import pprint

def cmd_dumper(cmd, fb, args):
    print ("%s(%s)" % (cmd, ",".join(list(map(repr,args)))))

cbox.init_engine()
cbox.start_audio(cmd_dumper)

global Document
Document = cbox.Document

scene = Document.get_scene()
scene.clear()
instrument = scene.add_new_instrument_layer("test_sampler", "sampler").get_instrument()
pgm_no = instrument.engine.get_unused_program()


sfzString = """
<region> region_label=Region-2a //This is put into implicitMaster0 and implicitGroup0

<global> //One global per file
global_label=Global1

//<control> //is not part of the hierarchy. control has no label.

master_label=ImplicitMaster0 //Not parsed? -> Unlabeled
group_label=ImplicitGroup0 //Not parsed? -> Unlabeled
<region> region_label=Region-1a //This is put into implicitMaster0 and implicitGroup0

<group> group_label=Group0  //master_label=WrongLabel  //This is a label in the wrong place. It will override the group_label
<region> region_label=Region0a

<master> master_label=Master1

group_label=ImplicitGroup1 //Not parsed? -> Unlabeled

<group> group_label=Group1
<region> region_label=Region1a
<region> region_label=Region1b


<master> master_label=Master2
group_label=ImplicitGroup2 //Not parsed? -> Unlabeled


<group> group_label=Group2
<region> region_label=Region2a
<region> region_label=Region2b
"""

pgm = instrument.engine.load_patch_from_string(pgm_no, '.', sfzString, 'test_sampler_hierarchy')

print ("Program:", pgm, pgm.status()) # -> Program: SamplerProgram<a58c6888-19c5-4b0f-be03-c4ce83b6eeee>
print ("Control Inits:", pgm.get_control_inits())  #Empty . Is this <control> ? No.
globalHierarchy = pgm.get_global()  # -> Single SamplerLayer. Literally sfz <global>. But not the global scope, e.g. no under any <tag>.
#If there is no <global> tag in the .sfz this will still create a root SamplerLayer
print ("Global:", globalHierarchy)

def recurse(item, level = 0):
    status = item.status()
    print ("  " * level + str(status))
    for subitem in item.get_children():
        recurse(subitem, level + 1)

recurse(globalHierarchy)

#print (globalHierarchy.get_params_full()["hallo"])  #This is a custom opcode, just <global>hallo=welt . It throws an error on load, but is saved nevertheless.
#print ("Global Hierarchy:", globalHierarchy.get_children()) # -> SamplerLayer. Traverse the hierarchy. Why is there only one to start with?

hierarchy = pgm.get_hierarchy() #starts with global and dicts down with get_children(). First single entry layer is get_global()
print ("Complete Hierarchy")
pprint(hierarchy)


print("Ready!")
while True:
    cbox.call_on_idle(cmd_dumper)
