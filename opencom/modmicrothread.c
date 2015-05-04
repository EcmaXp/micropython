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

// Module Name: umicrothread
// This module will control microthread.

#include <stdio.h>
#include <string.h>

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objfun.h"
#include "py/objint.h"
#include "py/objtuple.h"
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

typedef struct _mp_microthread_context_t {
    mp_obj_dict_t *dict_locals;
    mp_obj_dict_t *dict_globals;

    // context.vm
    // map with loaded modules
    // mp_map_t mp_loaded_modules_map;

    #if MICROPY_PY_SYS_EXC_INFO
    // mp_obj_t cur_exception;
    #endif

    // unused
    // mp_obj_dict_t dict_main;

    // dictionary for overridden builtins
    #if MICROPY_CAN_OVERRIDE_BUILTINS
    // mp_obj_dict_t *mp_module_builtins_override_dict;
    #endif

    #if MICROPY_LIMIT_CPU
    mp_uint_t cpu_max_opcodes_executeable;
    mp_uint_t cpu_min_opcodes_executeable;
    mp_uint_t cpu_current_opcodes_executed;
    #endif    
} mp_microthread_context_t;

typedef enum _mp_microthread_status_type {
    MP_MICROTHREAD_STATUS_READY = 1,
    MP_MICROTHREAD_STATUS_RUNNING = 2,
    MP_MICROTHREAD_STATUS_STOP = 3,
    MP_MICROTHREAD_STATUS_YIELD = 4,
    MP_MICROTHREAD_STATUS_SOFT_PAUSE = 5,
    MP_MICROTHREAD_STATUS_HARD_PAUSE = 6,
} mp_microthread_status_type;

typedef struct _mp_obj_microthread_t {
    mp_obj_base_t base;

    mp_obj_t name;
    mp_obj_t fun_bc;

    mp_obj_t last_result;

    mp_code_state *code_state;
    mp_microthread_status_type status;
    mp_microthread_context_t context;
} mp_obj_microthread_t;

STATIC mp_obj_microthread_t *mp_current_microthread;
STATIC mp_obj_microthread_t mp_fallback_microthread;

const mp_obj_type_t mp_type_microthread;

#define MP_ENTER_MICROTHREAD(microthread) do { \
    mp_store_microthread(&mp_fallback_microthread); \
    mp_load_microthread((microthread)); \
} while(0)

#define MP_EXIT_MICROTHREAD(microthread) do { \
    mp_store_microthread((microthread)); \
    mp_load_microthread(&mp_fallback_microthread); \
} while(0)

STATIC mp_obj_t mod_microthread_init(void) {
    mp_obj_microthread_t *thread = &mp_fallback_microthread;
    
    thread->base.type = &mp_type_microthread;

    thread->name = (mp_obj_t)NULL;
    thread->fun_bc = (mp_obj_t)NULL;
    thread->last_result = mp_const_none;

    thread->code_state = (mp_code_state *)NULL;

    thread->status = MP_MICROTHREAD_STATUS_RUNNING;
    
    thread->context.dict_locals = MP_STATE_CTX(dict_locals);
    thread->context.dict_globals = MP_STATE_CTX(dict_globals);
#if MICROPY_LIMIT_CPU
    thread->context.cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    thread->context.cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    thread->context.cpu_current_opcodes_executed = MP_STATE_VM(cpu_current_opcodes_executed);
#endif
    
    mp_current_microthread = &mp_fallback_microthread;
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_microthread_init_obj, mod_microthread_init);

STATIC mp_obj_t microthread_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, n_args, true);
    
    if (mp_fallback_microthread.base.type == MP_OBJ_NULL) {
        mod_microthread_init();
    }
    
    mp_obj_t name_obj = args[0];
    mp_obj_t fun_bc_obj = args[1];
    
    mp_obj_fun_bc_t *fun_bc = MP_OBJ_CAST(fun_bc_obj);
    
    assert(MP_OBJ_IS_FUN(fun_bc_obj));
    // assert as nlr_raise?
    
    mp_code_state *code_state = mp_obj_fun_bc_prepare_codestate(fun_bc_obj, n_args - 2, n_kw, &args[2]);
    code_state->current = code_state;
    
    mp_obj_microthread_t *thread = m_new_obj(mp_obj_microthread_t);
    thread->base.type = &mp_type_microthread;

    thread->name = name_obj;
    thread->fun_bc = fun_bc_obj;
    thread->last_result = mp_const_none;

    thread->code_state = code_state;
    
    thread->status = MP_MICROTHREAD_STATUS_READY;

    thread->context.dict_locals = mp_obj_new_dict(0);    
    thread->context.dict_globals = fun_bc->globals;
#if MICROPY_LIMIT_CPU
    thread->context.cpu_max_opcodes_executeable = 0;
    thread->context.cpu_min_opcodes_executeable = 0;
    thread->context.cpu_current_opcodes_executed = 0;
