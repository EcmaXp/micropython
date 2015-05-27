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
#include "py/runtime0.h"
#include "py/objstr.h"
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
    bool is_working;
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
#if DEBUG
#define _D(x) printf(#x "\n")
#else
#define _D(x) (void)0
#endif

#define _JNUPY_CUR_STATE(x) (jnupy_cur_state.x)
#define JNUPY_G_VM jnupy_glob_java_vm
#define JNUPY_ENV _JNUPY_CUR_STATE(java_env)
#define JNUPY_SELF _JNUPY_CUR_STATE(java_self)
#define JNUPY_MP_STATE _JNUPY_CUR_STATE(mp_state)

/** JNUPY CALL MECRO **/
#define JNUPY_RAW_CALL_WITH(env, func, ...) (*env)->func(env, __VA_ARGS__)
#define JNUPY_RAW_CALL(func, ...) (*JNUPY_ENV)->func(JNUPY_ENV, __VA_ARGS__)
#define JNUPY_IS_RAW_CALL_HAS_ERROR() (*JNUPY_ENV)->ExceptionOccurred(JNUPY_ENV)
#define JNUPY_RAW_AUTO_THROW (JNUPY_IS_RAW_CALL_HAS_ERROR()? nlr_gk_jump(NULL): (void)0)
#define JNUPY_CALL(func, ...) (*JNUPY_ENV)->func(JNUPY_ENV, __VA_ARGS__); JNUPY_RAW_AUTO_THROW
/*
JNUPY_CALL usage:

// With getting value
jobject msg = JNUPY_CALL(GetLongField, JNUPY_SELF, JFIELD(PythonState, mpState));

// With ignore value
JNUPY_CALL(NewGlobalRef, jfunc);

// It can't mix with if, for, while, return, or etc control block.
// But you can use JNUPY_RAW_CALL, and after call, must check by JNUPY_IS_RAW_CALL_HAS_ERROR()
if (JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/AssertionError", CM4H2), "There is no state") == 0) {
    if (JNUPY_IS_RAW_CALL_HAS_ERROR()) {
        return;
    }
}
assret(! "throwing is failed.");
*/

#define NLR_GK_TOP _JNUPY_CUR_STATE(nlr_gk_top)

/** JNUPY NLR GOAL KEEPER **/
// nlr goal keeper are wapper for java throw in nested function call.
// like assert fail.

#define nlr_gk_set_buf(gk_buf) nlr_gk_set_buf_raw(gk_buf)
#define nlr_gk_new() {false}
#define nlr_gk_push(gk_buf) (nlr_gk_push_raw(gk_buf), nlr_push(&(gk_buf)->buf))
#define nlr_gk_pop(gk_buf) (nlr_gk_pop_raw(gk_buf))
#define nlr_gk_jump(val) (nlr_gk_jump_raw(val))

void nlr_gk_set_buf_raw(nlr_gk_buf_t *gk_buf) {
    if (gk_buf == NULL) {
        mp_nlr_top = NULL;
    } else {
        mp_nlr_top = &gk_buf->buf;
    }
}

void nlr_gk_push_raw(nlr_gk_buf_t *gk_buf) {
    gk_buf->is_working = true;
    
    gk_buf->prev = NLR_GK_TOP;
    nlr_gk_set_buf(gk_buf->prev);
    
    NLR_GK_TOP = gk_buf;
}

void nlr_gk_pop_raw(nlr_gk_buf_t *gk_buf) {
    if (gk_buf->is_working) {
        NLR_GK_TOP = NLR_GK_TOP->prev;
        nlr_gk_set_buf(NLR_GK_TOP);
    }
}

NORETURN void nlr_gk_jump_raw(void *val) {
    NLR_GK_TOP->is_working = false;
    
    nlr_gk_set_buf(NLR_GK_TOP);
    NLR_GK_TOP = NLR_GK_TOP->prev;
    
    nlr_jump(val);
}

/** JNI CLASS/VALUE REFERENCE MECRO **/
#define _JNUPY_REF_ID(hash) _jnupy_REF_##hash

