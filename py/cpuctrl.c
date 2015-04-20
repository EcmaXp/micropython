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

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/runtime.h"

void mp_cpu_ctrl_init(void) {
#if MICROPY_LIMIT_CPU
    /* TODO:
    
    */
    MP_STATE_CTX(maximum_opcodes_executeable) = -1;
    MP_STATE_CTX(current_opcodes_executed) = 0;
    MP_STATE_CTX(cpu_limit_level) = 0;
#endif
}

#if MICROPY_LIMIT_CPU

void mp_cpu_set_limit(mp_uint_t maximum_opcodes_executeable){
    MP_STATE_VM(maximum_opcodes_executeable) = maximum_opcodes_executeable;
}

mp_uint_t mp_cpu_get_limit(void){
    return MP_STATE_VM(maximum_opcodes_executeable);
}

void mp_cpu_set_usage(mp_uint_t current_opcodes_executed){
    MP_STATE_VM(current_opcodes_executed) = current_opcodes_executed;
}

void mp_cpu_clear_usage(void){
    mp_cpu_set_usage(0);
}

mp_uint_t mp_cpu_usage(void){
    return MP_STATE_VM(current_opcodes_executed);    
}

inline void mp_cpu_opcode_executed(void){
    MP_STATE_VM(current_opcodes_executed)++;
}

inline bool mp_cpu_is_limited(void){
    return MP_STATE_VM(maximum_opcodes_executeable) <= MP_STATE_VM(current_opcodes_executed);
}

/* TODO: should be limit
 (!) long time calc
 (!) big num calc
*/

#endif // MICROPY_LIMIT_CPU