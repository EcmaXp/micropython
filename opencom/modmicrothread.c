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
#include "py/objint.h"
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
    
    // name
    mp_obj_t name;
    
    // function
    mp_obj_t fun_bc;
    
    // code_state
    mp_code_state *code_state;
    
    // send_value
    mp_obj_t send_value;
    
    // last_kind
    mp_vm_return_kind_t last_kind;

    // last_result
    mp_obj_t last_result;

    mp_obj_dict_t *dict_locals;
    mp_obj_dict_t *dict_globals;

    #if MICROPY_LIMIT_CPU
    mp_uint_t cpu_max_opcodes_executeable;
    mp_uint_t cpu_min_opcodes_executeable;
    mp_uint_t cpu_current_opcodes_executed;
    #endif
} mp_obj_microthread_t;

mp_obj_microthread_t *mp_current_microthread;
mp_obj_microthread_t *mp_fallback_microthread;

const mp_obj_type_t mp_type_microthread;

#define MP_ENTER_MICROTHREAD(microthread) do { \
    mp_store_microthread(mp_fallback_microthread); \
    mp_load_microthread((microthread)); \
} while(0)

#define MP_EXIT_MICROTHREAD(microthread) do { \
    mp_store_microthread((microthread)); \
    mp_load_microthread(mp_fallback_microthread); \
} while(0)

STATIC mp_obj_t mod_microthread_init(void) {
    mp_obj_microthread_t *fallback_thread = m_new_obj(mp_obj_microthread_t);

    fallback_thread->base.type = &mp_type_microthread;
    fallback_thread->name = (mp_obj_t)NULL;
    fallback_thread->fun_bc = (mp_obj_t)NULL;
    fallback_thread->code_state = (mp_code_state *)NULL;
    fallback_thread->send_value = mp_const_none;
    fallback_thread->last_kind = MP_VM_RETURN_NORMAL;
    fallback_thread->last_result = mp_const_none;
    fallback_thread->dict_locals = MP_STATE_CTX(dict_locals);
    fallback_thread->dict_globals = MP_STATE_CTX(dict_globals);
#if MICROPY_LIMIT_CPU
    fallback_thread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    fallback_thread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    fallback_thread->cpu_current_opcodes_executed = MP_STATE_VM(cpu_current_opcodes_executed);
#endif
    
    mp_fallback_microthread = fallback_thread;
    mp_current_microthread = mp_fallback_microthread;
    
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_microthread_init_obj, mod_microthread_init);

STATIC mp_obj_t microthread_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, 2, false);
    
    if (mp_fallback_microthread == NULL) {
        mod_microthread_init();
    }
    
    mp_obj_t name_obj = args[0];
    mp_obj_t fun_bc_obj = args[1];
    
    mp_obj_fun_bc_t *fun_bc = MP_OBJ_CAST(fun_bc_obj);
    
    assert(MP_OBJ_IS_FUN(fun_bc_obj));
    // assert as nlr_raise?
    
    mp_code_state *code_state = mp_obj_fun_bc_prepare_codestate(fun_bc_obj, 0, 0, NULL);
    code_state->current = code_state;
    
    mp_obj_microthread_t *thread = m_new_obj(mp_obj_microthread_t);
    thread->base.type = &mp_type_microthread;
    thread->name = name_obj;
    thread->fun_bc = fun_bc_obj;
    thread->code_state = code_state;
    thread->send_value = mp_const_none;
    thread->last_kind = MP_VM_RETURN_FORCE_PAUSE;
    thread->last_result = mp_const_none;
    thread->dict_locals = mp_obj_new_dict(0);    
    thread->dict_globals = fun_bc->globals;
#if MICROPY_LIMIT_CPU
    thread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    thread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    thread->cpu_current_opcodes_executed = 0;
#endif

    return thread;
}