#define JNUPY_CLASS(name, id) _JNUPY_REF_ID(id)
#define JNUPY_METHOD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_FIELD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_STATICMETHOD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_STATICFIELD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_ANY(id) _JNUPY_REF_ID(id)

#define _JNUPY_REF(vtype, id, default) STATIC vtype _JNUPY_REF_ID(id) = default;
#define JNUPY_REF_CLASS(id) _JNUPY_REF(jclass, id, NULL)
#define JNUPY_REF_METHOD(id) _JNUPY_REF(jmethodID, id, 0)
#define JNUPY_REF_FIELD(id) _JNUPY_REF(jfieldID, id, 0)
#define JNUPY_REF_STATICMETHOD(id) JNUPY_REF_METHOD(id)
#define JNUPY_REF_STATICFIELD(id) JNUPY_REF_FIELD(id)
#define JNUPY_REF_ANY(type, id, default) _JNUPY_REF(type, id, default)

#define _JNUPY_LOAD(id, value) if (!(_JNUPY_REF_ID(id) = (value))) break;
#define JNUPY_LOAD_CLASS(name, id) \
    _JNUPY_LOAD(id, jnupy_refclass(name))
#define JNUPY_LOAD_METHOD(clsname, name, type, clsid, id) \
    _JNUPY_LOAD(id, JNUPY_RAW_CALL(GetMethodID, _JNUPY_REF_ID(clsid), name, type))
#define JNUPY_LOAD_FIELD(clsname, name, type, clsid, id) \
    _JNUPY_LOAD(id, JNUPY_RAW_CALL(GetFieldID, _JNUPY_REF_ID(clsid), name, type))
#define JNUPY_LOAD_STATICMETHOD(clsname, name, type, clsid, id) \
    _JNUPY_LOAD(id, JNUPY_RAW_CALL(GetStaticMethodID, _JNUPY_REF_ID(clsid), name, type))
#define JNUPY_LOAD_STATICFIELD(clsname, name, type, clsid, id) \
    _JNUPY_LOAD(id, JNUPY_RAW_CALL(GetStaticFieldID, _JNUPY_REF_ID(clsid), name, type))
#define JNUPY_LOAD_ANY(id, stmt) \
    _JNUPY_LOAD(id, stmt)

#define _JNUPY_UNLOAD(id, value) _JNUPY_REF_ID(id) = value;
#define JNUPY_UNLOAD_CLASS(name, id) \
    _JNUPY_UNLOAD(id, ((_JNUPY_REF_ID(id) != NULL? JNUPY_RAW_CALL(DeleteGlobalRef, _JNUPY_REF_ID(id)): (void)0), NULL))
#define JNUPY_UNLOAD_METHOD(clsname, name, type, clsid, id) \
    _JNUPY_UNLOAD(id, 0)
#define JNUPY_UNLOAD_FIELD(clsname, name, type, clsid, id) \
    _JNUPY_UNLOAD(id, 0)
#define JNUPY_UNLOAD_STATICMETHOD(clsname, name, type, clsid, id) \
    _JNUPY_UNLOAD(id, 0)
#define JNUPY_UNLOAD_STATICFIELD(clsname, name, type, clsid, id) \
    _JNUPY_UNLOAD(id, 0)
#define JNUPY_UNLOAD_ANY(id, stmt, value) \
    stmt; _JNUPY_UNLOAD(id, value)

/** JNUPY AUTO PARSER MECRO **/
#define JNUPY_AP(...)

