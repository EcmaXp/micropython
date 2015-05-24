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

#include "py/mpconfig.h"
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
#error jnupy require MICROPY_MULTI_STATE_CONTEXT
#endif

#if !MICROPY_ALLOW_PAUSE_VM
#error jnupy require MICROPY_ALLOW_PAUSE_VM
#endif

#if !MICROPY_OVERRIDE_ASSERT_FAIL
#error jnupy require MICROPY_OVERRIDE_ASSERT_FAIL
#endif

#if !MICROPY_LIMIT_CPU
#error jnupy require MICROPY_LIMIT_CPU
#endif

#if !MICROPY_ENABLE_GC
#error jnupy require MICROPY_ENABLE_GC
#endif

// and other limiter require for building.

/** Legacy Code **/ // TODO: remove this block
/*
TODO: should i make many functions for
    - make / control state
    - convert value (msgpack method or other method)
      (and it should support userdata, or native micropython ptr?)
    - execute source from file or buffer
    - handle assert failure or nlr_raise
    - use coffeecatch library or other signal capture handler?
    - safe warpper or sandbox for something.
*/
/*

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
*/

/** JNUPY INFO **/
#define JNUPY_JNIVERSION JNI_VERSION_1_6

/** JNUPY MECRO **/
#define JNUPY_CUR_STATE(x) (jnupy_cur_state.x)
#define JNUPY_G_VM jnupy_glob_java_vm
#define JNUPY_ENV JNUPY_CUR_STATE(java_env)
#define JNUPY_SELF JNUPY_CUR_STATE(java_self)
#define JNUPY_MP_STATE JNUPY_CUR_STATE(mp_state)
#define JNUPY_MP_PRE_STATE JNUPY_CUR_STATE(mp_pre_state)
#define JNUPY_NLR_TOP JNUPY_CUR_STATE(nlr_top)

#define JNUPY_FUNC(name) Java_kr_pe_ecmaxp_micropython_PythonState_##name
#define JNUPY_FUNC_DEF(ret, name) \
    JNIEXPORT ret JNICALL JNUPY_FUNC(name) 

/** JNUPY INTERNAL VALUE **/
STATIC int initialized = 0;

typedef struct _jnupy_current_state_t {
    mp_state_ctx_t *mp_state;
    mp_state_ctx_t *mp_pre_state; // just a dummy for nlr_xxx
    JNIEnv *java_env;
    jobject java_self;
    nlr_buf_t *nlr_top;
} jnupy_current_state_t;
// pre state required for nlr_top!

STATIC MP_THREAD jnupy_current_state_t jnupy_cur_state;
STATIC JavaVM *jnupy_glob_java_vm;

/** UPY INTERNAL MECRO **/
#define _MEM_SIZE_B  (1)
#define _MEM_SIZE_KB (1024)
#define _MEM_SIZE_MB (1024 * 1024)

#define MEM_SIZE(x, y) ((x) * _MEM_SIZE_##y * (BYTES_PER_WORD / 4))

/** UPY INTERNAL VALUE **/
// STATIC uint emit_opt = MP_EMIT_OPT_NONE;

/** UPY INTERNAL TYPE/FUNCTIONS **/

void jnupy_load_state() {
    if (JNUPY_MP_STATE == NULL) {
        assert(! "mp_state is invaild; (NULL)");
    }
    
    mp_state_force_load(JNUPY_MP_STATE);

    if (JNUPY_MP_PRE_STATE != NULL) {
        mp_state_free(JNUPY_MP_PRE_STATE);
    }
    
    assert(JNUPY_MP_STATE == mp_state_ctx);
}

void jnupy_load_pre_state() {
    if (JNUPY_MP_STATE != NULL) {
        jnupy_load_state();
    } else if (JNUPY_MP_PRE_STATE == NULL) {
        mp_state_ctx_t *state = mp_state_new();
        assert(state != NULL);
        
        JNUPY_MP_PRE_STATE = state;
        mp_state_force_load(state);
    }
}

#include "py/lexer.h"

typedef struct _mp_lexer_vstr_buf_t {
    vstr_t *vstr;
    char *buf;
    mp_uint_t len;
    mp_uint_t pos;
} mp_lexer_vstr_buf_t;

STATIC mp_uint_t vstr_buf_next_byte(mp_lexer_vstr_buf_t *vb) {
    if (vb->pos >= vb->len) {
        return MP_LEXER_EOF;
    }
    
    return vb->buf[vb->pos++];
}

