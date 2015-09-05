
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

void mp_copybc_copy(const byte *ip, copybc_handler* handler) {
    // init copy
    
    mp_uint_t unum;
    qstr qst;

    switch (*ip++) {
        case MP_BC_LOAD_CONST_FALSE:
            break;

        case MP_BC_LOAD_CONST_NONE:
            break;

        case MP_BC_LOAD_CONST_TRUE:
            break;

        case MP_BC_LOAD_CONST_SMALL_INT: {
            mp_int_t num = 0;
            if ((ip[0] & 0x40) != 0) {
                num--;
            }
            do {
                num = (num << 7) | (*ip & 0x7f);
            } while ((*ip++ & 0x80) != 0);
            break;
        }

        case MP_BC_LOAD_CONST_STRING:
            HANDLE_QSTR;
            break;

        case MP_BC_LOAD_CONST_OBJ:
            HANDLE_PTR;
            break;

        case MP_BC_LOAD_NULL:
            break;

        case MP_BC_LOAD_FAST_N:
            HANDLE_UINT;
            break;

        case MP_BC_LOAD_DEREF:
            HANDLE_UINT;
            break;

        case MP_BC_LOAD_NAME:
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                *ip++;
            }
            break;

        case MP_BC_LOAD_GLOBAL:
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                *ip++;
            }
            break;

        case MP_BC_LOAD_ATTR:
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                *ip++;
            }
            break;

        case MP_BC_LOAD_METHOD:
            HANDLE_QSTR;
            break;

        case MP_BC_LOAD_BUILD_CLASS:
            break;

        case MP_BC_LOAD_SUBSCR:
            break;

        case MP_BC_STORE_FAST_N:
            HANDLE_UINT;
            break;

        case MP_BC_STORE_DEREF:
            HANDLE_UINT;
            break;

        case MP_BC_STORE_NAME:
            HANDLE_QSTR;
            break;

        case MP_BC_STORE_GLOBAL:
            HANDLE_QSTR;
            break;

        case MP_BC_STORE_ATTR:
            HANDLE_QSTR;
            if (MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) {
                *ip++;
            }
            break;

        case MP_BC_STORE_SUBSCR:
            break;

        case MP_BC_DELETE_FAST:
            HANDLE_UINT;
            break;

        case MP_BC_DELETE_DEREF:
            HANDLE_UINT;
            break;

        case MP_BC_DELETE_NAME:
            HANDLE_QSTR;
            break;

        case MP_BC_DELETE_GLOBAL:
            HANDLE_QSTR;
            break;

        case MP_BC_DUP_TOP:
            break;

        case MP_BC_DUP_TOP_TWO:
            break;

        case MP_BC_POP_TOP:
            break;

        case MP_BC_ROT_TWO:
            break;

        case MP_BC_ROT_THREE:
            break;

        case MP_BC_JUMP:
            HANDLE_SLABEL;
            break;

        case MP_BC_POP_JUMP_IF_TRUE:
            HANDLE_SLABEL;
            break;

        case MP_BC_POP_JUMP_IF_FALSE:
            HANDLE_SLABEL;
            break;

        case MP_BC_JUMP_IF_TRUE_OR_POP:
            HANDLE_SLABEL;
            break;

        case MP_BC_JUMP_IF_FALSE_OR_POP:
            HANDLE_SLABEL;
            break;

        case MP_BC_SETUP_WITH:
            HANDLE_ULABEL; 
            break;

        case MP_BC_WITH_CLEANUP:
            break;

        case MP_BC_UNWIND_JUMP:
            HANDLE_SLABEL;
            ip += 1;
            break;

        case MP_BC_SETUP_EXCEPT:
            HANDLE_ULABEL; 
            break;

        case MP_BC_SETUP_FINALLY:
            HANDLE_ULABEL; 
            break;

        case MP_BC_END_FINALLY:
            break;

        case MP_BC_GET_ITER:
            break;

        case MP_BC_FOR_ITER:
            HANDLE_ULABEL; 
            break;

        case MP_BC_POP_BLOCK:
            break;

        case MP_BC_POP_EXCEPT:
            break;

        case MP_BC_NOT:
            break;

        case MP_BC_BUILD_TUPLE:
            HANDLE_UINT;
            break;

        case MP_BC_BUILD_LIST:
            HANDLE_UINT;
            break;

        case MP_BC_LIST_APPEND:
            HANDLE_UINT;
            break;

        case MP_BC_BUILD_MAP:
            HANDLE_UINT;
            break;

        case MP_BC_STORE_MAP:
            break;

        case MP_BC_MAP_ADD:
            HANDLE_UINT;
            break;

        case MP_BC_BUILD_SET:
            HANDLE_UINT;
            break;

        case MP_BC_SET_ADD:
            HANDLE_UINT;
            break;

#if MICROPY_PY_BUILTINS_SLICE
        case MP_BC_BUILD_SLICE:
            HANDLE_UINT;
            break;
#endif

        case MP_BC_UNPACK_SEQUENCE:
            HANDLE_UINT;
            break;

        case MP_BC_UNPACK_EX:
            HANDLE_UINT;
            break;

        case MP_BC_MAKE_FUNCTION:
            HANDLE_PTR;
            break;

        case MP_BC_MAKE_FUNCTION_DEFARGS:
            HANDLE_PTR;
            break;

        case MP_BC_MAKE_CLOSURE: {
            HANDLE_PTR;
            mp_uint_t n_closed_over = *ip++;
            break;
        }

        case MP_BC_MAKE_CLOSURE_DEFARGS: {
            HANDLE_PTR;
            mp_uint_t n_closed_over = *ip++;
            break;
        }

        case MP_BC_CALL_FUNCTION:
            HANDLE_UINT;
            break;

        case MP_BC_CALL_FUNCTION_VAR_KW:
            HANDLE_UINT;
            break;

        case MP_BC_CALL_METHOD:
            HANDLE_UINT;
            break;

        case MP_BC_CALL_METHOD_VAR_KW:
            HANDLE_UINT;
            break;

        case MP_BC_RETURN_VALUE:
            break;

        case MP_BC_RAISE_VARARGS:
            unum = *ip++;
            break;

        case MP_BC_YIELD_VALUE:
            break;

        case MP_BC_YIELD_FROM:
            break;

        case MP_BC_IMPORT_NAME:
            HANDLE_QSTR;
            break;

        case MP_BC_IMPORT_FROM:
            HANDLE_QSTR;
            break;

        case MP_BC_IMPORT_STAR:
            break;

        default:
            if (ip[-1] < MP_BC_LOAD_CONST_SMALL_INT_MULTI + 64) {
            } else if (ip[-1] < MP_BC_LOAD_FAST_MULTI + 16) {
            } else if (ip[-1] < MP_BC_STORE_FAST_MULTI + 16) {
            } else if (ip[-1] < MP_BC_UNARY_OP_MULTI + 6) {
            } else if (ip[-1] < MP_BC_BINARY_OP_MULTI + 36) {
                mp_uint_t op = ip[-1] - MP_BC_BINARY_OP_MULTI;
            } else {
                assert(0);
                return ip;
            }
            break;
    }

    return ip;
}

