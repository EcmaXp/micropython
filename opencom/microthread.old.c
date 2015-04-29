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

#include <stdio.h>
#include <string.h>

#include "microthread.h"
#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objfun.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/bc.h"

mp_microthread_t *mp_new_microthread(const char *thread_name, mp_obj_t fun_bc){
    assert(MP_OBJ_IS_FUN(fun_bc));
    
    mp_code_state *code_state = mp_obj_fun_bc_prepare_codestate(fun_bc, 0, 0, NULL);
    code_state->current = code_state;
    
    vstr_t *vstr_module_name = vstr_new();
    vstr_printf(vstr_module_name, "_microthread_%s", thread_name);
    qstr qstr_module_name = qstr_from_str(vstr_null_terminated_str(vstr_module_name));

    mp_obj_t module_obj = mp_obj_new_module(qstr_module_name);
    mp_obj_module_t *module = (mp_obj_module_t *)module_obj;

    mp_module_register(qstr_module_name, module_obj);
    
    mp_microthread_t *microthread = m_new_obj(mp_microthread_t);
    
    microthread->module = module_obj;
    microthread->fun_bc = fun_bc;
    microthread->code_state = code_state;
    microthread->last_kind = MP_VM_RETURN_PAUSE;
    microthread->dict_locals = module->globals;
    microthread->dict_globals = module->globals;
    microthread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    microthread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    microthread->cpu_current_opcodes_executed = 0;

    // vstr_clear(vstr_module_name);    
    return microthread;
}

void mp_init_microthread(void){
    mp_microthread_t *fallback_thread = m_new_obj(mp_microthread_t);

    fallback_thread->module = (mp_obj_t)NULL;    
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
}

void mp_load_microthread(mp_microthread_t *microthread){
    mp_current_microthread = microthread;
    MP_STATE_CTX(dict_locals) = microthread->dict_locals;
    MP_STATE_CTX(dict_globals) = microthread->dict_globals;
    MP_STATE_VM(cpu_max_opcodes_executeable) = microthread->cpu_max_opcodes_executeable;
    MP_STATE_VM(cpu_min_opcodes_executeable) = microthread->cpu_min_opcodes_executeable; 
    MP_STATE_VM(cpu_current_opcodes_executed) = microthread->cpu_current_opcodes_executed;
}

void mp_store_microthread(mp_microthread_t *microthread){
    microthread->dict_locals = MP_STATE_CTX(dict_locals);
    microthread->dict_globals = MP_STATE_CTX(dict_globals);
    microthread->cpu_max_opcodes_executeable = MP_STATE_VM(cpu_max_opcodes_executeable);
    microthread->cpu_min_opcodes_executeable = MP_STATE_VM(cpu_min_opcodes_executeable);
    microthread->cpu_current_opcodes_executed = microthread->cpu_current_opcodes_executed;
}


mp_vm_return_kind_t mp_resume_microthread(mp_microthread_t *microthread){
    MP_ENTER_MICROTHREAD(microthread);

    mp_code_state *code_state = microthread->code_state;
    mp_vm_return_kind_t kind = mp_resume_bytecode(code_state, code_state->current, MP_OBJ_NULL);
    microthread->last_kind = kind;

    MP_EXIT_MICROTHREAD(microthread);
    return kind;
}
