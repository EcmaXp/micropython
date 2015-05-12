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
#ifndef __MICROPY_INCLUDED_PY_CPUCTRL_H__
#define __MICROPY_INCLUDED_PY_CPUCTRL_H__

#include <stdio.h>
#include "py/mpconfig.h"

void mp_cpu_ctrl_init(void);

#if MICROPY_LIMIT_CPU

#define MP_CPU_UPDATE_STATUS() (_mp_cpu_update_status())
#define MP_CPU_FORCE_UPDATE_STATUS() (_mp_cpu_update_status())
#define MP_CPU_HARD_CHECK() (!_mp_cpu_is_hard_limited())
#define MP_CPU_SOFT_CHECK() (!_mp_cpu_is_soft_limited())
#define MP_CPU_SAFE_CHECK() (!_mp_cpu_is_safe_limited())
#define MP_CPU_CLEAR_SOFT_LIMITED() (mp_cpu_clear_soft_limited())

void mp_cpu_set_hard_limit(mp_uint_t hard_limit);
void mp_cpu_set_soft_limit(mp_uint_t soft_limit);
void mp_cpu_set_soft_limited(void);
void mp_cpu_clear_soft_limited(void);
void mp_cpu_set_limit(mp_uint_t safe_limit);

mp_uint_t mp_cpu_get_hard_limit(void);
mp_uint_t mp_cpu_get_soft_limit(void);
bool mp_cpu_get_soft_limited(void);
mp_uint_t mp_cpu_get_safe_limit(void);

void mp_cpu_set_usage(mp_uint_t cpu_current_executed);
void mp_cpu_clear_usage(void);
mp_uint_t mp_cpu_usage(void);

void mp_cpu_update_status(void);

void mp_cpu_exc_hard_limit(void);
void mp_cpu_exc_soft_limit(void);

inline bool _mp_cpu_update_status(void) {
#if MICROPY_LIMIT_CPU_CHECK_INTERVAL
    if (MP_STATE_VM(cpu_check_clock)-- <= 0) {
        mp_cpu_update_status();
        return true;
    }
    
    return false;
#else
    MP_STATE_VM(cpu_current_executed)++;
    return true;
#endif
}

inline bool _mp_cpu_is_hard_limited() {
    return (MP_STATE_VM(cpu_hard_limit) > 0) && \
        (MP_STATE_VM(cpu_hard_limit) <= MP_STATE_VM(cpu_current_executed));
}

inline bool _mp_cpu_is_soft_limited() {
    return (MP_STATE_VM(cpu_soft_limit) > 0) && \
        !MP_STATE_VM(cpu_soft_limit_executed) && \
        (MP_STATE_VM(cpu_soft_limit) <= MP_STATE_VM(cpu_current_executed));
}

inline bool _mp_cpu_is_safe_limited() {
    return (MP_STATE_VM(cpu_safe_limit) > 0) && \
        (MP_STATE_VM(cpu_safe_limit) <= MP_STATE_VM(cpu_current_executed));
}

#else // MICROPY_LIMIT_CPU

#define MP_CPU_UPDATE_STATUS() (false)
#define MP_CPU_FORCE_UPDATE_STATUS()
#define MP_CPU_HARD_CHECK() (true)
#define MP_CPU_SOFT_CHECK() (true)
#define MP_CPU_SAFE_CHECK() (true)
#define MP_CPU_CLEAR_SOFT_LIMITED()

#endif // MICROPY_LIMIT_CPU

#endif // __MICROPY_INCLUDED_PY_CPUCTRL_H__
