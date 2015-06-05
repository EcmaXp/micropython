/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Damien P. George
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
#ifndef __MICROPY_INCLUDED_PY_MPSTATE_H__
#define __MICROPY_INCLUDED_PY_MPSTATE_H__

#include <stdint.h>

#include "py/mpconfig.h"
#include "py/misc.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/objexcept.h"

// This file contains structures defining the state of the Micro Python
// memory system, runtime and virtual machine.  The state is a global
// variable, but in the future it is hoped that the state can become local.

// This structure hold information about the memory allocation system.
typedef struct _mp_state_mem_t {
    #if MICROPY_MEM_STATS
    size_t total_bytes_allocated;
    size_t current_bytes_allocated;
    size_t peak_bytes_allocated;
    #endif

    byte *gc_alloc_table_start;
    mp_uint_t gc_alloc_table_byte_len;
    #if MICROPY_ENABLE_FINALISER
    byte *gc_finaliser_table_start;
    #endif
    mp_uint_t *gc_pool_start;
    mp_uint_t *gc_pool_end;

    int gc_stack_overflow;
    mp_uint_t gc_stack[MICROPY_ALLOC_GC_STACK_SIZE];
    mp_uint_t *gc_sp;
    uint16_t gc_lock_depth;

    // This variable controls auto garbage collection.  If set to 0 then the
    // GC won't automatically run when gc_alloc can't find enough blocks.  But
    // you can still allocate/free memory and also explicitly call gc_collect.
    uint16_t gc_auto_collect_enabled;

    mp_uint_t gc_last_free_atb_index;

    #if MICROPY_PY_GC_COLLECT_RETVAL
    mp_uint_t gc_collected;
    #endif
} mp_state_mem_t;

// This structure hold runtime and VM information.  It includes a section
// which contains root pointers that must be scanned by the GC.
typedef struct _mp_state_vm_t {
    ////////////////////////////////////////////////////////////
    // START ROOT POINTER SECTION
    // everything that needs GC scanning must go here
    // this must start at the start of this structure
    //

    #if !MICROPY_MULTI_STATE_CONTEXT
    // Note: nlr asm code has the offset of this hard-coded
    // MP_STATE_VM(_nlr_top) is dangerous, use mp_nlr_top
    nlr_buf_t *_nlr_top;
    #else
    // just use mp_nlr_top
    #endif
    
    qstr_pool_t *last_pool;

    // non-heap memory for creating an exception if we can't allocate RAM
    mp_obj_exception_t mp_emergency_exception_obj;

    // memory for exception arguments if we can't allocate RAM
    #if MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF
    #if MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE > 0
    // statically allocated buf
    byte mp_emergency_exception_buf[MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE];
    #else
    // dynamically allocated buf
    byte *mp_emergency_exception_buf;
    #endif
    #endif

    // map with loaded modules
    // TODO: expose as sys.modules
    mp_map_t mp_loaded_modules_map;

    // pending exception object (MP_OBJ_NULL if not pending)
    mp_obj_t mp_pending_exception;

    // current exception being handled, for sys.exc_info()
    #if MICROPY_PY_SYS_EXC_INFO
    mp_obj_t cur_exception;
    #endif

    // dictionary for the __main__ module
    mp_obj_dict_t dict_main;

    // these two lists must be initialised per port, after the call to mp_init
    mp_obj_list_t mp_sys_path_obj;
    mp_obj_list_t mp_sys_argv_obj;

    // dictionary for overridden builtins
    #if MICROPY_CAN_OVERRIDE_BUILTINS
    mp_obj_dict_t *mp_module_builtins_override_dict;
    #endif

    // include any root pointers defined by a port
    MICROPY_PORT_ROOT_POINTERS

    //
    // END ROOT POINTER SECTION
    ////////////////////////////////////////////////////////////

    // Stack top at the start of program
    // Note: this entry is used to locate the end of the root pointer section.
    char *stack_top;

    #if MICROPY_STACK_CHECK
    mp_uint_t stack_limit;
    #endif

    mp_uint_t mp_optimise_value;

    // size of the emergency exception buf, if it's dynamically allocated
    #if MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF && MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE == 0
    mp_int_t mp_emergency_exception_buf_size;
    #endif
    
    #if MICROPY_MULTI_STATE_CONTEXT
    bool is_state_loaded;
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
} mp_state_vm_t;

// This structure combines the above 2 structures, and adds the local
// and global dicts.
// Note: if this structure changes then revisit all nlr asm code since they
// have the offset of nlr_top hard-coded.
typedef struct _mp_state_ctx_t {
    // these must come first for root pointer scanning in GC to work
    mp_obj_dict_t *dict_locals;
    mp_obj_dict_t *dict_globals;
    // this must come next for root pointer scanning in GC to work
    mp_state_vm_t vm;
    mp_state_mem_t mem;
} mp_state_ctx_t;

#if MICROPY_MULTI_STATE_CONTEXT
extern THREAD mp_state_ctx_t *mp_state_ctx;
#define MP_STATE_CTX(x) (mp_state_ctx->x)
#define MP_STATE_VM(x) (mp_state_ctx->vm.x)
#define MP_STATE_MEM(x) (mp_state_ctx->mem.x)
#define MP_STATE_CTX_PTR mp_state_ctx
#else
extern mp_state_ctx_t mp_state_ctx;
#define MP_STATE_CTX(x) (mp_state_ctx.x)
#define MP_STATE_VM(x) (mp_state_ctx.vm.x)
#define MP_STATE_MEM(x) (mp_state_ctx.mem.x)
#define MP_STATE_CTX_PTR &mp_state_ctx
#endif // MICROPY_MULTI_STATE_CONTEXT

#endif // __MICROPY_INCLUDED_PY_MPSTATE_H__
