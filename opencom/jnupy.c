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

/** ref: ../java/org/micropython/jnupy/PythonState.java **/

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
*/

/** JNUPY INFO **/
#define JNUPY_JNIVERSION JNI_VERSION_1_6

/** JNUPY INTERNAL VALUE **/
STATIC int initialized = 0;

typedef struct _nlr_gk_buf_t {
    bool is_used;
    struct _nlr_gk_buf_t *prev;
    nlr_buf_t buf;
} nlr_gk_buf_t;

typedef struct _jnupy_current_state_t {
    // JNUPY_MP_STATE
    // micropython state context
    mp_state_ctx_t *mp_state;
    
    // JNUPY_NLR_GK_TOP
    // nlr goalkeeper top (for JNI function call warpper)
    nlr_gk_buf_t *nlr_gk_top;

    // JNUPY_ENV
    // java env (vaild only current thread)
    JNIEnv *java_env;

    // JNUPY_SELF
    // java self (vaild only current thread)
    jobject java_self;
    
} jnupy_current_state_t;

// _JNUPY_CUR_STATE(x)
STATIC MP_THREAD jnupy_current_state_t jnupy_cur_state;

// JNUPY_G_VM
STATIC JavaVM *jnupy_glob_java_vm;

/** JNUPY MECRO **/
// #if DEBUG
#define _D(x) printf(#x "\n")
// #else
// #define _D(x) (void)0
// #endif

#define _JNUPY_CUR_STATE(x) (jnupy_cur_state.x)
#define JNUPY_G_VM jnupy_glob_java_vm
#define JNUPY_ENV _JNUPY_CUR_STATE(java_env)
#define JNUPY_SELF _JNUPY_CUR_STATE(java_self)
#define JNUPY_MP_STATE _JNUPY_CUR_STATE(mp_state)
#define NLR_GK_TOP _JNUPY_CUR_STATE(nlr_gk_top)

/** JNUPY NLR GOAL KEEPER **/
/*
nlr goal keeper are wapper for java throw in nested function call.
like assert fail.
*/

#define nlr_gk_set_buf(gk_buf) mp_nlr_top = &(gk_buf)->buf

#define nlr_gk_new() {false}
#define nlr_gk_push(gk_buf) (nlr_gk_push_raw(gk_buf), nlr_push(&(gk_buf)->buf))
#define nlr_gk_pop(gk_buf) (nlr_gk_pop_raw(gk_buf))
#define nlr_gk_jump(val) (nlr_gk_jump_raw(val))

void nlr_gk_push_raw(nlr_gk_buf_t *gk_buf) {
    gk_buf->is_used = true;
    
    nlr_gk_set_buf(NLR_GK_TOP);
    gk_buf->prev = NLR_GK_TOP;
    
    NLR_GK_TOP = gk_buf;
}

void nlr_gk_pop_raw(nlr_gk_buf_t *gk_buf) {
    if (gk_buf->is_used) {
        NLR_GK_TOP = NLR_GK_TOP->prev;
        nlr_gk_set_buf(NLR_GK_TOP);
    }
}

NORETURN void nlr_gk_jump_raw(void *val) {
    nlr_gk_set_buf(NLR_GK_TOP);
    nlr_jump(val);
}

/** JNI CLASS/VALUE REFERENCE MECRO **/
#define _JNUPY_REF_ID(hash) _jnupy_REF_##hash
// #define _jnupy_REF_##hash _jnupy_ref_real

#define JNUPY_CLASS(name, id) _JNUPY_REF_ID(id)
#define JNUPY_METHOD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_FIELD(class_, name, type, id) _JNUPY_REF_ID(id)

#define _JNUPY_REF(vtype, id, default) STATIC vtype _JNUPY_REF_ID(id) = default;
#define JNUPY_REF_CLASS(id) _JNUPY_REF(jclass, id, NULL)
#define JNUPY_REF_METHOD(id) _JNUPY_REF(jmethodID, id, 0)
#define JNUPY_REF_FIELD(id) _JNUPY_REF(jfieldID, id, 0)

/*
JNUPY_REF(CLASS, )
	if (!(luaerror_class = referenceclass(env, "com/naef/jnlua/LuaError"))
			|| !(luaerror_id = (*env)->GetMethodID(env, luaerror_class, "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V"))
			|| !(setluastacktrace_id = (*env)->GetMethodID(env, luaerror_class, "setLuaStackTrace", "([Lcom/naef/jnlua/LuaStackTraceElement;)V"))) {
		return JNLUA_JNIVERSION;
	}
*/

#define _JNUPY_LOAD(id, value) if (!(_JNUPY_REF(id) = (value))) break;
#define JNUPY_LOAD_CLASS(name, id) \
    _JNUPY_LOAD(id, referenceclass(env, (name)))
