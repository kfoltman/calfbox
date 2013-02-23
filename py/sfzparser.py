import os
import re

class SFZRegion(dict):
    def __init__(self, group):
        dict.__init__(self)
        self.group = group

    def merged(self):
        if self.group is None:
            return dict(self)
        v = dict(self)
        v.update(self.group)
        return v

    def __str__(self):
        return "(" + str(self.group) + ") : " + dict.__str__(self.merged())

class SFZContext:
    def __init__(self):
        self.group = None
        self.region = None

class SFZ:
    def __init__(self):
        self.regions = []
    
    def load(self, fname):
        self.parse(open(fname, "r").read())
            
    def parse(self, data):
        context = SFZContext()
        for ptype, pdata in re.findall("<(region|group)>\s*([^<]*)", data, re.S):
            self.parse_part(ptype, pdata.strip(), context)
            
    def parse_part(self, ptype, pdata, context):
        if ptype == 'group':
            context.group = {}
            context.region = None
            target = context.group
        else:
            context.region = SFZRegion(context.group)
            target = context.region
            
        pairs = re.split("\s+([a-zA-Z_0-9]+)=", " "+pdata)[1:]
        for i in range(0, len(pairs), 2):
            target[pairs[i]] = pairs[i + 1]
        if ptype == 'region':
            self.regions.append(target)
            
def find_sample_in_path(path, sample):
    jpath = os.path.join(path, sample)
    if os.path.exists(jpath):
        return jpath
    path, sample = os.path.split(jpath)
    sample = sample.lower()
    files = os.path.listdir(path)
    for f in files:
        if f.lower() == sample:
            return os.path.join(path, f)
    return None

    