#endif

    return thread;
}

void mp_load_microthread(mp_obj_microthread_t *thread) {
    thread->status = MP_MICROTHREAD_STATUS_RUNNING;
    mp_current_microthread = thread;

    MP_STATE_CTX(dict_locals) = thread->context.dict_locals;
    MP_STATE_CTX(dict_globals) = thread->context.dict_globals;

#if MICROPY_LIMIT_CPU
    MP_STATE_VM(cpu_max_opcodes_executeable) = thread->context.cpu_max_opcodes_executeable;
    MP_STATE_VM(cpu_min_opcodes_executeable) = thread->context.cpu_min_opcodes_executeable; 
    MP_STATE_VM(cpu_current_opcodes_executed) = thread->context.cpu_current_opcodes_executed;
#endif

}

void mp_store_microthread(mp_obj_microthread_t *thread) {
    thread->status = MP_MICROTHREAD_STATUS_STOP;
    
    thread->context.dict_locals = MP_STATE_CTX(dict_locals);
    thread->context.dict_globals = MP_STATE_CTX(dict_globals);
    
#if MICROPY_LIMIT_CPU
    thread->context.cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    thread->context.cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    thread->context.cpu_current_opcodes_executed = MP_STATE_VM(cpu_current_opcodes_executed);
#endif
}


STATIC mp_obj_t microthread_attr_resume(mp_uint_t n_args, mp_obj_t args[]) {
    mp_obj_t microthread_obj = args[0];
    mp_obj_t send_value = n_args == 2? args[1]: mp_const_none;

    mp_obj_microthread_t *thread = MP_OBJ_CAST(microthread_obj);
    mp_code_state *code_state = thread->code_state;
    thread->last_result = mp_const_none;
    
    // TODO: how to throw error?
    
    if (thread->status == MP_MICROTHREAD_STATUS_SOFT_PAUSE) {
        code_state->current->sp[0] = send_value;
    } else if (thread->status == MP_MICROTHREAD_STATUS_STOP) {
        mp_obj_t *items = m_new(mp_obj_t, 2);
        items[0] = MP_OBJ_NEW_QSTR(MP_QSTR_stop);
        items[1] = mp_const_none;
        return mp_obj_new_tuple(2, items);
    }

    MP_ENTER_MICROTHREAD(thread);
    mp_vm_return_kind_t kind;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0){
        kind = mp_resume_bytecode(code_state, code_state->current, MP_OBJ_NULL);
        nlr_pop();
    } else {
        kind = MP_VM_RETURN_EXCEPTION;
        code_state->current->state[code_state->current->n_state - 1] = &nlr.ret_val;
    }

    MP_EXIT_MICROTHREAD(thread);
    assert(code_state->n_state > 0);

    qstr return_status_qstr;
    switch (kind){
        case MP_VM_RETURN_NORMAL:
            return_status_qstr = MP_QSTR_normal;
            thread->status = MP_MICROTHREAD_STATUS_STOP;
            thread->last_result = (mp_obj_t)code_state->current->sp[0];
            break;
        case MP_VM_RETURN_YIELD:
            return_status_qstr = MP_QSTR_yield;
            // TODO: get yielded value
            thread->status = MP_MICROTHREAD_STATUS_YIELD;
            thread->last_result = mp_const_none;
            break;
        case MP_VM_RETURN_EXCEPTION:
            return_status_qstr = MP_QSTR_exception;
            thread->status = MP_MICROTHREAD_STATUS_STOP;
            thread->last_result = (mp_obj_t)code_state->current->state[code_state->current->n_state - 1];
            
            if (mp_obj_exception_match(thread->last_result, &mp_type_SystemHardLimit)) {
                return_status_qstr = MP_QSTR_limit;
                thread->last_result = MP_OBJ_NEW_QSTR(MP_QSTR_limit_hard);
            } else if (mp_obj_exception_match(thread->last_result, &mp_type_SystemSoftLimit)) {
                return_status_qstr = MP_QSTR_limit;
                thread->last_result = MP_OBJ_NEW_QSTR(MP_QSTR_limit_soft);
            } else if (0) {
                // TODO: handle can't pauseable.
            } else {
                return_status_qstr = MP_QSTR_exception;
                // TODO: get traceback?
                // mp_obj_exception_get_traceback
            }
            
            break;
        case MP_VM_RETURN_PAUSE:
            return_status_qstr = MP_QSTR_pause;
            thread->status = MP_MICROTHREAD_STATUS_SOFT_PAUSE;
            thread->last_result = thread->last_result;
            break;
        case MP_VM_RETURN_FORCE_PAUSE:
            return_status_qstr = MP_QSTR_force_pause;
            thread->status = MP_MICROTHREAD_STATUS_HARD_PAUSE;
            thread->last_result = mp_const_none;
            break;
        default:
            assert(0);
    }
    
    mp_obj_t *items = m_new(mp_obj_t, 2);
    items[0] = MP_OBJ_NEW_QSTR(return_status_qstr);
    items[1] = thread->last_result;
    
    thread->last_result = mp_const_none;
    
    return mp_obj_new_tuple(2, items);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(microthread_attr_resume_obj, 1, 2, microthread_attr_resume);