#define JNUPY_LOAD_METHOD(clsname, name, type, clsid, id) \
    _JNUPY_LOAD(id, (*env)->GetMethodID(env, _JNUPY_REF(clsid), name, type))
#define JNUPY_LOAD_FIELD(clsname, name, type, clsid, id) \
    _JNUPY_LOAD(id, (*env)->GetFieldID(env, _JNUPY_REF(clsid), name, type))

/* TODO: how to auto pasing in c code?
JNUPY_RE
JNUPY_CLASS()
JNUPY_FIELD(CLASS("java/lang/System.out", b), "Field Ljava/io/PrintStream;", c)
JNUPY_REF("Method java/io/PrintStream.println:(Ljava/lang/String;)V", 0)
*/

/** JNUPY AUTO PARSER MECRO **/
#define JNUPY_AP(...)

/** JNI CLASS/VALUE REFERENCE **/
JNUPY_AP(REF, START)

JNUPY_AP(REF, END)

/** JNI LOAD/UNLOAD FUNCTIONS **/

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
	JNIEnv *env;

    if ((*vm)->GetEnv(vm, (void **) &env, JNUPY_JNIVERSION) != JNI_OK) {
		return JNUPY_JNIVERSION;
	}

	if ((*env)->GetJavaVM(env, &JNUPY_G_VM) != JNI_OK) {
	    return JNUPY_JNIVERSION;
	}
	
	if (initialized) {
	    return JNUPY_JNIVERSION;
	}
	
	do {
    JNUPY_AP(LOAD, START)
    
	JNUPY_AP(LOAD, END)
	initialized = 1;
	} while (false);
	
	return JNUPY_JNIVERSION;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
	JNIEnv *env;

	if ((*vm)->GetEnv(vm, (void **) &env, JNUPY_JNIVERSION) != JNI_OK) {
		return;
	}

	return;
}

/** JNUPY INTERNAL FUNCTION **/
bool jnupy_load_state() {
    if (JNUPY_MP_STATE == NULL) {
        (*JNUPY_ENV)->ThrowNew(JNUPY_ENV, (*JNUPY_ENV)->FindClass(JNUPY_ENV, "java/lang/AssertionError"), "There is no state");
        return false;
    }

    mp_state_force_load(JNUPY_MP_STATE);

    if (JNUPY_MP_STATE != mp_state_ctx) {
        return false;
    }

    return true;
}

void jnupy_setup_env(JNIEnv *env, jobject self) {
    JNUPY_ENV = env;
    JNUPY_SELF = self;
}

/** UPY INTERNAL MECRO **/
#define _MEM_SIZE_B  (1)
#define _MEM_SIZE_KB (1024)
#define _MEM_SIZE_MB (1024 * 1024)

#define MEM_SIZE(x, y) ((x) * _MEM_SIZE_##y * (BYTES_PER_WORD / 4))

/** UPY INTERNAL VALUE **/
// STATIC uint emit_opt = MP_EMIT_OPT_NONE;

/** UPY INTERNAL TYPE/FUNCTIONS **/
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
    // TODO: use java output?
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return ret;
}

NORETURN void mp_assert_fail(const char *assertion, const char *file,
                             unsigned int line, const char *function) {

    const char *fmt = "<JNUPY>: %s:%u %s: Assertion '%s' failed.";
    size_t buf_size = strlen(fmt) + strlen(file) + strlen(function) + strlen(assertion) + 64;
    char *buf = malloc(buf_size);

    if (buf != NULL) {
        snprintf(buf, buf_size, fmt, file, line, function, assertion);
    } else {
        buf = "<JNUPY> Allocation buffer failed.";
    }

    if (JNUPY_ENV != NULL) {
        (*JNUPY_G_VM)->AttachCurrentThread(JNUPY_G_VM, (void **) &JNUPY_ENV, NULL);
        (*JNUPY_ENV)->ThrowNew(JNUPY_ENV, (*JNUPY_ENV)->FindClass(JNUPY_ENV, "java/lang/AssertionError"), buf);
    } else {
        printf("%s\n", buf);
    }

    nlr_gk_jump(NULL);
}

void nlr_jump_fail(void *val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "<JNUPY>: FATAL: uncaught NLR %p (mp_state_ctx=%p)", val, mp_state_ctx);
    
    if (JNUPY_ENV != NULL) {
        (*JNUPY_G_VM)->AttachCurrentThread(JNUPY_G_VM, (void **) &JNUPY_ENV, NULL);
        (*JNUPY_ENV)->FatalError(JNUPY_ENV, buf);
    } else {
        printf("%s\n", buf);
    }

    abort();
}

/** JNI EXPORT FUNCTION MECRO **/
#define JNUPY_FUNC(name) Java_org_micropython_jnupy_PythonState_##name
#define JNUPY_FUNC_DEF(ret, name) \
    JNIEXPORT ret JNICALL JNUPY_FUNC(name)

