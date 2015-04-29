/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 sigsrv
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
#ifndef __MICROPY_INCLUDED_MICROTHREAD_H__
#define __MICROPY_INCLUDED_MICROTHREAD_H__

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objfun.h"
#include "py/runtime.h"
#include "py/bc.h"

typedef struct _mp_microthread_t {
    mp_obj_t module;
    mp_obj_t fun_bc;
    mp_code_state *code_state;
    mp_vm_return_kind_t last_kind;

    mp_obj_dict_t *dict_locals;
    mp_obj_dict_t *dict_globals;

    #if MICROPY_LIMIT_CPU
    mp_uint_t cpu_max_opcodes_executeable;
    mp_uint_t cpu_min_opcodes_executeable;
    mp_uint_t cpu_current_opcodes_executed;
    #endif
    
    /* any value in MP_STATE_xxx(...) */
} mp_microthread_t;

mp_microthread_t *mp_current_microthread;
mp_microthread_t *_mp_fallback_microthread;

mp_microthread_t *mp_new_microthread(const char *thread_name, mp_obj_t fun_bc);

void mp_init_microthread(void);

void mp_load_microthread(mp_microthread_t *microthread);
void mp_store_microthread(mp_microthread_t *microthread);

mp_vm_return_kind_t mp_resume_microthread(mp_microthread_t *microthread);

#define MP_ENTER_MICROTHREAD(microthread) do { \
    mp_store_microthread(_mp_fallback_microthread); \
    mp_load_microthread((microthread)); \
} while(0)

#define MP_EXIT_MICROTHREAD(microthread) do { \
    mp_store_microthread((microthread)); \
    mp_load_microthread(_mp_fallback_microthread); \
} while(0)


#endif // __MICROPY_INCLUDED_MICROTHREAD_H__
