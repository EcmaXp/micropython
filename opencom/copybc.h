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
#ifndef __MICROPY_INCLUDED_OPENCOM_COPYBC_H__
#define __MICROPY_INCLUDED_OPENCOM_COPYBC_H__

#include "py/mpconfig.h"
#include "py/qstr.h"

typedef void (*mp_copybc_handler_t)(void*, const mp_copybc_opdata_t*);

typedef struct _mp_copybc_opdata_t {
    const byte *ip;
    const byte *next_ip;
    byte op;
    mp_uint_t is_ptr:1;
    mp_uint_t is_num:1;
    mp_uint_t is_unum:1;
    mp_uint_t is_qstr:1;
    mp_uint_t has_extra:1;
    union {
        machine_ptr_t u_ptr;
        mp_int_t u_num;
        mp_uint_t u_unum;
        qstr u_qstr;
    } data;
    mp_uint_t extra;
} mp_copybc_opdata_t;

void mp_copybc_copy(const byte *ip, mp_uint_t len, mp_copybc_handler_t handler, void *handler_data);
mp_copybc_opdata_t mp_copybc_subcopy(const byte const *code_start, const byte **ip_start);

#endif // __MICROPY_INCLUDED_OPENCOM_COPYBC_H__