STATIC void vstr_buf_close(mp_lexer_vstr_buf_t *vb) {
    vstr_free(vb->vstr);
    m_del_obj(mp_lexer_vstr_buf_t, vb);
}

mp_lexer_t *mp_lexer_new_from_vstr(const char *filename, vstr_t *vstr) {
    char *buf = vstr_str(vstr);
    if (buf == NULL) {
        return NULL;
    }
    
    mp_lexer_vstr_buf_t *vb = m_new_obj_maybe(mp_lexer_vstr_buf_t);
    if (vb == NULL) {
        return NULL;
    }
    
    vb->vstr = vstr;
    vb->buf = buf;
    vb->len = vstr_len(vstr);
    vb->pos = 0;
    
    return mp_lexer_new(qstr_from_str(filename), vb, (mp_lexer_stream_next_byte_t)vstr_buf_next_byte, (mp_lexer_stream_close_t)vstr_buf_close);
}


/** PORT IMPL VALUE/FUNCTIONS **/
MP_THREAD mp_uint_t mp_verbose_flag = 0;

uint mp_import_stat(const char *path) {
    // TODO: limit path to internal only?
    // (but custom importer maybe don't this this?)
    
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

NORETURN void mp_assert_fail(const char *assertion, const char *file,
                             unsigned int line, const char *function) {
    char buf[256];
    snprintf(buf, sizeof(buf), "<JNUPY>: %s:%u %s: Assertion '%s' failed.\n", file, line, function, assertion);
    puts(buf);
    
    if (JNUPY_ENV != NULL) {
        (*JNUPY_G_VM)->AttachCurrentThread(JNUPY_G_VM, (void **) &JNUPY_ENV, NULL);
        (*JNUPY_ENV)->ThrowNew(JNUPY_ENV, (*JNUPY_ENV)->FindClass(JNUPY_ENV, "java/lang/NoSuchMethodError"), buf);
    }
    
    MP_STATE_VM(nlr_top) = JNUPY_NLR_TOP;
    nlr_jump(NULL);
}

void nlr_jump_fail(void *val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "<JNUPY>: FATAL: uncaught NLR %p\n", val);
    puts(buf);
    
    if (JNUPY_ENV != NULL) {
        printf("e0\n");
        (*JNUPY_G_VM)->AttachCurrentThread(JNUPY_G_VM, (void **) &JNUPY_ENV, NULL);
        printf("e1\n");
        (*JNUPY_ENV)->FatalError(JNUPY_ENV, buf);
        printf("e2\n");
    } else {
        puts(buf);
    }
}

/** JNI CLASS/VALUE REFERENCE **/

