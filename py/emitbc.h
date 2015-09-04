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

#ifndef __MICROPY_INCLUDED_PY_EMITBC_H__
#define __MICROPY_INCLUDED_PY_EMITBC_H__

#include "py/mpstate.h"
#include "py/emit.h"
#include "py/bc0.h"

struct _emit_t;

// emit_bc enable by EcmaXp
#if 1
// other function can access emitbc function. (eg. modpersist)
#define EMIT_BC_STATIC
#else
#define EMIT_BC_STATIC STATIC
#endif

typedef byte *(*emit_allocator_t)(emit_t *emit, int nbytes);

EMIT_BC_STATIC void emit_write_uint(emit_t *emit, emit_allocator_t allocator, mp_uint_t val);
EMIT_BC_STATIC byte *emit_get_cur_to_write_code_info(emit_t *emit, int num_bytes_to_write);
EMIT_BC_STATIC void emit_align_code_info_to_machine_word(emit_t *emit);
EMIT_BC_STATIC void emit_write_code_info_uint(emit_t *emit, mp_uint_t val);
EMIT_BC_STATIC void emit_write_code_info_qstr(emit_t *emit, qstr qst);

#if MICROPY_ENABLE_SOURCE_LINE
EMIT_BC_STATIC void emit_write_code_info_bytes_lines(emit_t *emit, mp_uint_t bytes_to_skip, mp_uint_t lines_to_skip);
#endif

// all functions must go through this one to emit byte code
EMIT_BC_STATIC byte *emit_get_cur_to_write_bytecode(emit_t *emit, int num_bytes_to_write);
EMIT_BC_STATIC void emit_align_bytecode_to_machine_word(emit_t *emit);
EMIT_BC_STATIC void emit_write_bytecode_byte(emit_t *emit, byte b1);
EMIT_BC_STATIC void emit_write_bytecode_uint(emit_t *emit, mp_uint_t val);
EMIT_BC_STATIC void emit_write_bytecode_byte_byte(emit_t *emit, byte b1, byte b2);
EMIT_BC_STATIC void emit_write_bytecode_byte_int(emit_t *emit, byte b1, mp_int_t num);
EMIT_BC_STATIC void emit_write_bytecode_byte_uint(emit_t *emit, byte b, mp_uint_t val);
EMIT_BC_STATIC void emit_write_bytecode_prealigned_ptr(emit_t *emit, void *ptr);
EMIT_BC_STATIC void emit_write_bytecode_byte_ptr(emit_t *emit, byte b, void *ptr);
// currently unused
// EMIT_BC_STATIC void emit_write_bytecode_byte_uint_uint(emit_t *emit, byte b, mp_uint_t num1, mp_uint_t num2);
EMIT_BC_STATIC void emit_write_bytecode_byte_qstr(emit_t *emit, byte b, qstr qst);
EMIT_BC_STATIC void emit_write_bytecode_byte_unsigned_label(emit_t *emit, byte b1, mp_uint_t label);
EMIT_BC_STATIC void emit_write_bytecode_byte_signed_label(emit_t *emit, byte b1, mp_uint_t label);

#if MICROPY_EMIT_NATIVE
EMIT_BC_STATIC void mp_emit_bc_set_native_type(emit_t *emit, mp_uint_t op, mp_uint_t arg1, qstr arg2);
#endif

#endif // __MICROPY_INCLUDED_PY_EMITBC_H__