void mp_load_microthread(mp_obj_microthread_t *microthread) {
    mp_current_microthread = microthread;
    MP_STATE_CTX(dict_locals) = microthread->dict_locals;
    MP_STATE_CTX(dict_globals) = microthread->dict_globals;
#if MICROPY_LIMIT_CPU
    MP_STATE_VM(cpu_max_opcodes_executeable) = microthread->cpu_max_opcodes_executeable;
    MP_STATE_VM(cpu_min_opcodes_executeable) = microthread->cpu_min_opcodes_executeable; 
    MP_STATE_VM(cpu_current_opcodes_executed) = microthread->cpu_current_opcodes_executed;
#endif
}

void mp_store_microthread(mp_obj_microthread_t *microthread) {
    microthread->dict_locals = MP_STATE_CTX(dict_locals);
    microthread->dict_globals = MP_STATE_CTX(dict_globals);
#if MICROPY_LIMIT_CPU
    microthread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    microthread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    microthread->cpu_current_opcodes_executed = MP_STATE_VM(cpu_current_opcodes_executed);
#endif
}


mp_obj_t mod_microthread_resume(mp_obj_t microthread_obj) {
    mp_obj_microthread_t *thread = MP_OBJ_CAST(microthread_obj);

    mp_code_state *code_state = thread->code_state;
    mp_vm_return_kind_t kind = thread->last_kind;

    MP_ENTER_MICROTHREAD(thread);
    
    if (kind == MP_VM_RETURN_PAUSE){
        code_state->current->sp[0] = thread->send_value;
        thread->send_value = mp_const_none;
    }
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0){
        kind = mp_resume_bytecode(code_state, code_state->current, MP_OBJ_NULL);
        nlr_pop();
    } else {
        kind = MP_VM_RETURN_EXCEPTION;
        code_state->state[0] = &nlr.ret_val;
    }

    MP_EXIT_MICROTHREAD(thread);

    assert(code_state->n_state > 0);
    thread->last_kind = kind;

    switch (kind){
        case MP_VM_RETURN_NORMAL:
            thread->last_result = (mp_obj_t)code_state->current->sp[0];
            break;
        case MP_VM_RETURN_YIELD:
            // TODO: get yielded value
            thread->last_result = mp_const_none;
            break;
        case MP_VM_RETURN_EXCEPTION:
            printf("hello2\n");
            thread->last_result = (mp_obj_t)code_state->current->state[0];
            break;
        case MP_VM_RETURN_PAUSE:
        case MP_VM_RETURN_FORCE_PAUSE:
        default:
            thread->last_result = mp_const_none;
    }
    
    // TODO: kind to number?
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_microthread_resume_obj, mod_microthread_resume);

STATIC void microthread_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_obj_microthread_t *self = MP_OBJ_CAST(self_in);

    assert(kind == PRINT_REPR || kind == PRINT_STR);
    
    mp_printf(print, "<%q name=", self->base.type->name);
    mp_obj_print_helper(print, self->name, kind);
    mp_print_str(print, ", function=");
    mp_obj_print_helper(print, self->fun_bc, kind);
    mp_print_str(print, ">");
}

/*

run_microthread() ...?
move all microthread thing to here?


*/

/* STATIC const mp_map_elem_t microthread_locals_dict_table[] = {
    // { MP_OBJ_NEW_QSTR(MP_QSTR_clear), (mp_obj_t)&dict_clear_obj },
};

STATIC MP_DEFINE_CONST_DICT(microthread_locals_dict, microthread_locals_dict_table);
*/

STATIC void microthread_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_microthread_t *thread = MP_OBJ_CAST(self_in);
    
    bool is_load = (dest[0] == MP_OBJ_NULL);
    bool is_store = (dest[0] == MP_OBJ_SENTINEL && dest[1] != MP_OBJ_NULL);
    // bool is_del = (dest[0] == MP_OBJ_SENTINEL && dest[1] == MP_OBJ_NULL);
    
    if (attr == MP_QSTR_name && is_load) {
        dest[0] = thread->name;
        goto LOAD_OK;
    }
    if (attr == MP_QSTR_function && is_load) {
        dest[0] = thread->fun_bc;
        goto LOAD_OK;
    }
    if (attr == MP_QSTR_last_kind && is_load) {
        dest[0] = mp_obj_new_int((mp_int_t)thread->last_kind);
        goto LOAD_OK;
    }
    if (attr == MP_QSTR_last_result && is_load) {
        dest[0] = thread->last_result;
        goto LOAD_OK;
    }
    if (attr == MP_QSTR_send_value) {
        if (is_load) {
            dest[0] = thread->send_value;
            goto LOAD_OK;
        } else if (is_store) {
            thread->send_value = dest[1];
            goto STORE_OK;
        }
    }