/** JNI CLASS/VALUE AUTO REFERENCE **/
JNUPY_AP(REF, START)
// CLASS: java/lang/AssertionError
JNUPY_REF_CLASS(CM4H2)
// CLASS: java/lang/Boolean
JNUPY_REF_CLASS(CDKHI)
// CLASS: java/lang/Integer
JNUPY_REF_CLASS(CTOBT)
// CLASS: java/lang/Object
JNUPY_REF_CLASS(CVNFN)
// CLASS: java/lang/String
JNUPY_REF_CLASS(CCHCW)
// CLASS: org/micropython/jnupy/JavaFunction
JNUPY_REF_CLASS(CRBZE)
// CLASS: org/micropython/jnupy/PythonState
JNUPY_REF_CLASS(C4SDY)
// FIELD: org/micropython/jnupy/PythonState->mpState[J]
JNUPY_REF_FIELD(F3VA2)
// METHOD: java/lang/Integer-><init>[(I)V]
JNUPY_REF_METHOD(MMSNU)
// METHOD: java/lang/String-><init>[([BIII)V]
JNUPY_REF_METHOD(M7EEC)
// METHOD: java/lang/String-><init>[([BIILjava/lang/String;)V]
JNUPY_REF_METHOD(MT7JN)
// METHOD: org/micropython/jnupy/JavaFunction->invoke[(Lorg/micropython/jnupy/PythonState;[Ljava/lang/Object;)Ljava/lang/Object;]
JNUPY_REF_METHOD(MEFVT)
// STATICFIELD: java/lang/Boolean->FALSE[Ljava/lang/Boolean;]
JNUPY_REF_STATICFIELD(SYCJ2)
// STATICFIELD: java/lang/Boolean->TRUE[Ljava/lang/Boolean;]
JNUPY_REF_STATICFIELD(S3RTH)
JNUPY_AP(REF, END)

/** JNI CLASS/VALUE NON-AUTO ANY REFERENCE **/
JNUPY_REF_ANY(jstring, RUTF8, NULL)

JNUPY_AP(EXPORT)

/** JNI CLASS/VALUE MANUAL REFERENCE MECRO **/
#define JCLASS(x) JCLASS_##x
#define JFIELD(x, y) ((void)JCLASS(x), JFIELD_##x##_##y)
#define JMETHOD(x, y) ((void)JCLASS(x), JMETHOD_##x##_##y)
#define JMETHODV(x, y, z) ((void)JCLASS(x), JMETHOD_##x##_##y##_##z)
#define JSTATICFIELD(x, y) ((void)JCLASS(x), JSTATICFIELD_##x##_##y)
#define JSTATICMETHOD(x, y) ((void)JCLASS(x), JSTATICMETHOD_##x##_##y)
#define JOBJECT(x) JOBJECT_##x
#define JANY(x) JNUPY_ANY(x)

/** JNI CLASS/VALUE MANUAL REFERENCE **/
#define JCLASS_Boolean JNUPY_CLASS("java/lang/Boolean", CDKHI)
#define JSTATICFIELD_Boolean_TRUE JNUPY_STATICFIELD("java/lang/Boolean", "TRUE", "Ljava/lang/Boolean;", S3RTH)
#define JSTATICFIELD_Boolean_FALSE JNUPY_STATICFIELD("java/lang/Boolean", "FALSE", "Ljava/lang/Boolean;", SYCJ2)

#define JCLASS_PythonState JNUPY_CLASS("org/micropython/jnupy/PythonState", C4SDY)
#define JFIELD_PythonState_mpState JNUPY_FIELD("org/micropython/jnupy/PythonState", "mpState", "J", F3VA2)

#define JCLASS_JavaFunction JNUPY_CLASS("org/micropython/jnupy/JavaFunction", CRBZE)
#define JMETHOD_JavaFunction_invoke JNUPY_METHOD("org/micropython/jnupy/JavaFunction", "invoke", "(Lorg/micropython/jnupy/PythonState;[Ljava/lang/Object;)Ljava/lang/Object;", MEFVT)

#define JCLASS_Integer JNUPY_CLASS("java/lang/Integer", CTOBT)
#define JMETHOD_Integer_INIT JNUPY_METHOD("java/lang/Integer", "<init>", "(I)V", MMSNU)

#define JCLASS_String JNUPY_CLASS("java/lang/String", CCHCW)
#define JMETHOD_String_INIT_str JNUPY_METHOD("java/lang/String", "<init>", "([BIILjava/lang/String;)V", MT7JN)
#define JMETHOD_String_INIT_bytes JNUPY_METHOD("java/lang/String", "<init>", "([BIII)V", M7EEC)

