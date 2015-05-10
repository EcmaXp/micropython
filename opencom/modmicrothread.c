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

#include <stdio.h>
#include <string.h>

#include "modmicrothread.h"
#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objfun.h"
#include "py/objint.h"
#include "py/objtuple.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/bc.h"

#if MICROPY_STACKLESS_EXTRA
#include "py/objclosure.h"
#include "py/objboundmeth.h"
#endif

#if !MICROPY_STACKLESS
#error This module require stackless features.
#endif

// it must be not pointer because gc trying collect value.
STATIC mp_obj_microthread_t mp_fallback_microthread;

STATIC mp_obj_microthread_t *mp_current_microthread;

STATIC mp_obj_t mod_microthread_init(void) {
    mp_obj_microthread_t *thread = &mp_fallback_microthread;

    thread->base.type = &mp_type_microthread;

    thread->name = MP_OBJ_NULL;
    thread->fun = MP_OBJ_NULL;
    thread->last_result = mp_const_none;

    thread->code_state = (mp_code_state *)NULL;

    thread->status = MP_MICROTHREAD_STATUS_RUNNING;

#define STATE(t, a) thread->context.a = MP_STATE_##t(a);

    STATE(CTX, dict_locals);
    STATE(CTX, dict_globals);

    STATE(VM, mp_loaded_modules_map);
    STATE(VM, cur_exception);
    STATE(VM, mp_module_builtins_override_dict);

    #if MICROPY_LIMIT_CPU
    STATE(VM, cpu_hard_limit);
    STATE(VM, cpu_soft_limit);
    STATE(VM, cpu_safe_limit);
    STATE(VM, cpu_current_executed);
    STATE(VM, cpu_soft_limit_executed);
    #endif

#undef STATE

    mp_current_microthread = &mp_fallback_microthread;
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_microthread_init_obj, mod_microthread_init);

STATIC mp_obj_t microthread_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    if (mp_fallback_microthread.base.type == MP_OBJ_NULL) {
        mod_microthread_init();
    }

    mp_arg_check_num(n_args, n_kw, 2, n_args, true);

    mp_obj_t name_obj = args[0];
    mp_obj_t fun_obj = args[1];

    mp_obj_t global_dict = MP_OBJ_NULL;
    mp_obj_t real_fun_obj = fun_obj;

    #if MICROPY_STACKLESS_EXTRA
    mp_flatcall_fun_t flatcall = NULL;
    if (mp_obj_get_type(fun_obj)->flatcall != NULL) {
        flatcall = mp_obj_get_type(fun_obj)->flatcall;
    } else {
        real_fun_obj = MP_OBJ_NULL;
    }
    #else
    #define flatcall mp_obj_fun_prepare_codestate
    #endif

    // getting real global object.
    while (real_fun_obj != MP_OBJ_NULL) {
        if (MP_OBJ_IS_TYPE(real_fun_obj, &mp_type_fun_bc)) {
            mp_obj_fun_bc_t *fun_bc = real_fun_obj;
            global_dict = fun_bc->globals;
            break;
        #if MICROPY_STACKLESS_EXTRA
        } else if (MP_OBJ_IS_TYPE(real_fun_obj, &mp_type_closure)) {
            mp_obj_closure_t *closure = real_fun_obj;
            real_fun_obj = closure->fun;
        } else if (MP_OBJ_IS_TYPE(real_fun_obj, &mp_type_bound_meth)) {
            // TODO: handle mp_type_bound_meth
            mp_obj_bound_meth_t *bound_meth = real_fun_obj;
            real_fun_obj = bound_meth->meth;
        } else if (MP_OBJ_IS_TYPE(real_fun_obj, &mp_type_fun_bc)) {
            break;
        #else // MICROPY_STACKLESS_EXTRA
        } else {
            real_fun_obj = MP_OBJ_NULL;
        #endif
        }
    }

    if (real_fun_obj == MP_OBJ_NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "function is not pauseable."));
    }

    mp_code_state *code_state = flatcall(fun_obj, n_args - 2, n_kw, &args[2]);
    code_state->current = code_state;

    mp_obj_microthread_t *thread = m_new_obj(mp_obj_microthread_t);
    thread->base.type = &mp_type_microthread;

    thread->name = name_obj;
    thread->fun = fun_obj;
    thread->last_result = mp_const_none;

    thread->code_state = code_state;

    thread->status = MP_MICROTHREAD_STATUS_READY;

