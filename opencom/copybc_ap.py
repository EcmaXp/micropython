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

#include <assert.h>

#include "py/obj.h"
#include "copybc.h"
#include "copybc0.h"

<mecro_result>

void mp_copybc_copy(const byte *ip, mp_uint_t len, mp_copybc_handler_t handler, void *handler_data) {
    const byte const *code_start = ip;
    while (ip < len + code_start) {
        mp_copybc_opdata_t opdata = mp_copybc_subcopy(code_start, ip);
        ip = opdata.next_ip;
        handler(handler_data, &opdata);
    }
}

mp_copybc_opdata_t mp_copybc_subcopy(const byte const *code_start, const byte **ip_start) {
    mp_copybc_opdata_t opdata = {-1, false, false, false};
    const byte *ip = *ip_start;

    <result>
"""

mecro_result = []
result = []

with open("../py/showbc.c", 'r') as fp:
    content = fp.read()
    lines = content.splitlines()

get_indent = lambda: line[:len(line) - len(sline)]
def build_line(sline):
    indent = get_indent()
    line = indent + sline
    return sline, line

assert "#if MICROPY_DEBUG_PRINTERS" in content and "const byte *mp_showbc_code_start;" in content

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

assert "const byte *mp_bytecode_print_str(const byte *ip) {" in content

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
        line = line[:line.index("//")]
    
    sline = line.strip()
    if not sline:
        continue
    elif "DECODE_" in line:
        result.append(line)
        result.append(build_line(sline.replace("DECODE_", "HANDLE_"))[1])
    elif "switch (*ip++) {" in sline:
        chk_point += 1
    elif "while ((*ip++ & 0x80) != 0)" in sline:
        chk_point += 1
        result.append(line)
        result.append(build_line("HANDLE_INT;")[1])
        continue
    elif "*ip++" in sline:
        result.append(build_line("HANDLE_EXTRA(*ip++);")[1])
    elif sline.startswith("printf"):
        continue
    elif sline.startswith("mp_obj_print_helper"):
        chk_point += 1
        continue
    elif sline.startswith("case") and ":" in sline:
        indent = get_indent()
        result.append(line)
        result.append(indent + " " * 4 + "HANDLE_OP;")
        continue
    elif sline == "mp_uint_t op = ip[-1] - MP_BC_BINARY_OP_MULTI;":
        chk_point += 1
        result.append(line)
        result.append(build_line("(void)op;")[1])
        result.append(build_line("HANDLE_OP;")[1])
        continue
    elif sline == "break;":
        result.append(build_line("HANDLE_FINISH;")[1])
        continue
    elif sline == "return ip;":
        chk_point += 1
        result.append(build_line("HANDLE_INVAILD;")[1])
        continue
    else:
        result.append(line)

assert chk_point == 6

mecro_result = "\n".join(mecro_result).strip()
result = "\n".join(result).strip()

with open(sys.argv[1], 'w') as fp:
    content = template.replace("<mecro_result>", mecro_result).replace("<result>", result)
    fp.write(content)
    
    if not content.endswith("\n\n"):
        fp.write("\n")

sys.exit(0)
