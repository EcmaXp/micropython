### This file is jnupy auto parser ###
import sys
import os

if __name__ != "__main__" or len(sys.argv) != 2:
    exit("usage: ./jnupy_ap.py jnupy.c")

# how about use compiled jar?

CONFIG = {}

JNUPY_JNI_START_PARSE = "JNUPY_JNI_START_PARSE"
JNUPY_JNI_START_EXPORT = "JNUPY_JNI_START_EXPORT"

JNUPY_JNI_START_AUTOLOAD = "JNUPY_JNI_START_AUTOLOAD"
JNUPY_JNI_END_AUTOLOAD = "JNUPY_JNI_END_AUTOLOAD"
JNUPY_JNI_START_REF = "JNUPY_JNI_START_REF"
JNUPY_JNI_END_REF = "JNUPY_JNI_END_REF"

JNUPY_AUTO_TAG = (
    JNUPY_JNI_START_AUTOLOAD,
    JNUPY_JNI_END_AUTOLOAD,
    JNUPY_JNI_START_REF,
    JNUPY_JNI_END_REF,
)

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

for lineno, line in enumerate(lines):
    line = line.rstrip()
    if line == JNUPY_JNI_START_PARSE:
        IS_JNUPY = True
        continue
    
    if not IS_JNUPY:
        continue

    if line == JNUPY_JNI_START_AUTOLOAD:
        CONFIG[line] = lineno

with open(sys.argv[1], 'w') as fp:
    fp.write("\n".join(lines))

sys.exit(1)
