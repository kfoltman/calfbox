from _cbox import *

class GetThings:
    def __init__(self, cmd, anames, args):
        for i in anames:
            if i.startswith("*"):
                setattr(self, i[1:], [])
            elif i.startswith("%"):
                setattr(self, i[1:], {})
            else:
                setattr(self, i, None)
        anames = set(anames)
        self.seq = []
        def update_callback(cmd, fb, args):
            self.seq.append((cmd, fb, args))
            cmd = cmd[1:]
            if cmd in anames:
                if len(args) == 1:
                    setattr(self, cmd, args[0])
                else:
                    setattr(self, cmd, args)
            elif "*" + cmd in anames:
                if len(args) == 1:
                    getattr(self, cmd).append(args[0])
                else:
                    getattr(self, cmd).append(args)
            elif "%" + cmd in anames:
                if len(args) == 2:
                    getattr(self, cmd)[args[0]] = args[1]
                else:
                    getattr(self, cmd)[args[0]] = args[1:]
        do_cmd(cmd, update_callback, args)
    def __str__(self):
        return str(self.seq)

class VarPath:
    def __init__(self, path, args = []):
        self.path = path
        self.args = args
    def plus(self, subpath, *args):
        return VarPath(self.path if subpath is None else self.path + "/" + subpath, self.args + list(args))
    def set(self, *values):
        do_cmd(self.path, None, self.args + list(values))

class Config:
    @staticmethod
    def sections(prefix = ""):
        return [CfgSection(name) for name in GetThings('/config/sections', ['*section'], [str(prefix)]).section]

    @staticmethod
    def keys(section, prefix = ""):
        return GetThings('/config/keys', ['*key'], [str(section), str(prefix)]).key

    @staticmethod
    def get(section, key):
        return GetThings('/config/get', ['value'], [str(section), str(key)]).value

    @staticmethod
    def set(section, key, value):
        do_cmd('/config/set', None, [str(section), str(key), str(value)])

    @staticmethod
    def delete(section, key):
        do_cmd('/config/delete', None, [str(section), str(key)])

    @staticmethod
    def save(filename = None):
        if filename is None:
            do_cmd('/config/save', None, [])
        else:
            do_cmd('/config/save', None, [str(filename)])

class CfgSection:
    def __init__(self, name):
        self.name = name
        
    def __getitem__(self, key):
        return Config.get(self.name, key)

    def __setitem__(self, key, value):
        Config.set(self.name, key, value)
        
    def __delitem__(self, key):
        Config.delete(self.name, key)
        
    def keys(self, prefix = ""):
        return Config.keys(self.name, prefix)