#define STATE(t, a, b) thread->context.a = (b);
#define STATE_FROM(t, a) thread->context.a = MP_STATE_##t(a);

    STATE(CTX, dict_locals, mp_obj_new_dict(0));
    STATE(CTX, dict_globals, global_dict);

    STATE_FROM(VM, mp_loaded_modules_map);
    STATE(VM, cur_exception, MP_OBJ_NULL);
    STATE_FROM(VM, mp_module_builtins_override_dict);

    #if MICROPY_LIMIT_CPU
    STATE(VM, cpu_hard_limit, 0);
    STATE(VM, cpu_soft_limit, 0);
    STATE(VM, cpu_safe_limit, 0);
    STATE(VM, cpu_current_executed, 0);
    STATE(VM, cpu_soft_limit_executed, 0);
    #endif

#undef STATE
#undef STATE_FROM

#if !MICROPY_STACKLESS_EXTRA
#undef flatcall
#endif

    return thread;
}

void mp_load_microthread_context(mp_microthread_context_t *context) {
#define STATE(t, a) MP_STATE_##t(a) = context->a;

    STATE(CTX, dict_locals);
    STATE(CTX, dict_globals);

    STATE(VM, mp_loaded_modules_map);
    STATE(VM, cur_exception);
    STATE(VM, mp_module_builtins_override_dict);

    #if MICROPY_LIMIT_CPU
    STATE(VM, cpu_hard_limit);
    STATE(VM, cpu_soft_limit);
    STATE(VM, cpu_safe_limit);
    STATE(VM, cpu_current_executed);
    STATE(VM, cpu_soft_limit_executed);
    #endif

#undef STATE
}

void mp_store_microthread_context(mp_microthread_context_t *context) {
#define STATE(t, a) context->a = MP_STATE_##t(a);

    STATE(CTX, dict_locals);
    STATE(CTX, dict_globals);

    STATE(VM, mp_loaded_modules_map);
    STATE(VM, cur_exception);
    STATE(VM, mp_module_builtins_override_dict);

    #if MICROPY_LIMIT_CPU
    STATE(VM, cpu_hard_limit);
    STATE(VM, cpu_soft_limit);
    STATE(VM, cpu_safe_limit);
    STATE(VM, cpu_current_executed);
    STATE(VM, cpu_soft_limit_executed);
    #endif

#undef STATE
}


