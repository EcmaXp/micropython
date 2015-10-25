#!/usr/bin/env python3
### This file is py/showbc.c -> parsebc.c auto parser ###
from __future__ import print_function
import collections
import hashlib
import base64
import sys
import os
import re

if __name__ != "__main__" or len(sys.argv) != 3:
    exit("usage: ./parsebc_ap.py parsebc.c parsebc_tpl.c")

with open(sys.argv[2], "r") as fp:
    template = fp.read()

mecro_result = []
result = []

with open("../py/showbc.c", 'r') as fp:
    lines = fp.read().splitlines()

get_indent = lambda: line[:len(line) - len(sline)]
def build_line(sline):
    indent = get_indent()
    line = indent + sline
    return sline, line

assert "#if MICROPY_DEBUG_PRINTERS" in lines and "const byte *mp_showbc_code_start;" in lines

start_mecro_parse = False
for line in lines:
    if "#if MICROPY_DEBUG_PRINTERS" in line:
        start_mecro_parse = True
        continue
    elif not start_mecro_parse:
        continue
    elif "const byte *mp_showbc_code_start;" in line:
        assert start_mecro_parse
        start_mecro_parse = False
        break

    sline = line.strip()
    if sline.startswith("//"):
        continue
    elif "//" in line:
        line = line[:line.index("//")]
    
    mecro_result.append(line)

assert "const byte *mp_bytecode_print_str(const byte *ip) {" in lines

chk_point = 0
start_parse = False
for line in lines:
    if "const byte *mp_bytecode_print_str" in line:
        start_parse = True
        continue
    elif not start_parse:
        continue
    elif "void mp_bytecode_print2" in line:
        assert start_parse
        start_parse = False
        break
    
    line = line.rstrip()
    if "//" in line:
        line = line[:line.index("//")].rstrip()
    sline = line.strip()
    
    if not sline:
        continue
    elif "DECODE_" in line:
        result.append(line)
        result.append(build_line(sline.replace("DECODE_", "HANDLE_"))[1])
    elif "switch (*ip++) {" in sline:
        chk_point += 1
        result.append(line)
    elif "while ((*ip++ & 0x80) != 0)" in sline:
        chk_point += 1
        result.append(line)
        result.append(build_line("HANDLE_INT;")[1])
    elif "*ip++" in sline:
        result.append(build_line("HANDLE_EXTRA(*ip++);")[1])
    elif sline.startswith("printf"):
        pass
    elif sline.startswith("mp_obj_print_helper"):
        chk_point += 1
    elif sline.startswith("case") and ":" in sline:
        indent = get_indent()
        result.append(line)
        result.append(indent + " " * 4 + "HANDLE_OP;")
    elif sline == "mp_uint_t op = ip[-1] - MP_BC_BINARY_OP_MULTI;":
        chk_point += 1
        result.append(line)
        result.append(build_line("(void)op;")[1])
        result.append(build_line("HANDLE_OP;")[1])
    elif sline == "break;":
        result.append(build_line("HANDLE_FINISH;")[1])
    elif sline == "return ip;":
        chk_point += 1
        result.append(build_line("HANDLE_INVAILD;")[1])
    else:
        result.append(line)

assert chk_point == 6

mecro_result = "\n".join(mecro_result).strip()
result = "\n".join(result).strip()
result = result.rstrip().rstrip("}").rstrip() # strip '}'

content = template
content = content.replace("/*<MECRO_RESULT>*/", mecro_result)
content = content.replace("/*<RESULT>*/", result)

with open(sys.argv[1], 'w') as fp:
    fp.write(content)

sys.exit(0)