/** JNI LOAD/UNLOAD FUNCTIONS **/

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
	JNIEnv *env;
    
    if ((*vm)->GetEnv(vm, (void **) &env, JNUPY_JNIVERSION) != JNI_OK) {
		return JNUPY_JNIVERSION;
	}
	
	if ((*env)->GetJavaVM(env, &JNUPY_G_VM) != JNI_OK) {
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
// http://cafe.daum.net/oddtip/JxlJ/27?docid=1CBe5|JxlJ|27|20080424210900&q=java%20jni&srchid=CCB1CBe5|JxlJ|27|20080424210900

#define return \
    JNUPY_NLR_TOP = _nlr_goalkeeper.prev; \
    MP_STATE_VM(nlr_top) = JNUPY_NLR_TOP; \
    return
#define JNUPY_FUNC_BODY(ret_stmt) \
    JNUPY_ENV = env; \
    JNUPY_SELF = self; \
    nlr_buf_t _nlr_goalkeeper; \
    _nlr_goalkeeper.prev = JNUPY_NLR_TOP; \
    JNUPY_NLR_TOP = &_nlr_goalkeeper; \
    if (nlr_push(&_nlr_goalkeeper) != 0) { \
        ret_stmt; \
    } else

JNUPY_FUNC_DEF(void, mp_1test_1jni) (JNIEnv *env, jobject self) {
//JNUPY_FUNC_BODY(return) {
//    printf("Welcome to java native micropython! (env=%p; obj=%p;)\n", JNUPY_ENV, JNUPY_SELF);
//}
    int i = 0;
    mp_state_ctx = mp_state_new(); // FIX?
    nlr_buf_t _nlr_goalkeeper;
    
    printf("%d\n", i++);
    MP_STATE_VM(nlr_top) = JNUPY_NLR_TOP;
    printf("%d\n", i++);
    JNUPY_NLR_TOP = &_nlr_goalkeeper;
    
    if (nlr_push(&_nlr_goalkeeper) == 0) {
        printf("%d\n", i++);
        printf("Welcome to java native micropython! (env=%p; obj=%p;)\n", JNUPY_ENV, JNUPY_SELF);
        assert(! "failed?");
        printf("%d\n", i++);
        nlr_pop();
        printf("%d\n", i++);
        return;
    } else {
        printf("%d\n", i++);
        printf("failed\n");
        printf("%d\n", i++);
        return;
    }
}

JNUPY_FUNC_DEF(void, mp_1test_1jni_1fail)
        (JNIEnv *env, jobject self) {
JNUPY_FUNC_BODY(return) {
	assert(! "just checking assert work?");
}}
/*
JNUPY_FUNC_DEF(jboolean, mp_1state_1new)
        (JNIEnv *env, jobject self) {
    JNUPY_FUNC_INIT();
    
	if (JNUPY_MP_STATE != NULL) {
	    // TODO: raise error?
	    return JNI_FALSE;
	}
    
	JNUPY_MP_STATE = mp_state_new();
	if (JNUPY_MP_STATE == NULL) {
	    return JNI_FALSE;
	}
	
	mp_uint_t stack_size = MEM_SIZE(40, KB);
	mp_uint_t mem_size = MEM_SIZE(256, KB);
	
    mp_state_load(JNUPY_MP_STATE);
    mp_stack_set_limit(stack_size);
    
    long heap_size = mem_size;
    char *heap = malloc(heap_size);
    gc_init(heap, heap + heap_size);
    assert((char *)MP_STATE_MEM(gc_alloc_table_start) == heap);
    
    mp_init();
    
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_init(mp_sys_argv, 0);

    return JNI_TRUE;
    
    JNUPY_FUNC_DEINIT(0);
}

JNUPY_FUNC_DEF(jboolean, mp_1state_1check)
        (JNIEnv *env, jobject self) {
    JNUPY_FUNC_INIT();
    
	if (JNUPY_MP_STATE != NULL && mp_state_is_loaded(JNUPY_MP_STATE)) {
	    return JNI_TRUE;
	} else {
	    return JNI_FALSE;
	}
    JNUPY_FUNC_DEINIT(0);
}

JNUPY_FUNC_DEF(jboolean, mp_1state_1free)
        (JNIEnv *env, jobject self) {
    JNUPY_FUNC_INIT();
    
	if (JNUPY_MP_STATE != NULL) {
	    if (!mp_state_is_loaded(JNUPY_MP_STATE)) {
    	    mp_state_load(JNUPY_MP_STATE);
            mp_deinit();
            
            free(MP_STATE_MEM(gc_alloc_table_start));
            
            mp_state_store(JNUPY_MP_STATE);
	    }
	    
	    mp_state_free(JNUPY_MP_STATE);
	}
	
	return JNI_TRUE;
	
    JNUPY_FUNC_DEINIT(0);
}

JNUPY_FUNC_DEF(jboolean, mp_1module_1new)
        (JNIEnv *env, jobject self, jstring code) {
    JNUPY_FUNC_INIT();
    jnupy_load_state();
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        char *filename = "<CODE>";
        const char *codebuf = (*env)->GetStringUTFChars(env, code, 0);
        
        vstr_t *vstr = vstr_new();
        vstr_add_str(vstr, codebuf);
        
        (*env)->ReleaseStringUTFChars(env, code, codebuf);
        
        mp_lexer_t *lex = mp_lexer_new_from_vstr(filename, vstr);
        if (lex == NULL) {
            return JNI_FALSE;
        }
        
        qstr source_name = lex->source_name;

        mp_parse_node_t pn = mp_parse(lex, MP_PARSE_FILE_INPUT);

        mp_obj_t module_fun = mp_compile(pn, source_name, MP_EMIT_OPT_NONE, false);
        if (module_fun == NULL) {
            return JNI_FALSE;
        }
        
        mp_call_function_0(module_fun);
        nlr_pop();
        
        return JNI_TRUE;
    } else {
        mp_obj_print_exception(&mp_plat_print, nlr.ret_val);
        return JNI_FALSE;
    }
    
    JNUPY_FUNC_DEINIT(0);
}
*/

#undef return