#define JOBJECT_TRUE JNUPY_CALL(GetStaticObjectField, JCLASS(Boolean), JSTATICFIELD(Boolean, TRUE))
#define JOBJECT_FALSE JNUPY_CALL(GetStaticObjectField, JCLASS(Boolean), JSTATICFIELD(Boolean, FALSE))

/** JNI LOAD/UNLOAD FUNCTIONS **/
STATIC jclass jnupy_refclass(const char *className) {
	jclass class_ = JNUPY_RAW_CALL(FindClass, className);
	if (!class_) {
		return NULL;
	}

	return JNUPY_RAW_CALL(NewGlobalRef, class_);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNUPY_G_VM = vm;

    if (JNUPY_RAW_CALL_WITH(JNUPY_G_VM, GetEnv, (void **) &JNUPY_ENV, JNUPY_JNIVERSION) != JNI_OK) {
		return JNUPY_JNIVERSION;
	}

	do {
    // section for load (DO NOT MODIFY)
    JNUPY_AP(LOAD, START)
    JNUPY_LOAD_CLASS("java/lang/AssertionError", CM4H2)
    JNUPY_LOAD_CLASS("java/lang/Boolean", CDKHI)
    JNUPY_LOAD_CLASS("java/lang/Integer", CTOBT)
    JNUPY_LOAD_CLASS("java/lang/Object", CVNFN)
    JNUPY_LOAD_CLASS("java/lang/String", CCHCW)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/JavaFunction", CRBZE)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonState", C4SDY)
    JNUPY_LOAD_FIELD("org/micropython/jnupy/PythonState", "mpState", "J", C4SDY, F3VA2)
    JNUPY_LOAD_METHOD("java/lang/Integer", "<init>", "(I)V", CTOBT, MMSNU)
    JNUPY_LOAD_METHOD("java/lang/String", "<init>", "([BIII)V", CCHCW, M7EEC)
    JNUPY_LOAD_METHOD("java/lang/String", "<init>", "([BIILjava/lang/String;)V", CCHCW, MT7JN)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/JavaFunction", "invoke", "(Lorg/micropython/jnupy/PythonState;[Ljava/lang/Object;)Ljava/lang/Object;", CRBZE, MEFVT)
    JNUPY_LOAD_STATICFIELD("java/lang/Boolean", "FALSE", "Ljava/lang/Boolean;", CDKHI, SYCJ2)
    JNUPY_LOAD_STATICFIELD("java/lang/Boolean", "TRUE", "Ljava/lang/Boolean;", CDKHI, S3RTH)
	JNUPY_AP(LOAD, END)

	// section for load jany
	JNUPY_LOAD_ANY(RUTF8, JNUPY_RAW_CALL(NewStringUTF, "utf-8"))
	// Question: NewStringUTF require NewGlobalRef?

	initialized = 1;
	} while (false);

	return JNUPY_JNIVERSION;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
    JNUPY_G_VM = vm;

	if (JNUPY_RAW_CALL_WITH(JNUPY_G_VM, GetEnv, (void **) &JNUPY_ENV, JNUPY_JNIVERSION) != JNI_OK) {
		return;
	}

	initialized = 0;

	do {
    // section for unload (DO NOT MODIFY)
    JNUPY_AP(UNLOAD, START)
    JNUPY_UNLOAD_CLASS("java/lang/AssertionError", CM4H2)
    JNUPY_UNLOAD_CLASS("java/lang/Boolean", CDKHI)
    JNUPY_UNLOAD_CLASS("java/lang/Integer", CTOBT)
    JNUPY_UNLOAD_CLASS("java/lang/Object", CVNFN)
    JNUPY_UNLOAD_CLASS("java/lang/String", CCHCW)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/JavaFunction", CRBZE)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonState", C4SDY)
    JNUPY_UNLOAD_FIELD("org/micropython/jnupy/PythonState", "mpState", "J", C4SDY, F3VA2)
    JNUPY_UNLOAD_METHOD("java/lang/Integer", "<init>", "(I)V", CTOBT, MMSNU)
    JNUPY_UNLOAD_METHOD("java/lang/String", "<init>", "([BIII)V", CCHCW, M7EEC)
    JNUPY_UNLOAD_METHOD("java/lang/String", "<init>", "([BIILjava/lang/String;)V", CCHCW, MT7JN)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/JavaFunction", "invoke", "(Lorg/micropython/jnupy/PythonState;[Ljava/lang/Object;)Ljava/lang/Object;", CRBZE, MEFVT)
    JNUPY_UNLOAD_STATICFIELD("java/lang/Boolean", "FALSE", "Ljava/lang/Boolean;", CDKHI, SYCJ2)
    JNUPY_UNLOAD_STATICFIELD("java/lang/Boolean", "TRUE", "Ljava/lang/Boolean;", CDKHI, S3RTH)
	JNUPY_AP(UNLOAD, END)

	// section for unload jany
	JNUPY_UNLOAD_ANY(RUTF8, JNUPY_RAW_CALL(ReleaseStringUTFChars, JANY(RUTF8), NULL), 0)

	} while (false);

	return;
}

