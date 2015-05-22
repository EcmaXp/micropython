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

/*
https://github.com/buksy/jnlua/blob/master/src/main/c/jnlua.c
https://github.com/buksy/jnlua/blob/master/src/main/java/com/naef/jnlua/LuaState.java
*/
/** ref: ../java/kr/pe/ecmaxp/micropython/PythonState.java **/

/** JNLUA-LICENSE
Copyright (C) 2008,2012 Andre Naef

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
**/

#include <jni.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "py/mpstate.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/bc.h"
#include "py/cpuctrl.h"
#include "py/stackctrl.h"
#include "py/statectrl.h"
#include "py/objmodule.h"
#include "modmicrothread.h"
#include "genhdr/mpversion.h"

/** BUILD LIMITER **/
#if !MICROPY_MULTI_STATE_CONTEXT
#error jnupy require MICROPY_MULTI_STATE_CONTEXT.
#endif

#if !MICROPY_PY_MICROTHREAD
#error jnupy require MICROPY_PY_MICROTHREAD
#endif

// and other limiter require for building.


/** JNPYTHON INFO **/
#define JNUPY_JNIVERSION JNI_VERSION_1_6

/** JNPYTHON MECRO **/
#define JNUPY_FUNC(name) Java_kr_pe_ecmaxp_micropython_PythonState_##name

/** JNPYTHON VALUE **/
STATIC int initialized = 0;

/** INTERNAL MECRO **/
#define _MEM_SIZE_B  (1)
#define _MEM_SIZE_KB (1024)
#define _MEM_SIZE_MB (1024 * 1024)

#define MEM_SIZE(x, y) ((x) * _MEM_SIZE_##y * (BYTES_PER_WORD / 4))

/** INTERNAL VALUE **/
STATIC uint emit_opt = MP_EMIT_OPT_NONE;

/** PORT IMPL VALUE/FUNCTIONS **/
mp_uint_t mp_verbose_flag = 0;

uint mp_import_stat(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return MP_IMPORT_STAT_DIR;
        } else if (S_ISREG(st.st_mode)) {
            return MP_IMPORT_STAT_FILE;
        }
    }
    return MP_IMPORT_STAT_NO_EXIST;
}

int DEBUG_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return ret;
}

/* TODO: assert handler? */

void nlr_jump_fail(void *val) {
    printf("FATAL: uncaught NLR %p\n", val);
    exit(1);
}

/** JNI CLASS/VALUE REFERENCE **/


/** JNI LOAD/UNLOAD FUNCTIONS **/

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
	JNIEnv *env;
    
    if ((*vm)->GetEnv(vm, (void **) &env, JNUPY_JNIVERSION) != JNI_OK) {
		return JNUPY_JNIVERSION;
	}
	
	initialized = 1;
	return JNUPY_JNIVERSION;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
	JNIEnv *env;
	
	if ((*vm)->GetEnv(vm, (void **) &env, JNUPY_JNIVERSION) != JNI_OK) {
		return;
	}
	
	return;
}

/** JNI EXPORT FUNCTIONS (kr.pe.ecmaxp.micropython.PythonState.mp_xxx **/

JNIEXPORT void JNICALL JNUPY_FUNC(mp_1test_1jni) (JNIEnv *env, jobject obj) {
	printf("Welcome to java native micropython! (env=%p; obj=%p;)\n", env, obj);
}

JNIEXPORT void JNICALL JNUPY_FUNC(mp_1state_1new) (JNIEnv *env, jobject obj) {
	mp_state_ctx_t *state = mp_state_new();
	if (state == NULL) {
	    return;
	}
}

/*
helper function:
STATIC int handle_uncaught_exception(mp_obj_t exc)
STATIC mp_obj_t new_module_from_lexer(mp_lexer_t *lex)
STATIC mp_obj_t *new_module_from_file(const char *filename)
STATIC mp_obj_t get_executor(mp_obj_t module_fun)
STATIC bool execute(mp_state_ctx_t *state, mp_obj_t thread)
mp_state_ctx_t *new_state(mp_uint_t stack_size, mp_uint_t mem_size)
void free_state(mp_state_ctx_t *state)
int something(char *filename)
*/

/*
$ python3 -i -c "from ctypes import CDLL; mp=CDLL('./libmicropython.so')"
>>> mp.something("../../machine/rom/bios.py")
<result of execute bios>
>>>

TODO: should i make many functions for
    - make / control state
    - convert value (msgpack method or other method)
      (and it should support userdata, or native micropython ptr?)
    - execute source from file or buffer
    - handle assert failure or nlr_raise
    - use coffeecatch library or other signal capture handler?
    - safe warpper or sandbox for something.
*/