#define _JNUPY_FUNC_BODY_START(init_expr) \
    jnupy_setup_env(env, self); \
    nlr_gk_buf_t _nlr_gk = nlr_gk_new(); \
    if (init_expr) { \
        do { \
            if (nlr_gk_push(&_nlr_gk) != 0) { \
                break; \
            } \
            
            /* body */

#define _JNUPY_FUNC_BODY_END(ret_stmt) \
        } while(0); \
    } \
    ret_stmt;

#define return \
    nlr_gk_pop(&_nlr_gk); \
    return

#define JNUPY_FUNC_START_WITH_STATE _JNUPY_FUNC_BODY_START(jnupy_load_state())
#define JNUPY_FUNC_START _JNUPY_FUNC_BODY_START(true)
#define JNUPY_FUNC_END_VOID _JNUPY_FUNC_BODY_END(return)
#define JNUPY_FUNC_END_VALUE(value) _JNUPY_FUNC_BODY_END(return (value))
#define JNUPY_FUNC_END _JNUPY_FUNC_BODY_END(return 0)

/** JNI EXPORT FUNCTIONS (org.micropython.jnupy.PythonState.mp_xxx **/
// http://cafe.daum.net/oddtip/JxlJ/27?docid=1CBe5|JxlJ|27|20080424210900&q=java%20jni&srchid=CCB1CBe5|JxlJ|27|20080424210900
JNUPY_AP(EXPORT)

JNUPY_FUNC_DEF(void, mp_1test_1jni)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START;

    printf("Welcome to java native micropython! (env=%p; obj=%p;)\n", JNUPY_ENV, JNUPY_SELF);

    JNUPY_FUNC_END_VOID;
}

JNUPY_FUNC_DEF(void, mp_1test_1jni_1fail)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START;

	assert(! "just checking assert work?");

    JNUPY_FUNC_END_VOID;
}

JNUPY_FUNC_DEF(void, mp_1test_1jni_1state)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START_WITH_STATE;

    JNUPY_FUNC_END_VOID;
}


JNUPY_FUNC_DEF(jboolean, mp_1state_1new)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START;

    if (!initialized) {
        return JNI_FALSE;
    }

	if (JNUPY_MP_STATE != NULL) {
	    // TODO: raise error?
	    return JNI_FALSE;
	}

	mp_state_ctx_t *state = mp_state_new();
	if (state == NULL) {
	    return JNI_FALSE;
	}

    char *heap = NULL;

    nlr_gk_buf_t nlr_gk = nlr_gk_new();
    if (nlr_gk_push(&nlr_gk) == 0) {
        mp_uint_t stack_size = MEM_SIZE(40, KB);
        mp_uint_t heap_size = MEM_SIZE(256, KB);

        mp_state_force_load(state);
        mp_stack_set_limit(stack_size);

        heap = malloc(heap_size);
        assert(heap != NULL);

        gc_init(heap, heap + heap_size);

        assert((char *)MP_STATE_MEM(gc_alloc_table_start) == heap);

        mp_init();

        mp_obj_list_init(mp_sys_path, 0);
        mp_obj_list_init(mp_sys_argv, 0);

        JNUPY_MP_STATE = state;
        mp_state_store(state);

        nlr_gk_pop(&nlr_gk);
        return JNI_TRUE;
    } else {
        if (state != NULL) {
            free(state);
        }

        if (heap != NULL) {
            free(heap);
        }

        return JNI_FALSE;
    }

    JNUPY_FUNC_END;
}


JNUPY_FUNC_DEF(jboolean, mp_1state_1check)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START;

	if (JNUPY_MP_STATE != NULL) {
	    return JNI_TRUE;
	} else {
	    return JNI_FALSE;
	}

    JNUPY_FUNC_END;
}

JNUPY_FUNC_DEF(jboolean, mp_1state_1free)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START;

	if (JNUPY_MP_STATE != NULL) {
	    if (!mp_state_is_loaded(JNUPY_MP_STATE)) {
    	    mp_state_load(JNUPY_MP_STATE);
	    }

        mp_deinit();
        free(MP_STATE_MEM(gc_alloc_table_start));
        mp_state_store(JNUPY_MP_STATE);

	    mp_state_free(JNUPY_MP_STATE);
	}

	return JNI_TRUE;

    JNUPY_FUNC_END;
}

JNUPY_FUNC_DEF(jboolean, mp_1code_1exec)
    (JNIEnv *env, jobject self, jstring code) {
    JNUPY_FUNC_START_WITH_STATE;

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

    JNUPY_FUNC_END;
}

/** JNI EXPORT FUNCTION MECRO CLNEAUP **/
#undef return

/*
JNUPY_CLASS("hello")
JNUPY_CLASS("helloworld")
JNUPY_CLASS("helloworld3")
JNUPY_METHOD("helloworld3", "hello world", "?")

*/