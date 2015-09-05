#!/usr/bin/env python3
### This file is py/showbc.c -> copybc.c auto parser ###
from __future__ import print_function
import collections
import hashlib
import base64
import sys
import os
import re

if __name__ != "__main__" or len(sys.argv) != 2:
    exit("usage: ./copybc_ap.py copybc.c")

template = """
/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 EcmaXp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/obj.h"
#include "copybc.h"
#include "copybc0.h"

const byte *mp_copybc_copy(const byte *ip, copybc_handler* handler) {
    // init copy
    
<RESULT>
"""

with open("../py/showbc.c", 'r') as fp:
    content = fp.read()
    lines = content.splitlines()

assert "const byte *mp_bytecode_print_str(const byte *ip) {" in content

result = []

get_indent = lambda: line[:len(line) - len(sline)]

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
    
    sline = line.strip()
    if sline.startswith("//"):
        continue
    elif sline.startswith("printf"):
        if "*ip++" in sline:
            indent = get_indent()
            sline = "*ip++;"
            line = indent + sline
        else:
            continue
    elif sline.startswith("mp_obj_print_helper"):
        indent = get_indent()
        sline = "(mp_obj_t)unum;"
        line = indent + sline
        continue
    elif sline.startswith("case") and ":" in sline:
        indent = get_indent()
        result.append(line)
        result.append(indent + " " * 4 + "HANDLE_OP(ip[-1]);")
        continue
    elif sline == "mp_uint_t op = ip[-1] - MP_BC_BINARY_OP_MULTI;":
        indent = get_indent()
        result.append(line)
        result.append(indent + "HANDLE_OP(op);")
        continue
    elif "//" in line:
        line = line[:line.index("//")]
    
    line = line.replace("DECODE_", "HANDLE_")
    result.append(line)

with open(sys.argv[1], 'w') as fp:
    content = template.replace("<RESULT>", "\n".join(result))
    fp.write(content)
    
    if not content.endswith("\n\n"):
        fp.write("\n")

sys.exit(0)