/** JNUPY INTERNAL FUNCTION **/
bool jnupy_load_state() {
    mp_state_ctx_t *state = (mp_state_ctx_t *) (void *) JNUPY_CALL(GetLongField, JNUPY_SELF, JFIELD(PythonState, mpState));

    if (state == NULL) {
        JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/AssertionError", CM4H2), "There is no state");

        // just return, it will throw error.
        return false;
    }

    mp_state_force_load(state);

    if (state != mp_state_ctx) {
        JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/AssertionError", CM4H2), "Invaild Load");
        return false;
    }

    JNUPY_MP_STATE = state;
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
        JNUPY_RAW_CALL_WITH(JNUPY_G_VM, AttachCurrentThread, (void **) &JNUPY_ENV, NULL);
        JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/AssertionError", CM4H2), buf);
    } else {
        printf("%s\n", buf);
    }

    nlr_gk_jump(NULL);
}

void nlr_jump_fail(void *val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "<JNUPY>: FATAL: uncaught NLR %p (mp_state_ctx=%p)", val, mp_state_ctx);

    if (JNUPY_ENV != NULL) {
        JNUPY_RAW_CALL_WITH(JNUPY_G_VM, AttachCurrentThread, (void **) &JNUPY_ENV, NULL);
        JNUPY_RAW_CALL(FatalError, buf);
    } else {
        printf("%s\n", buf);
    }

    abort();
}

/** JNUPY INTERNAL MODULE **/
const mp_obj_type_t mp_type_jfunc;
typedef struct _mp_obj_jfunc_t {
    mp_obj_base_t base;
    jobject jfunc;
} mp_obj_jfunc_t;

STATIC mp_obj_t jfunc_new(jobject jfunc) {
    mp_obj_jfunc_t *o = m_new_obj_with_finaliser(mp_obj_jfunc_t);
    o->base.type = &mp_type_jfunc;
    o->jfunc = JNUPY_CALL(NewGlobalRef, jfunc);

    return o;
}

// TODO: check modmsgpack.c will help coding!

STATIC mp_obj_t jnupy_obj_j2py(jobject jobj) {
    mp_obj_t obj = mp_const_none;
    // jclass cls = env->GetObjectClass(clsObj);

    // java/lang/Boolean

    return obj;
}

