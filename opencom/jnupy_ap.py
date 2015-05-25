### This file is jnupy auto parser ###
from __future__ import print_function
import sys
import os
import re

if __name__ != "__main__" or len(sys.argv) != 2:
    exit("usage: ./jnupy_ap.py jnupy.c")

# how about use compiled jar?

JNUPY_AP = "JNUPY_AP"
JNUPY_AP_DEFINE = "#define {}(...)".format(JNUPY_AP)
JNUPY_AP_CALL = re.compile(r"{}\((.*?)\)".format(re.escape(JNUPY_AP)))


try:
    unicode
except NameError:
    StringTypes = (str,)
else:
    StringTypes = (unicode, str,)

class FileControl():
    def __init__(self, lines):
        self.lines = lines
        self.ref = {i:i for i in range(len(lines))}
    
    def __getitem__(self, x):
        getline = self.lines.__getitem__
        lines = []
        for src, ref in self._getsource(x):
            if ref is not None:
                lines.append(getline(ref))
        
        if not isinstance(x, slice):
            if not lines:
                raise KeyError(x)
            return lines[0]
        else:
            return lines

    def __delitem__(self, x):
        for src, ref in self._getsource(x):
            if ref is not None:
                self.ref[src] = None
                self.lines[ref] = None

    def __setitem__(self, x, lines):
        if isinstance(lines, StringTypes):
            lines = [lines]

        first = True
        for src, ref in self._getsource(x):
            if ref is None:
                raise KeyError
            self.lines[ref] = lines
            break
        
    def __contains__(self, x):
        for src, ref in self._getsource(x):
            if ref is None:
                return False
                        
        return True

    def _getsource(self, x):
        getlineno = self.ref.get
        if not isinstance(x, slice):
            ref_lineno = getlineno(x)
            if ref_lineno is None:
                raise KeyError(x)
            return ((x, ref_lineno),)
        else:
            linenos = []
            start = x.start if x.start is not None else 0
            end = x.stop if x.stop is not None else len(self.ref)
            step = x.step if x.step is not None else 1
            i = start
            while i < end:
                ref_lineno = getlineno(i)
                # ref_lineno can be None
                linenos.append((i, ref_lineno))
                i += step
            return linenos

    def getvalue(self):
        result = []
        for line in self.lines:
            if line is None:
                continue
            
            if isinstace(lines, types.StringTypes):
                result.append(line)
            else:
                result.extend(line)

        return "\n".join(result)

CFG = {}
CFG_TAB = {}

REF = "REF"
LOAD = "LOAD"
EXPORT = "EXPORT"
START = "START"
END = "END"

#define JNUPY_JCLASS(a, b) b
#define JNUPY_JMETHOD(a, b) b
#define JNUPY_JFIELD(a, b) b
"""
	if (!(luadebug_class = referenceclass(env, "com/naef/jnlua/LuaState$LuaDebug"))
			|| !(luadebug_init_id = (*env)->GetMethodID(env, luadebug_class, "<init>", "(JZ)V"))
			|| !(luadebug_field_id = (*env)->GetFieldID(env, luadebug_class, "luaDebug", "J"))) {
"""

with open(sys.argv[1]) as fp:
    lines = fp.read().splitlines()

IS_JNUPY = False
IS_EXPORT = False

for lineno, rawline in enumerate(lines):
    line = rawline.rstrip()
    if line == JNUPY_AP_DEFINE:
        IS_JNUPY = True
        continue
    
    if not IS_JNUPY:
        continue
    
    line = line.strip()
    res = JNUPY_AP_CALL.match(line)
    if res:
        args = tuple(map(str.strip, res.group(1).split(",")))
        CFG[args] = lineno
        CFG_TAB[args] = rawline[len(rawline) - len(rawline.lstrip()):]

fc = FileControl(lines)

print(fc[CFG[REF, START] + 1:CFG[REF, END]])
print(fc[CFG[LOAD, START] + 1:CFG[LOAD, END]])

print(CFG)
print(CFG_TAB)
sys.exit(1)

with open(sys.argv[1], 'w') as fp:
    fp.write(fc.getvalue())

sys.exit(0)
