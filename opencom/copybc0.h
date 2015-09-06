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
#ifndef __MICROPY_INCLUDED_OPENCOM_COPYBC0_H__
#define __MICROPY_INCLUDED_OPENCOM_COPYBC0_H__

#include "py/obj.h"

#define ASSIGN(tag_is, tag_data, x) opdata.tag_is = true; opdata.data.tag_data = (x);

#define HANDLE(x, y, z) handler(handler_data, x, y, z)
#define HANDLE_ULABEL ASSIGN(is_num, u_num, ip + unum - code_start)
#define HANDLE_SLABEL ASSIGN(is_num, u_num, ip + unum - code_start)
#define HANDLE_PTR ASSIGN(is_ptr, u_ptr, (machine_ptr_t)unum);
#define HANDLE_INT ASSIGN(is_num, u_num, num);
#define HANDLE_UINT ASSIGN(is_unum, u_unum, unum);
#define HANDLE_QSTR ASSIGN(is_qstr, u_qstr, qst);
#define HANDLE_OP opdata.ip = (ip - 1); opdata.op = *(ip - 1);
#define HANDLE_EXTRA(x) opdata.has_extra = true; opdata.extra = (x);
#define HANDLE_FINISH opdata.next_ip = ip; return opdata;
#define HANDLE_INVAILD assert(0); opdata.next_ip = ip; return opdata;

#endif // __MICROPY_INCLUDED_OPENCOM_COPYBC0_H__