STATIC jobject jnupy_obj_py2j(mp_obj_t obj) {
    jobject jobj = NULL;

    if (0) {
    } else if (obj == mp_const_none) {
        jobj = NULL;
    } else if (obj == mp_const_true) {
        jobj = JOBJECT_TRUE;
    } else if (obj == mp_const_false) {
        jobj = JOBJECT_FALSE;
    } else if (MP_OBJ_IS_SMALL_INT(obj)) {
        mp_int_t val = MP_OBJ_SMALL_INT_VALUE(obj);
        jobj = JNUPY_CALL(NewObject, JCLASS(Integer), JMETHOD(Integer, INIT), val);
    } else if (MP_OBJ_IS_INT(obj)) {
        // TODO: handle big num
        mp_int_t val = mp_obj_int_get_truncated(obj);
        jobj = JNUPY_CALL(NewObject, JCLASS(Integer), JMETHOD(Integer, INIT), val);
    } else if (MP_OBJ_IS_STR_OR_BYTES(obj)) {
        mp_buffer_info_t objbuf;
        mp_get_buffer_raise(obj, &objbuf, MP_BUFFER_READ);

        jbyteArray bytearr = JNUPY_CALL(NewByteArray, objbuf.len);
        JNUPY_CALL(SetByteArrayRegion, bytearr, 0, objbuf.len, objbuf.buf);

        if (MP_OBJ_IS_STR(obj)) {
            jobj = JNUPY_CALL(NewObject, JCLASS(String), JMETHODV(String, INIT, str), bytearr, 0, objbuf.len, JANY(RUTF8));
        } else {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "invaild type"));
            // jobj = JNUPY_CALL(NewObject, JCLASS(String), JMETHODV(String, INIT, bytes), bytearr, 0, objbuf.len);
        }

        JNUPY_CALL(ReleaseByteArrayElements, bytearr, 0, objbuf.len);
    } else {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "invaild type"));
    }

    return jobj;
}

STATIC mp_obj_t jfunc_call(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, n_args, false);
    mp_obj_jfunc_t *o = self_in;

    jobjectArray jargs = JNUPY_CALL(NewObjectArray, n_args, JNUPY_CLASS("java/lang/Object", CVNFN), NULL);

    for (int i = 0; i < n_args; i++) {
        JNUPY_CALL(SetObjectArrayElement, jargs, i, jnupy_obj_py2j(args[i]));
    }

	jobject jresult = JNUPY_RAW_CALL(CallObjectMethod, o->jfunc, JMETHOD(JavaFunction, invoke), JNUPY_SELF, jargs);
	if (JNUPY_IS_RAW_CALL_HAS_ERROR()) {
	    // just throw java error, export to jnupy.
		nlr_raise(mp_obj_new_exception(&mp_type_RuntimeError));
	}

	return jnupy_obj_j2py(jresult);
}

STATIC mp_obj_t jfunc_del(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_obj_jfunc_t *o = self_in;

    JNUPY_CALL(DeleteGlobalRef, o->jfunc);
    printf("success delete;\n");

    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(jfunc_del_obj, jfunc_del);

STATIC const mp_map_elem_t jfunc_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___del__), (mp_obj_t)&jfunc_del_obj },
};

STATIC MP_DEFINE_CONST_DICT(jfunc_locals_dict, jfunc_locals_dict_table);

const mp_obj_type_t mp_type_jfunc = {
    { &mp_type_type },
    .name = MP_QSTR_JFunction,
    .call = jfunc_call,
    .locals_dict = (mp_obj_t)&jfunc_locals_dict,
};

STATIC const mp_map_elem_t mp_module_jnupy_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_micropython) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_JFunction), (mp_obj_t)&mp_type_jfunc },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_jnupy_globals, mp_module_jnupy_globals_table);

const mp_obj_module_t mp_module_jnupy = {
    .base = { &mp_type_module },
    .name = MP_QSTR_jnupy,
    .globals = (mp_obj_dict_t*)&mp_module_jnupy_globals,
};

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

JNUPY_FUNC_DEF(void, mp_1put_1java_1func)
    (JNIEnv *env, jobject self, jobject jfunc) {
    JNUPY_FUNC_START_WITH_STATE;

    mp_obj_t jfunc_obj = jfunc_new(jfunc);
    mp_obj_subscr(mp_globals_get(), MP_OBJ_NEW_QSTR(MP_QSTR_last_jfunc), jfunc_obj);

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

        mp_state_store(state);
        JNUPY_CALL(SetLongField, JNUPY_SELF, JFIELD(PythonState, mpState), (mp_uint_t)state);
        JNUPY_MP_STATE = state;

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
