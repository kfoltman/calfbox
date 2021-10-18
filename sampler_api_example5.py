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



#A pseudo sfz file that features all kind of labels
sfzString = """
<region> region_label=Region-2a //This is put into implicitMaster0 and implicitGroup0

<global> //One global per file
global_label=Global1

//Prepare keyswitches to test labels later
sw_default=60
sw_lokey=60
sw_hikey=61

//<control> //is not part of the hierarchy. control has no label.

master_label=ImplicitMaster0 //Not parsed? -> Unlabeled
group_label=ImplicitGroup0 //Not parsed? -> Unlabeled
<region> region_label=Region-1a //This is put into implicitMaster0 and implicitGroup0

<group> group_label=Group0  //master_label=WrongLabel  //This is a label in the wrong place. It will override the group_label
<region> region_label=Region0a

<master> master_label=Master1 Label
sw_last=60
sw_label=Key 60: Keyswitch For Master 1


group_label=ImplicitGroup1 //Not parsed? -> Unlabeled

<group> group_label=Group1
<region> region_label=Region1a
<region> region_label=Region1b


<master> master_label=Master2
sw_last=61
sw_label=Key 61: Keyswitch For Master 2

group_label=ImplicitGroup2 //Not parsed? -> Unlabeled


<group> group_label=Group2
<region> region_label=Region2a
<region> wrongOpcode=hello world foo bar region_label=Region2b  //a wrong opcode. Will throw a warning on load but is nevertheless available in our python data

<group> group_label=Group3
<region> sample=*saw pitch_oncc1=40 label_cc1=Detune second oscillator
<region> key=72 label_key72=A very special key
"""

pgm = instrument.engine.load_patch_from_string(pgm_no, '.', sfzString, 'test_sampler_hierarchy')

print ("Program:", pgm, pgm.status()) # -> Program: SamplerProgram<a58c6888-19c5-4b0f-be03-c4ce83b6eeee>
print ("Control Inits:", pgm.get_control_inits())  #Empty . Is this <control> ? No.
globalHierarchy = pgm.get_global()  # -> Single SamplerLayer. Literally sfz <global>. But not the global scope, e.g. no under any <tag>.
#If there is no <global> tag in the .sfz this will still create a root SamplerLayer
print ("Global:", globalHierarchy)


print("\nShow all SamplerLayer in their global/master/group/region hierarchy through indentations.\n" + "=" * 80)
def recurse(item, level = 0):
    status = item.status()
    print ("  " * level + str(status))
    for subitem in item.get_children():
        recurse(subitem, level + 1)

recurse(globalHierarchy)


print("\nShow all non-engine-default sfz opcodes and values in a global/master/group/region hierarchy.\n" + "=" * 80)
def recurse2(item, level = 0):
    status = item.status()
    data = item.as_dict()
    children = item.get_children()
    if data or children:
        print ("  " * level + "<%s> %s" % (item.status().level, data))
    for subitem in children:
        recurse2(subitem, level + 1)

recurse2(globalHierarchy)

print("\n\n" + "=" * 80)



#The following were just stages during development, now handled by recurse and recurse2() above
"""
hierarchy = pgm.get_hierarchy() #starts with global and dicts down with get_children(). First single entry layer is get_global()
print ("Complete Hierarchy")
pprint(hierarchy)

for k,v in hierarchy.items():  #Global
    print (k.as_string())
    for k1,v1 in v.items():  #Master
        print (k1.as_string())
        if v1:
            for k2,v2 in v1.items():  #Group
                print (k2.as_string())
                if v2:
                    for k3,v3 in v2.items():  #Regions
                        print (k3.as_string())
"""

print("Ready!")
while True:
    cbox.call_on_idle(cmd_dumper)
