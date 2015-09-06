
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
#include "py/bc0.h"
#include "copybc.h"
#include "copybc0.h"

#define DECODE_UINT { \
    unum = 0; \
    do { \
        unum = (unum << 7) + (*ip & 0x7f); \
    } while ((*ip++ & 0x80) != 0); \
}
#define DECODE_ULABEL do { unum = (ip[0] | (ip[1] << 8)); ip += 2; } while (0)
#define DECODE_SLABEL do { unum = (ip[0] | (ip[1] << 8)) - 0x8000; ip += 2; } while (0)
#define DECODE_QSTR { \
    qst = 0; \
    do { \
        qst = (qst << 7) + (*ip & 0x7f); \
    } while ((*ip++ & 0x80) != 0); \
}
#define DECODE_PTR do { \
    ip = (byte*)(((mp_uint_t)ip + sizeof(mp_uint_t) - 1) & (~(sizeof(mp_uint_t) - 1))); /* align ip */ \
    unum = *(mp_uint_t*)ip; \
    ip += sizeof(mp_uint_t); \
} while (0)

void mp_copybc_copy(const byte *ip, mp_uint_t len, mp_copybc_handler_t handler, void *handler_data) {
    const byte const *code_start = ip;
    while (ip < len + code_start) {
        mp_copybc_opdata_t opdata = mp_copybc_subcopy(code_start, ip);
        ip = opdata.next_ip;
        handler(handler_data, &opdata);
    }
}

mp_copybc_opdata_t mp_copybc_subcopy(const byte const *code_start, const byte *ip) {
    mp_copybc_opdata_t opdata = {ip, ip, -1, false, false, false};

    mp_uint_t unum;
    qstr qst;
    switch (*ip++) {
        case MP_BC_LOAD_CONST_FALSE:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_LOAD_CONST_NONE:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_LOAD_CONST_TRUE:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_LOAD_CONST_SMALL_INT: {
            HANDLE_OP;
            mp_int_t num = 0;
            if ((ip[0] & 0x40) != 0) {
                num--;
            }
            do {
                num = (num << 7) | (*ip & 0x7f);
            } while ((*ip++ & 0x80) != 0);
            HANDLE_INT;
            HANDLE_FINISH;
        }
        case MP_BC_LOAD_CONST_STRING:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_LOAD_CONST_OBJ:
            HANDLE_OP;
            DECODE_PTR;
            HANDLE_PTR;
            HANDLE_FINISH;
        case MP_BC_LOAD_NULL:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_LOAD_FAST_N:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_LOAD_DEREF:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_LOAD_NAME:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                HANDLE_EXTRA(*ip++);
            }
            HANDLE_FINISH;
        case MP_BC_LOAD_GLOBAL:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                HANDLE_EXTRA(*ip++);
            }
            HANDLE_FINISH;
        case MP_BC_LOAD_ATTR:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                HANDLE_EXTRA(*ip++);
            }
            HANDLE_FINISH;
        case MP_BC_LOAD_METHOD:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_LOAD_BUILD_CLASS:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_LOAD_SUBSCR:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_STORE_FAST_N:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_STORE_DEREF:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_STORE_NAME:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_STORE_GLOBAL:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_STORE_ATTR:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                HANDLE_EXTRA(*ip++);
            }
            HANDLE_FINISH;
        case MP_BC_STORE_SUBSCR:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_DELETE_FAST:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_DELETE_DEREF:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_DELETE_NAME:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_DELETE_GLOBAL:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_DUP_TOP:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_DUP_TOP_TWO:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_POP_TOP:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_ROT_TWO:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_ROT_THREE:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_JUMP:
            HANDLE_OP;
            DECODE_SLABEL;
            HANDLE_SLABEL;
            HANDLE_FINISH;
        case MP_BC_POP_JUMP_IF_TRUE:
            HANDLE_OP;
            DECODE_SLABEL;
            HANDLE_SLABEL;
            HANDLE_FINISH;
        case MP_BC_POP_JUMP_IF_FALSE:
            HANDLE_OP;
            DECODE_SLABEL;
            HANDLE_SLABEL;
            HANDLE_FINISH;
        case MP_BC_JUMP_IF_TRUE_OR_POP:
            HANDLE_OP;
            DECODE_SLABEL;
            HANDLE_SLABEL;
            HANDLE_FINISH;
        case MP_BC_JUMP_IF_FALSE_OR_POP:
            HANDLE_OP;
            DECODE_SLABEL;
            HANDLE_SLABEL;
            HANDLE_FINISH;
        case MP_BC_SETUP_WITH:
            HANDLE_OP;
            DECODE_ULABEL;
            HANDLE_ULABEL;
            HANDLE_FINISH;
        case MP_BC_WITH_CLEANUP:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_UNWIND_JUMP:
            HANDLE_OP;
            DECODE_SLABEL;
            HANDLE_SLABEL;
            ip += 1;
            HANDLE_FINISH;
        case MP_BC_SETUP_EXCEPT:
            HANDLE_OP;
            DECODE_ULABEL;
            HANDLE_ULABEL;
            HANDLE_FINISH;
        case MP_BC_SETUP_FINALLY:
            HANDLE_OP;
            DECODE_ULABEL;
            HANDLE_ULABEL;
            HANDLE_FINISH;
        case MP_BC_END_FINALLY:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_GET_ITER:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_FOR_ITER:
            HANDLE_OP;
            DECODE_ULABEL;
            HANDLE_ULABEL;
            HANDLE_FINISH;
        case MP_BC_POP_BLOCK:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_POP_EXCEPT:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_NOT:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_BUILD_TUPLE:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_BUILD_LIST:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_LIST_APPEND:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_BUILD_MAP:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_STORE_MAP:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_MAP_ADD:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_BUILD_SET:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_SET_ADD:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
