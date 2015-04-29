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

// Module Name: umicrothread
// This module will control microthread.

#include <stdio.h>
#include <string.h>

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objfun.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/bc.h"

/******************************************************************************/
/*

typedef struct _mp_obj_range_t {
    mp_obj_base_t base;
    // TODO make these values generic objects or something
    mp_int_t start;
    mp_int_t stop;
    mp_int_t step;
} mp_obj_range_t;

const mp_obj_type_t mp_type_range = {
    { &mp_type_type },
    .name = MP_QSTR_range,
    .print = range_print,
    .make_new = range_make_new,
    .unary_op = range_unary_op,
    .subscr = range_subscr,
    .getiter = range_getiter,
#if MICROPY_PY_BUILTINS_RANGE_ATTRS
    .attr = range_attr,
#endif
};
*/
/******************************************************************************/

typedef struct _mp_obj_microthread_t {
    mp_obj_base_t base;
    
    mp_obj_t name;
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
} mp_obj_microthread_t;

const mp_obj_type_t mp_type_microthread = {
    { &mp_type_type },
    .name = MP_QSTR_MicroThread,
// init microthread
/*
    .name = MP_QSTR_function,
    .call = mod_mpoc_pause,
*/
};

mp_obj_microthread_t *mp_current_microthread;
mp_obj_microthread_t *_mp_fallback_microthread;

/* mp_obj_t *mp_new_microthread(const char *thread_name, mp_obj_t fun_bc);

void mp_init_microthread(void);

void mp_load_microthread(mp_microthread_t *microthread);
void mp_store_microthread(mp_microthread_t *microthread);

mp_vm_return_kind_t mp_resume_microthread(mp_microthread_t *microthread); */

#define MP_ENTER_MICROTHREAD(microthread) do { \
    mp_store_microthread(_mp_fallback_microthread); \
    mp_load_microthread((microthread)); \
} while(0)

#define MP_EXIT_MICROTHREAD(microthread) do { \
    mp_store_microthread((microthread)); \
    mp_load_microthread(_mp_fallback_microthread); \
} while(0)

STATIC mp_obj_t mod_microthread_init(void){
    mp_obj_microthread_t *fallback_thread = m_new_obj(mp_obj_microthread_t);

    fallback_thread->base.type = &mp_type_microthread;
    fallback_thread->fun_bc = (mp_obj_t)NULL;
    fallback_thread->code_state = (mp_code_state *)NULL;
    fallback_thread->last_kind = MP_VM_RETURN_NORMAL;
    fallback_thread->dict_locals = MP_STATE_CTX(dict_locals);
    fallback_thread->dict_globals = MP_STATE_CTX(dict_globals);
    fallback_thread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    fallback_thread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    fallback_thread->cpu_current_opcodes_executed = MP_STATE_VM(cpu_current_opcodes_executed);
    
    _mp_fallback_microthread = fallback_thread;
    mp_current_microthread = _mp_fallback_microthread;
    
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_microthread_init_obj, mod_microthread_init);

STATIC mp_obj_t mod_microthread_new_microthread(mp_obj_t thread_name, mp_obj_t fun_bc){
    assert(MP_OBJ_IS_FUN(fun_bc));
    // assert as nlr_raise?
    
    mp_code_state *code_state = mp_obj_fun_bc_prepare_codestate(fun_bc, 0, 0, NULL);
    code_state->current = code_state;
    
    mp_obj_microthread_t *microthread = m_new_obj(mp_obj_microthread_t);
    microthread->base.type = &mp_type_microthread;
    microthread->fun_bc = fun_bc;
    microthread->code_state = code_state;
    microthread->last_kind = MP_VM_RETURN_PAUSE;
    MP_STATE_CTX(dict_locals) = mp_obj_new_dict(0);    
    microthread->dict_globals = MP_STATE_CTX(dict_globals);
    microthread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    microthread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    microthread->cpu_current_opcodes_executed = 0;

    // vstr_clear(vstr_module_name);    
    return microthread;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_2(mod_microthread_new_microthread_obj, mod_microthread_new_microthread);

void mp_load_microthread(mp_obj_microthread_t *microthread){
    mp_current_microthread = microthread;
    MP_STATE_CTX(dict_locals) = microthread->dict_locals;
    MP_STATE_CTX(dict_globals) = microthread->dict_globals;
    MP_STATE_VM(cpu_max_opcodes_executeable) = microthread->cpu_max_opcodes_executeable;
    MP_STATE_VM(cpu_min_opcodes_executeable) = microthread->cpu_min_opcodes_executeable; 
    MP_STATE_VM(cpu_current_opcodes_executed) = microthread->cpu_current_opcodes_executed;
}

void mp_store_microthread(mp_obj_microthread_t *microthread){
    microthread->dict_locals = MP_STATE_CTX(dict_locals);
    microthread->dict_globals = MP_STATE_CTX(dict_globals);
    microthread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    microthread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    microthread->cpu_current_opcodes_executed = microthread->cpu_current_opcodes_executed;
}


mp_obj_t mod_microthread_resume(mp_obj_t microthread_obj){
    mp_obj_microthread_t *microthread = MP_OBJ_CAST(microthread_obj);
    MP_ENTER_MICROTHREAD(microthread);

    mp_code_state *code_state = microthread->code_state;
    mp_vm_return_kind_t kind = mp_resume_bytecode(code_state, code_state->current, MP_OBJ_NULL);
    microthread->last_kind = kind;

    MP_EXIT_MICROTHREAD(microthread);
    
    // TODO: kind to number?
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_microthread_resume_obj, mod_microthread_resume);


/*

run_microthread() ...?
move all microthread thing to here?


*/

STATIC const mp_map_elem_t mp_module_microthread_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_microthread) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_init), (mp_obj_t)&mod_microthread_init_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_new_microthread), (mp_obj_t)&mod_microthread_new_microthread_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_resume), (mp_obj_t)&mod_microthread_resume_obj },

};

STATIC MP_DEFINE_CONST_DICT(mp_module_microthread_globals, mp_module_microthread_globals_table);

const mp_obj_module_t mp_module_microthread = {
    .base = { &mp_type_module },
    .name = MP_QSTR_umicrothread,
    .globals = (mp_obj_dict_t*)&mp_module_microthread_globals,
};