#define FORCED_EXIT (0x100)
// If exc is SystemExit, return value where FORCED_EXIT bit set,
// and lower 8 bits are SystemExit value. For all other exceptions,
// return 1.
STATIC int handle_uncaught_exception(mp_obj_t exc) {
    // check for SystemExit
    if (mp_obj_is_subclass_fast(mp_obj_get_type(exc), &mp_type_SystemExit)) {
        // None is an exit value of 0; an int is its value; anything else is 1
        mp_obj_t exit_val = mp_obj_exception_get_value(exc);
        mp_int_t val = 0;
        if (exit_val != mp_const_none && !mp_obj_get_int_maybe(exit_val, &val)) {
            val = 1;
        }
        return FORCED_EXIT | (val & 255);
    }

    // Report all other exceptions
    mp_obj_print_exception(&mp_plat_print, exc);
    return 1;
}

STATIC mp_obj_t new_module_from_lexer(mp_lexer_t *lex) {
    qstr source_name = lex->source_name;

    mp_parse_node_t pn = mp_parse(lex, MP_PARSE_FILE_INPUT);
    /* if (pn == NULL){
        return NULL;
    } */
    
    mp_obj_t module_fun = mp_compile(pn, source_name, emit_opt, false);
    /* if (module_fun == NULL){
        return NULL;
    } */
    
    return module_fun;
}

STATIC mp_obj_t *new_module_from_file(const char *filename) {
    mp_lexer_t *lex = mp_lexer_new_from_file(filename);
    if (lex == NULL) {
        printf("MemoryError: lexer could not allocate memory\n");
        return NULL;
    }
    
    #if MICROPY_PY___FILE__
    mp_store_global(MP_QSTR___file__, MP_OBJ_NEW_QSTR(lex->source_name));
    #endif
    
    mp_obj_t module_fun = new_module_from_lexer(lex);
    if (module_fun == NULL) {
        printf("MemoryError: new_module_from_lexer could not allocate memory\n");
        return NULL;
    }
    
    return module_fun;
}

STATIC mp_obj_t get_executor(mp_obj_t module_fun) {
    mp_obj_t module_mt = mp_module_get(MP_QSTR_umicrothread);
    mp_obj_t thread = mp_call_function_2(mp_load_attr(module_mt, MP_QSTR_MicroThread), MP_OBJ_NEW_QSTR(MP_QSTR_module), module_fun);
    return thread;
}

STATIC bool execute(mp_state_ctx_t *state, mp_obj_t thread) {
    // TODO: handle error?

    mp_state_load(state);
    
    bool continue_execute = true;
    mp_obj_t result;
    mp_mrk_t kind = microthread_resume(thread, mp_const_none, &result);
    
    switch (kind) {
        case MP_MRK_STOP:
            continue_execute = false;
            break;
        case MP_MRK_EXCEPTION:
            handle_uncaught_exception(result);
        default:
            continue_execute = true;
    }

    mp_state_store(state);
    return continue_execute;
}

STATIC mp_state_ctx_t *new_state(mp_uint_t stack_size, mp_uint_t mem_size) {
    mp_state_ctx_t *state = mp_state_new();
    mp_state_load(state);

    mp_stack_set_limit(stack_size);

#if MICROPY_ENABLE_GC
    long heap_size = mem_size;
    char *heap = malloc(heap_size);
    gc_init(heap, heap + heap_size);
    assert((char *)MP_STATE_MEM(gc_alloc_table_start) == heap);
#endif
    
    mp_init();
    
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_init(mp_sys_argv, 0);

    mp_state_store(state);
    return state;
}

STATIC void free_state(mp_state_ctx_t *state) {
    mp_state_load(state);
    mp_deinit();
    
#if MICROPY_ENABLE_GC && !defined(NDEBUG)
    // We don't really need to free memory since we are about to exit the
    // process, but doing so helps to find memory leaks.
    free(MP_STATE_MEM(gc_alloc_table_start));
#endif

    mp_state_store(state);
}

int something(char *filename) {
    mp_state_ctx_t *state = new_state(MEM_SIZE(40, KB), MEM_SIZE(256, KB));
    mp_state_load(state);
    
    int ret = 0;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        printf("%s\n", filename);
        mp_obj_t module_fun = new_module_from_file(filename);
        mp_call_function_0(module_fun);
        
        nlr_pop();
    } else {
        ret = handle_uncaught_exception((mp_obj_t)nlr.ret_val);                
    }
    
    mp_state_store(state);
    free_state(state);
    
    (void)get_executor;
    (void)execute;
    
    return ret & 0xff;
}
