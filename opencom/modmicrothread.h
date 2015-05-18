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
#ifndef __MICROPY_INCLUDED_OPENCOM_MODMICROTHREAD_H__
#define __MICROPY_INCLUDED_OPENCOM_MODMICROTHREAD_H__

#include "py/obj.h"
#include "py/bc.h"

/* from py/mpstate.h */
typedef struct _mp_microthread_context_t {
    // == context ==
    mp_obj_dict_t *dict_locals;
    mp_obj_dict_t *dict_globals;

    // == context.mem ==
    // there is no memory context.

    // == context.vm ==
    // map with loaded modules
    mp_map_t mp_loaded_modules_map;

    #if MICROPY_PY_SYS_EXC_INFO
    mp_obj_t cur_exception;
    #endif

    // unused
    // mp_obj_dict_t dict_main;

    #if MICROPY_CAN_OVERRIDE_BUILTINS
    mp_obj_dict_t *mp_module_builtins_override_dict;
    #endif

    #if MICROPY_LIMIT_CPU
    mp_int_t cpu_last_check_clock;
    mp_int_t cpu_check_clock;
    
    mp_int_t cpu_hard_limit;
    mp_int_t cpu_soft_limit;
    mp_int_t cpu_safe_limit;
    
    mp_int_t cpu_current_executed;
    bool cpu_soft_limit_executed;
    #endif
} mp_microthread_context_t;

typedef enum _mp_microthread_status_t {
    MP_MICROTHREAD_STATUS_READY = 1,
    MP_MICROTHREAD_STATUS_RUNNING = 2,
    MP_MICROTHREAD_STATUS_STOP = 3,
    MP_MICROTHREAD_STATUS_YIELD = 4,
    MP_MICROTHREAD_STATUS_SOFT_PAUSE = 5,
    MP_MICROTHREAD_STATUS_HARD_PAUSE = 6,
} mp_microthread_status_t;

typedef enum _mp_microthread_resume_kind_t {
    MP_MRK_STOP = 0,
    MP_MRK_RUNNING = 1,
    MP_MRK_NORMAL = 2,
    MP_MRK_YIELD = 3,
    MP_MRK_PAUSE = 4,
    MP_MRK_FORCE_PAUSE = 5,
    MP_MRK_EXCEPTION = 6,
    MP_MRK_SOFT_LIMIT = 7,
    MP_MRK_HARD_LIMIT = 8,
} mp_microthread_resume_kind_t;

typedef mp_microthread_resume_kind_t mp_mrk_t;

typedef struct _mp_obj_microthread_t {
    mp_obj_base_t base;

    mp_obj_t name;
    mp_obj_t fun;
    mp_obj_t last_result;
    mp_obj_t prev_thread;

    mp_code_state *code_state;
    mp_microthread_status_t status;
    mp_microthread_context_t context;
} mp_obj_microthread_t;

const mp_obj_type_t mp_type_microthread;

mp_microthread_resume_kind_t microthread_resume(mp_obj_microthread_t *thread, mp_obj_t send_value, mp_obj_t *result);

#endif // __MICROPY_INCLUDED_OPENCOM_MODMICROTHREAD_H__