STATIC mp_obj_t microthread_attr_del(mp_obj_t microthread_obj) {
    printf("killed\n");
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(microthread_attr_del_obj, microthread_attr_del);

STATIC mp_obj_t mod_mpoc_pause(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    
    if (1 <= n_args) {
        mp_current_microthread->last_result = args[0];
    }
    
    return MP_OBJ_PAUSE_VM;
}

typedef struct _mp_type_fun_pause_t {
    mp_obj_base_t base;
} mp_type_fun_pause_t;

const mp_obj_type_t mp_type_fun_pause = {
    { &mp_type_type },
    .name = MP_QSTR_function,
    .call = mod_mpoc_pause,
};

STATIC const mp_type_fun_pause_t mod_microthread_pause_obj = {{&mp_type_fun_pause}};

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
    bool is_del = (dest[0] == MP_OBJ_SENTINEL && dest[1] == MP_OBJ_NULL);
    
    if (attr == MP_QSTR_resume && is_load) {
        dest[0] = (mp_obj_t)&microthread_attr_resume_obj;
        goto METHOD_OK;
    }
    if (attr == MP_QSTR_name && is_load) {
        dest[0] = thread->name;
        goto LOAD_OK;
    }
    if (attr == MP_QSTR_function && is_load) {
        dest[0] = thread->fun_bc;
        goto LOAD_OK;
    }
#if MICROPY_LIMIT_CPU
    if (attr == MP_QSTR_cpu_hard_limit) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->context.cpu_max_opcodes_executeable);
            return;
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->context.cpu_max_opcodes_executeable = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        } else if (is_del) {
            thread->context.cpu_max_opcodes_executeable = 0;
            goto DEL_OK;
        }
    }
    if (attr == MP_QSTR_cpu_soft_limit) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->context.cpu_min_opcodes_executeable);
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->context.cpu_min_opcodes_executeable = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        } else if (is_del) {
            thread->context.cpu_min_opcodes_executeable = 0;
            goto DEL_OK;
        }
    }
    if (attr == MP_QSTR_cpu_current_executed) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->context.cpu_current_opcodes_executed);
            goto LOAD_OK;
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->context.cpu_current_opcodes_executed = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        } else if (is_del) {
            thread->context.cpu_current_opcodes_executed = 0;
            goto DEL_OK;
        }
    }

    return;
#endif // MICROPY_LIMIT_CPU

LOAD_OK:
    return;
    
STORE_OK:
    dest[0] = MP_OBJ_NULL;
    return;

METHOD_OK:
    dest[1] = self_in;
    return;

DEL_OK:
    // TODO: write DEL_OK
    assert(0);
    return;

#if MICROPY_LIMIT_CPU
FAIL_NOT_INT:
    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError, "that is not int. (TODO: fix this message)"));
#endif

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
    .name = MP_QSTR__MicroThread,
    .make_new = microthread_make_new,
    .print = microthread_print,
    .attr = microthread_attr,
    // .locals_dict = microthread_locals_dict,
};



STATIC const mp_map_elem_t mp_module_microthread_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_umicrothread) },
    { MP_OBJ_NEW_QSTR(MP_QSTR__init), (mp_obj_t)&mod_microthread_init_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_MicroThread), (mp_obj_t)&mp_type_microthread },
    { MP_OBJ_NEW_QSTR(MP_QSTR_pause), (mp_obj_t)&mod_microthread_pause_obj },

#define C(x, y) { MP_OBJ_NEW_QSTR(MP_QSTR_##x), MP_OBJ_NEW_QSTR(MP_QSTR_##y) }
    C(STATUS_NORMAL, normal),
    C(STATUS_YIELD, yield),
    C(STATUS_EXCEPTION, exception),
    C(STATUS_LIMIT, limit),
    C(STATUS_PAUSE, pause),
    C(STATUS_FORCE_PAUSE, force_pause),
    C(STATUS_STOP, stop),
    C(LIMIT_SOFT, limit_soft),
    C(LIMIT_HARD, limit_hard),
#undef C
};

STATIC MP_DEFINE_CONST_DICT(mp_module_microthread_globals, mp_module_microthread_globals_table);

const mp_obj_module_t mp_module_microthread = {
    .base = { &mp_type_module },
    .name = MP_QSTR_umicrothread,
    .globals = (mp_obj_dict_t*)&mp_module_microthread_globals,
};