STATIC mp_obj_t microthread_attr_resume(mp_uint_t n_args, mp_obj_t args[]) {
    mp_obj_t microthread_obj = args[0];
    mp_obj_t send_value = n_args == 2? args[1]: mp_const_none;

    mp_obj_microthread_t *thread = microthread_obj;
    mp_code_state *code_state = thread->code_state;

    thread->last_result = mp_const_none;

    // TODO: how to throw error?

    if (thread->status == MP_MICROTHREAD_STATUS_SOFT_PAUSE) {
        code_state->current->sp[0] = send_value;
    } else if (thread->status == MP_MICROTHREAD_STATUS_STOP) {
        // nlr_raise(mp_obj_new_exception_msg(&mp_type_RuntimeError, "already thread are stopped."))
        // TODO: raise error? or just return stop?
        mp_obj_t *items = m_new(mp_obj_t, 2);
        items[0] = MP_OBJ_NEW_QSTR(MP_QSTR_stop);
        items[1] = mp_const_none;
        return mp_obj_new_tuple(2, items);
    }

    if (mp_fallback_microthread.base.type == MP_OBJ_NULL) {
        mod_microthread_init();
    }

    mp_obj_microthread_t *prev_thread = mp_current_microthread;
    mp_microthread_context_t prev_context;

    mp_store_microthread_context(&prev_context);
    mp_current_microthread = thread;
    mp_load_microthread_context(&thread->context);

    mp_vm_return_kind_t kind;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0){
        assert(code_state->n_state > 0);
        kind = mp_resume_bytecode(code_state, code_state->current, MP_OBJ_NULL);
        nlr_pop();
    } else {
        kind = MP_VM_RETURN_EXCEPTION;
        code_state->current->state[code_state->current->n_state - 1] = &nlr.ret_val;
    }

    mp_store_microthread_context(&thread->context);
    mp_current_microthread = prev_thread;
    mp_load_microthread_context(&prev_context);

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
    mp_obj_microthread_t *self = self_in;

    assert(kind == PRINT_REPR || kind == PRINT_STR);

    mp_printf(print, "<%q name=", self->base.type->name);
    mp_obj_print_helper(print, self->name, kind);
    mp_print_str(print, ", function=");
    mp_obj_print_helper(print, self->fun, kind);
    mp_print_str(print, ">");
}

STATIC void microthread_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_microthread_t *thread = self_in;

    bool is_load = (dest[0] == MP_OBJ_NULL);
    bool is_store = (dest[0] == MP_OBJ_SENTINEL && dest[1] != MP_OBJ_NULL);
    bool is_del = (dest[0] == MP_OBJ_SENTINEL && dest[1] == MP_OBJ_NULL);

    if (attr == MP_QSTR_resume && is_load) {
        dest[0] = (mp_obj_t)&microthread_attr_resume_obj;
        goto METHOD_OK;
    }
    // TODO: throw will be here? (error throw)
    // TODO: spawn_allowed? (Default: spawn allowed.)
    if (attr == MP_QSTR_name && is_load) {
        dest[0] = thread->name;
        goto LOAD_OK;
    }
    if (attr == MP_QSTR_function && is_load) {
        dest[0] = thread->fun;
        goto LOAD_OK;
    }
#if MICROPY_LIMIT_CPU
    if (attr == MP_QSTR_cpu_hard_limit) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->context.cpu_hard_limit);
            return;
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->context.cpu_hard_limit = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        } else if (is_del) {
            thread->context.cpu_hard_limit = 0;
            goto DEL_OK;
        }
    }
    if (attr == MP_QSTR_cpu_soft_limit) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->context.cpu_soft_limit);
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->context.cpu_soft_limit = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        } else if (is_del) {
            thread->context.cpu_soft_limit = 0;
            goto DEL_OK;
        }
    }
    if (attr == MP_QSTR_cpu_safe_limit) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->context.cpu_safe_limit);
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->context.cpu_safe_limit = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        } else if (is_del) {
            thread->context.cpu_safe_limit = 0;
            goto DEL_OK;
        }
    }
    if (attr == MP_QSTR_cpu_current_executed) {
        if (is_load) {
            dest[0] = mp_obj_new_int_from_uint(thread->context.cpu_current_executed);
            goto LOAD_OK;
        } else if (is_store) {
            if (!MP_OBJ_IS_INT(dest[1])) {
                goto FAIL_NOT_INT;
            }
            thread->context.cpu_current_executed = (mp_uint_t)mp_obj_int_get_truncated(dest[1]);
            goto STORE_OK;
        } else if (is_del) {
            thread->context.cpu_current_executed = 0;
            goto DEL_OK;
        }
    }
#endif // MICROPY_LIMIT_CPU

    return;

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

const mp_obj_type_t mp_type_microthread = {
    { &mp_type_type },
    .name = MP_QSTR__MicroThread,
    .make_new = microthread_make_new,
    .print = microthread_print,
    .attr = microthread_attr,
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
