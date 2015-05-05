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
#include "py/cpuctrl.h"

void mp_cpu_ctrl_init(void) {
#if MICROPY_LIMIT_CPU
    MP_STATE_VM(cpu_hard_limit) = 0;
    MP_STATE_VM(cpu_soft_limit) = 0;
    MP_STATE_VM(cpu_safe_limit) = 0;
    
    MP_STATE_VM(cpu_current_executed) = 0;

    // when soft_limit reach; it will raise error.
    MP_STATE_VM(cpu_soft_limit_executed) = false;
#endif
}

#if MICROPY_LIMIT_CPU

void mp_cpu_set_hard_limit(mp_uint_t hard_limit){
    MP_STATE_VM(cpu_hard_limit) = hard_limit;
}

void mp_cpu_set_soft_limit(mp_uint_t soft_limit){
    MP_STATE_VM(cpu_soft_limit) = soft_limit;
}

void mp_cpu_set_soft_limited(void){
    MP_STATE_VM(cpu_soft_limit_executed) = true;
}

void mp_cpu_clear_soft_limited(void){
    MP_STATE_VM(cpu_soft_limit_executed) = false;
}

void mp_cpu_set_limit(mp_uint_t safe_limit){
    MP_STATE_VM(cpu_safe_limit) = safe_limit;
}

mp_uint_t mp_cpu_get_hard_limit(void){
    return MP_STATE_VM(cpu_hard_limit);
}

mp_uint_t mp_cpu_get_soft_limit(void){
    return MP_STATE_VM(cpu_soft_limit);
}

bool mp_cpu_get_soft_limited(void){
    return MP_STATE_VM(cpu_soft_limit_executed);
}

mp_uint_t mp_cpu_get_safe_limit(void){
    return MP_STATE_VM(cpu_safe_limit);
}

void mp_cpu_set_usage(mp_uint_t cpu_current_executed){
    MP_STATE_VM(cpu_current_executed) = cpu_current_executed;
}

void mp_cpu_clear_usage(void){
    mp_cpu_set_usage(0);
}

mp_uint_t mp_cpu_usage(void){
    return MP_STATE_VM(cpu_current_executed);    
}

void mp_cpu_exc_hard_limit(void) {
    nlr_raise(mp_obj_new_exception(&mp_type_SystemHardLimit));
}

void mp_cpu_exc_soft_limit(void) {
    MP_STATE_VM(cpu_soft_limit_executed) = true;
    nlr_raise(mp_obj_new_exception(&mp_type_SystemSoftLimit));
}

/* TODO: should be limit
 (!) long time calc
 (!) big num calc
*/

#endif // MICROPY_LIMIT_CPU