#if MICROPY_PY_BUILTINS_SLICE
        case MP_BC_BUILD_SLICE:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
#endif
        case MP_BC_UNPACK_SEQUENCE:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_UNPACK_EX:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_MAKE_FUNCTION:
            HANDLE_OP;
            DECODE_PTR;
            HANDLE_PTR;
            HANDLE_FINISH;
        case MP_BC_MAKE_FUNCTION_DEFARGS:
            HANDLE_OP;
            DECODE_PTR;
            HANDLE_PTR;
            HANDLE_FINISH;
        case MP_BC_MAKE_CLOSURE: {
            HANDLE_OP;
            DECODE_PTR;
            HANDLE_PTR;
            HANDLE_EXTRA(*ip++);
            HANDLE_FINISH;
        }
        case MP_BC_MAKE_CLOSURE_DEFARGS: {
            HANDLE_OP;
            DECODE_PTR;
            HANDLE_PTR;
            HANDLE_EXTRA(*ip++);
            HANDLE_FINISH;
        }
        case MP_BC_CALL_FUNCTION:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_CALL_FUNCTION_VAR_KW:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_CALL_METHOD:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_CALL_METHOD_VAR_KW:
            HANDLE_OP;
            DECODE_UINT;
            HANDLE_UINT;
            HANDLE_FINISH;
        case MP_BC_RETURN_VALUE:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_RAISE_VARARGS:
            HANDLE_OP;
            HANDLE_EXTRA(*ip++);
            HANDLE_FINISH;
        case MP_BC_YIELD_VALUE:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_YIELD_FROM:
            HANDLE_OP;
            HANDLE_FINISH;
        case MP_BC_IMPORT_NAME:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_IMPORT_FROM:
            HANDLE_OP;
            DECODE_QSTR;
            HANDLE_QSTR;
            HANDLE_FINISH;
        case MP_BC_IMPORT_STAR:
            HANDLE_OP;
            HANDLE_FINISH;
        default:
            if (ip[-1] < MP_BC_LOAD_CONST_SMALL_INT_MULTI + 64) {
            } else if (ip[-1] < MP_BC_LOAD_FAST_MULTI + 16) {
            } else if (ip[-1] < MP_BC_STORE_FAST_MULTI + 16) {
            } else if (ip[-1] < MP_BC_UNARY_OP_MULTI + 6) {
            } else if (ip[-1] < MP_BC_BINARY_OP_MULTI + 36) {
                mp_uint_t op = ip[-1] - MP_BC_BINARY_OP_MULTI;
                (void)op;
                HANDLE_OP;
            } else {
                assert(0);
                HANDLE_INVAILD;
            }
            HANDLE_FINISH;
    }
    HANDLE_INVAILD;
}