#if MICROPY_LIMIT_CPU
    if (attr == MP_QSTR_cpu_hard_limit) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->cpu_max_opcodes_executeable);
            return;
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->cpu_max_opcodes_executeable = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        }
    }
    if (attr == MP_QSTR_cpu_soft_limit) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->cpu_min_opcodes_executeable);
        } else {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->cpu_min_opcodes_executeable = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        }
    }
    if (attr == MP_QSTR_cpu_current_executed) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->cpu_current_opcodes_executed);
            goto LOAD_OK;
        } else {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->cpu_current_opcodes_executed = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        }
    }

    return;
#endif // MICROPY_LIMIT_CPU

LOAD_OK:
    return;
    
STORE_OK:
    dest[0] = MP_OBJ_NULL;
    return;
    
FAIL_NOT_INT:
    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError, "that is not int. (TODO: fix this message)"));

}

/*
struct _mp_obj_type_t {
    mp_obj_base_t base;
    qstr name;
    mp_print_fun_t print;
    mp_make_new_fun_t make_new;     // to make an instance of the type

    mp_call_fun_t call;
    mp_unary_op_fun_t unary_op;     // can return MP_OBJ_NULL if op not supported
    mp_binary_op_fun_t binary_op;   // can return MP_OBJ_NULL if op not supported

    // implements load, store and delete attribute
    //
    // dest[0] = MP_OBJ_NULL means load
    //  return: for fail, do nothing
    //          for attr, dest[0] = value
    //          for method, dest[0] = method, dest[1] = self
    //
    // dest[0,1] = {MP_OBJ_SENTINEL, MP_OBJ_NULL} means delete
    // dest[0,1] = {MP_OBJ_SENTINEL, object} means store
    //  return: for fail, do nothing
    //          for success set dest[0] = MP_OBJ_NULL
    mp_attr_fun_t attr;

    mp_subscr_fun_t subscr;         // implements load, store, delete subscripting
                                    // value=MP_OBJ_NULL means delete, value=MP_OBJ_SENTINEL means load, else store
                                    // can return MP_OBJ_NULL if op not supported

    mp_fun_1_t getiter;             // corresponds to __iter__ special method
    mp_fun_1_t iternext; // may return MP_OBJ_STOP_ITERATION as an optimisation instead of raising StopIteration() (with no args)

    mp_buffer_p_t buffer_p;
    const mp_stream_p_t *stream_p;

    // these are for dynamically created types (classes)
    mp_obj_t bases_tuple;
    mp_obj_t locals_dict;

    What we might need to add here:

    len             str tuple list map
    abs             float complex
    hash            bool int none str
    equal           int str

    unpack seq      list tuple
};
*/

const mp_obj_type_t mp_type_microthread = {
    { &mp_type_type },
    .name = MP_QSTR_MicroThread,
    .make_new = microthread_make_new,
    .print = microthread_print,
    .attr = microthread_attr,
    // .locals_dict = microthread_locals_dict,
};



STATIC const mp_map_elem_t mp_module_microthread_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_microthread) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_init), (mp_obj_t)&mod_microthread_init_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_resume), (mp_obj_t)&mod_microthread_resume_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_MicroThread), (mp_obj_t)&mp_type_microthread },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_microthread_globals, mp_module_microthread_globals_table);

const mp_obj_module_t mp_module_microthread = {
    .base = { &mp_type_module },
    .name = MP_QSTR_umicrothread,
    .globals = (mp_obj_dict_t*)&mp_module_microthread_globals,
};
