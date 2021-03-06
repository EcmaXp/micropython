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

/** ref: ../java/org/micropython/jnupy/PythonNativeState.java **/

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

#define DEBUG_BACKTRACE 0

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <setjmp.h>

#if DEBUG_BACKTRACE
#include <execinfo.h>
#endif

#include <jni.h>

#include "mpconfigport.h"

#include "py/bc.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/cpuctrl.h"
#include "py/gc.h"
#include "py/mpconfig.h"
#include "py/mpstate.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objint.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/repl.h"
#include "py/runtime.h"
#include "py/runtime0.h"
#include "py/stackctrl.h"
#include "py/statectrl.h"
#include "genhdr/mpversion.h"

/** BUILD LIMITER **/
#if !MICROPY_MULTI_STATE_CONTEXT
#error jnupy require MICROPY_MULTI_STATE_CONTEXT
#endif

#if !MICROPY_OVERRIDE_ASSERT_FAIL
#error jnupy require MICROPY_OVERRIDE_ASSERT_FAIL
#endif

#if !MICROPY_ENABLE_GC
#error jnupy require MICROPY_ENABLE_GC
#endif

#if !MICROPY_PY_BUILTINS_FLOAT
#error jnupy require MICROPY_PY_BUILTINS_FLOAT
#endif

#if MICROPY_LONGINT_IMPL != MICROPY_LONGINT_IMPL_LONGLONG
#error jnupy support only MICROPY_LONGINT_IMPL_LONGLONG
// TODO: support MICROPY_LONGINT_IMPL_MPZ...
#endif

#if !MICROPY_GCREGS_SETJMP
#error jnupy require MICROPY_GCREGS_SETJMP
#endif

/** BUILD SOFT LIMITER **/
#if !MICROPY_ALLOW_PAUSE_VM
#error jnupy require MICROPY_ALLOW_PAUSE_VM
#endif

#if !MICROPY_LIMIT_CPU
#error jnupy require MICROPY_LIMIT_CPU
#endif

#if MICROPY_OBJ_REPR != MICROPY_OBJ_REPR_A
// TODO: play with MP_ROM_stuff (HELL)
#error jnupy only support MICROPY_OBJ_REPR_A
#endif

/** JNUPY INFO **/
#define JNUPY_JNIVERSION JNI_VERSION_1_6
// TODO: fill this.

/** JNUPY DEBUG **/
int DEBUG_printf(const char *fmt, ...) {
    // TODO: use java output?
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return ret;
}

#if 0 // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#define _D(x) printf(#x "\n")
#else // don't print debugging info
#define DEBUG_PRINT (0)
#define DEBUG_printf(...) (void)0
#define _D(x) (void)0
#endif

#define DEBUG_BACKTRACE_SIZE 8
#define DEBUG_BACKTRACE_FD 2 // stderr

/** JNUPY INTERNAL VALUE **/
STATIC int initialized = 0;

typedef struct _nlr_gk_buf_t {
    bool is_working;
    struct _nlr_gk_buf_t *prev;
    nlr_buf_t buf;
} nlr_gk_buf_t;

typedef struct _jnupy_func_stack_t {
	struct _jnupy_func_stack_t *prev; // prev jfs
	bool with_state; // if require fill JNUPY_MP_STATE
	bool is_first_load; // false
	bool has_exception; // false
	bool before_return; // false
	char *stack_last; // NULL (nested call fill that)
	char *stack_top; // last MP_STATE_VM(stack_top)
	nlr_buf_t *nlr_ptr; // last mp_nlr_top
	nlr_gk_buf_t _nlr_gk; // new nlr_gk buf
} jnupy_func_stack_t;

typedef struct _jnupy_current_state_t {
    // JNUPY_MP_STATE
    // micropython state context
    mp_state_ctx_t *mp_state;

    // JNUPY_NLR_GK_TOP
    // nlr goalkeeper top (for JNI function call warpper)
    nlr_gk_buf_t *nlr_gk_top;

	// JNUPY_JFS_TOP
	jnupy_func_stack_t *jfs_top;

    // JNUPY_ENV
    // java env (vaild only current thread)
    JNIEnv *java_env;

    // JNUPY_SELF
    // java self (vaild only current thread)
    jobject java_self;

    // JNUPY_PY_STATE
    // java PythonNativeState (vaild only current thread)
    jobject java_pystate;
} jnupy_current_state_t;

// _JNUPY_CUR_STATE(x)
STATIC MP_THREAD jnupy_current_state_t jnupy_cur_state;

// JNUPY_G_VM
STATIC JavaVM *jnupy_glob_java_vm;

// JNUPY_G_ENV
STATIC JNIEnv *jnupy_glob_java_env;

/** JNUPY MECRO **/
#define _JNUPY_CUR_STATE(x) (jnupy_cur_state.x)
#define JNUPY_G_VM jnupy_glob_java_vm
#define JNUPY_G_ENV jnupy_glob_java_env
#define JNUPY_ENV _JNUPY_CUR_STATE(java_env)
#define JNUPY_SELF _JNUPY_CUR_STATE(java_self)
#define JNUPY_MP_STATE _JNUPY_CUR_STATE(mp_state)
#define JNUPY_PY_JSTATE _JNUPY_CUR_STATE(java_pystate)
#define JNUPY_NLR_GK_TOP _JNUPY_CUR_STATE(nlr_gk_top)
#define JNUPY_JFS_TOP _JNUPY_CUR_STATE(jfs_top)

void jnupy_nest_prepare(char *stack_top) {
	if (JNUPY_JFS_TOP != NULL) {
		JNUPY_JFS_TOP->stack_last = MP_STATE_VM(stack_top);
		// printf("STACK LAST SETUP: %p\n", MP_STATE_VM(stack_top));
	}

	MP_STATE_VM(stack_top) = stack_top;
}

void jnupy_nest_cleanup() {
	if (JNUPY_JFS_TOP != NULL) {
		JNUPY_JFS_TOP->stack_last = NULL;
		// printf("STACK LAST CLEANUP: %p\n", JNUPY_JFS_TOP->stack_last);
	}
}

/** JNUPY CALL MECRO **/
#define JNUPY_RAW_CALL_WITH(env, func, ...) (*env)->func(env, __VA_ARGS__)
#define JNUPY_RAW_CALL(func, ...) (*JNUPY_ENV)->func(JNUPY_ENV, __VA_ARGS__)
#define JNUPY_RAW_CALL1(func) (*JNUPY_ENV)->func(JNUPY_ENV)
#define JNUPY_RAW_AUTO_THROW (jnupy_throw_jerror_auto())
#define JNUPY_ASSIGN_WITH_NESTED_CALL(value, func, ...) \
	char *_jnupy_stack_top = MP_STATE_VM(stack_top); \
	mp_stack_ctrl_init(); \
	jnupy_nest_prepare(_jnupy_stack_top); \
	value = (*JNUPY_ENV)->func(JNUPY_ENV, __VA_ARGS__); \
	jnupy_nest_cleanup(); \
	JNUPY_RAW_AUTO_THROW
#define JNUPY_VOID_NESTED_CALL(func, ...) \
	void *_jnupy_stack_top = MP_STATE_VM(stack_top); \
	mp_stack_ctrl_init(); \
	jnupy_nest_prepare(_jnupy_stack_top); \
	(*JNUPY_ENV)->func(JNUPY_ENV, __VA_ARGS__); \
	jnupy_nest_cleanup(); \
	JNUPY_RAW_AUTO_THROW
#define JNUPY_CALL(func, ...) (*JNUPY_ENV)->func(JNUPY_ENV, __VA_ARGS__); JNUPY_RAW_AUTO_THROW
/* JNUPY_CALL usage:

// With getting value
jobject msg = JNUPY_CALL(GetLongField, JNUPY_SELF, JFIELD(PythonNativeState, mpState));

// With ignore value
JNUPY_CALL(NewGlobalRef, jfunc);

// It can't mix with if, for, while, return, or etc control block.
// But you can use JNUPY_RAW_CALL, and after call, must check by JNUPY_RAW_CALL1(ExceptionOccurred)
if (JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/AssertionError", CM4H2), "There is no state") == 0) {
    if (JNUPY_RAW_CALL1(ExceptionOccurred)) {
        return;
    }
}
assret(! "throwing is failed.");
*/

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
        // mp_nlr_top = NULL;
    } else {
        mp_nlr_top = &gk_buf->buf;
    }
}

void nlr_gk_push_raw(nlr_gk_buf_t *gk_buf) {
    gk_buf->is_working = true;

    gk_buf->prev = JNUPY_NLR_GK_TOP;
    JNUPY_NLR_GK_TOP = gk_buf;
}

void nlr_gk_pop_raw(nlr_gk_buf_t *gk_buf) {
    if (gk_buf->is_working) {
        // assert(mp_nlr_top == JNUPY_NLR_GK_TOP);
        // assert(mp_nlr_top.prev == JNUPY_NLR_GK_TOP.prev)?

        JNUPY_NLR_GK_TOP->is_working = false;
        JNUPY_NLR_GK_TOP = JNUPY_NLR_GK_TOP->prev;
        nlr_gk_set_buf(JNUPY_NLR_GK_TOP);
    }
}

NORETURN void nlr_gk_jump_raw(void *val) {
    if (JNUPY_NLR_GK_TOP != NULL) {
        JNUPY_NLR_GK_TOP->is_working = false;
    
        nlr_gk_set_buf(JNUPY_NLR_GK_TOP);
        JNUPY_NLR_GK_TOP = JNUPY_NLR_GK_TOP->prev;

		nlr_jump(val);
	} else {
		nlr_jump_fail(val);
	}
}

NORETURN void jnupy_throw_jerror(jthrowable jerror);
void jnupy_throw_jerror_auto() {
    jthrowable jerror = JNUPY_RAW_CALL1(ExceptionOccurred);
	if (jerror != NULL) {
		jnupy_throw_jerror(jerror);
	}
}

/** JNI CLASS/VALUE REFERENCE MECRO **/
#define _JNUPY_REF_ID(hash) _jnupy_REF_##hash

#define JNUPY_CLASS(name, id) _JNUPY_REF_ID(id)
#define JNUPY_ENUM(clsname, name, id) _JNUPY_REF_ID(id)
#define JNUPY_METHOD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_FIELD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_STATICMETHOD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_STATICFIELD(class_, name, type, id) _JNUPY_REF_ID(id)
#define JNUPY_ANY(id) _JNUPY_REF_ID(id)

#define _JNUPY_REF(vtype, id, default) STATIC vtype _JNUPY_REF_ID(id) = default;
#define JNUPY_REF_CLASS(id) _JNUPY_REF(jclass, id, NULL)
#define JNUPY_REF_ENUM(id) _JNUPY_REF(jobject, id, NULL)
#define JNUPY_REF_METHOD(id) _JNUPY_REF(jmethodID, id, 0)
#define JNUPY_REF_FIELD(id) _JNUPY_REF(jfieldID, id, 0)
#define JNUPY_REF_STATICMETHOD(id) JNUPY_REF_METHOD(id)
#define JNUPY_REF_STATICFIELD(id) JNUPY_REF_FIELD(id)
#define JNUPY_REF_ANY(type, id, default) _JNUPY_REF(type, id, default)

#define _JNUPY_LOAD(id, value) if (!(_JNUPY_REF_ID(id) = (value))) break;
#define JNUPY_LOAD_CLASS(name, id) \
    _JNUPY_LOAD(id, jnupy_refclass(name))
#define JNUPY_LOAD_ENUM(clsname, name, clsid, id) \
    _JNUPY_LOAD(id, jnupy_refenum(_JNUPY_REF_ID(clsid), name, "L" clsname ";"))
// " // fix github syntex highlighter
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
#define _JNUPY_UNLOAD_REF(id) \
    _JNUPY_UNLOAD(id, ((_JNUPY_REF_ID(id) != NULL? JNUPY_RAW_CALL(DeleteGlobalRef, _JNUPY_REF_ID(id)): (void)0), NULL))
#define JNUPY_UNLOAD_CLASS(name, id) \
    _JNUPY_UNLOAD_REF(id)
#define JNUPY_UNLOAD_ENUM(clsname, name, clsid, id) \
    _JNUPY_UNLOAD_REF(id)
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
// CLASS: [B
JNUPY_REF_CLASS(CWUC3)
// CLASS: [Ljava/lang/Object;
JNUPY_REF_CLASS(CWN4U)
// CLASS: java/io/ByteArrayInputStream
JNUPY_REF_CLASS(CPYWG)
// CLASS: java/io/ByteArrayOutputStream
JNUPY_REF_CLASS(CPITF)
// CLASS: java/lang/AssertionError
JNUPY_REF_CLASS(CM4H2)
// CLASS: java/lang/Boolean
JNUPY_REF_CLASS(CDKHI)
// CLASS: java/lang/Class
JNUPY_REF_CLASS(CWQVW)
// CLASS: java/lang/Double
JNUPY_REF_CLASS(CMCKJ)
// CLASS: java/lang/Float
JNUPY_REF_CLASS(CLJBD)
// CLASS: java/lang/IllegalArgumentException
JNUPY_REF_CLASS(C5K3P)
// CLASS: java/lang/IllegalStateException
JNUPY_REF_CLASS(CQIAK)
// CLASS: java/lang/Integer
JNUPY_REF_CLASS(CTOBT)
// CLASS: java/lang/Long
JNUPY_REF_CLASS(CJACF)
// CLASS: java/lang/Object
JNUPY_REF_CLASS(CVNFN)
// CLASS: java/lang/Short
JNUPY_REF_CLASS(CMUMS)
// CLASS: java/lang/String
JNUPY_REF_CLASS(CCHCW)
// CLASS: java/util/HashMap
JNUPY_REF_CLASS(CEKJY)
// CLASS: org/micropython/jnupy/InternalException
JNUPY_REF_CLASS(CXMH3)
// CLASS: org/micropython/jnupy/JavaFunction
JNUPY_REF_CLASS(CRBZE)
// CLASS: org/micropython/jnupy/JavaObject
JNUPY_REF_CLASS(COLFU)
// CLASS: org/micropython/jnupy/PythonArguments
JNUPY_REF_CLASS(CGRO2)
// CLASS: org/micropython/jnupy/PythonImportStat
JNUPY_REF_CLASS(CEIHH)
// CLASS: org/micropython/jnupy/PythonModule
JNUPY_REF_CLASS(CL6LM)
// CLASS: org/micropython/jnupy/PythonNativeException
JNUPY_REF_CLASS(CLGS4)
// CLASS: org/micropython/jnupy/PythonNativeState
JNUPY_REF_CLASS(CEYRH)
// CLASS: org/micropython/jnupy/PythonObject
JNUPY_REF_CLASS(CKZG7)
// CLASS: org/micropython/jnupy/PythonParseInputKind
JNUPY_REF_CLASS(C6GOM)
// ENUM: org/micropython/jnupy/PythonImportStat->MP_IMPORT_STAT_DIR
JNUPY_REF_ENUM(E4DOQ)
// ENUM: org/micropython/jnupy/PythonImportStat->MP_IMPORT_STAT_FILE
JNUPY_REF_ENUM(EBJGE)
// ENUM: org/micropython/jnupy/PythonImportStat->MP_IMPORT_STAT_NO_EXIST
JNUPY_REF_ENUM(E3MMX)
// ENUM: org/micropython/jnupy/PythonParseInputKind->MP_PARSE_EVAL_INPUT
JNUPY_REF_ENUM(E6JE6)
// ENUM: org/micropython/jnupy/PythonParseInputKind->MP_PARSE_FILE_INPUT
JNUPY_REF_ENUM(EEPIP)
// ENUM: org/micropython/jnupy/PythonParseInputKind->MP_PARSE_SINGLE_INPUT
JNUPY_REF_ENUM(EGOM6)
// FIELD: org/micropython/jnupy/JavaObject->obj[Ljava/lang/Object;]
JNUPY_REF_FIELD(FKU6D)
// FIELD: org/micropython/jnupy/PythonNativeException->pyobj[Lorg/micropython/jnupy/PythonObject;]
JNUPY_REF_FIELD(FWOHZ)
// FIELD: org/micropython/jnupy/PythonNativeState->mpState[J]
JNUPY_REF_FIELD(FJ2IH)
// FIELD: org/micropython/jnupy/PythonObject->pythonState[Lorg/micropython/jnupy/PythonState;]
JNUPY_REF_FIELD(F4HBX)
// FIELD: org/micropython/jnupy/PythonObject->refObject[J]
JNUPY_REF_FIELD(FB723)
// METHOD: java/io/ByteArrayInputStream-><init>[([B)V]
JNUPY_REF_METHOD(MUZ6M)
// METHOD: java/io/ByteArrayOutputStream->toByteArray[()[B]
JNUPY_REF_METHOD(MHUAS)
// METHOD: java/lang/Boolean->booleanValue[()Z]
JNUPY_REF_METHOD(ME2HS)
// METHOD: java/lang/Class->getName[()Ljava/lang/String;]
JNUPY_REF_METHOD(MNDS4)
// METHOD: java/lang/Double-><init>[(D)V]
JNUPY_REF_METHOD(MONM4)
// METHOD: java/lang/Double->doubleValue[()D]
JNUPY_REF_METHOD(MRBT7)
// METHOD: java/lang/Double->floatValue[()F]
JNUPY_REF_METHOD(MTEKP)
// METHOD: java/lang/Float-><init>[(F)V]
JNUPY_REF_METHOD(MT3CM)
// METHOD: java/lang/Float->floatValue[()F]
JNUPY_REF_METHOD(MAHUY)
// METHOD: java/lang/Integer-><init>[(I)V]
JNUPY_REF_METHOD(MMSNU)
// METHOD: java/lang/Integer->intValue[()I]
JNUPY_REF_METHOD(MIDRV)
// METHOD: java/lang/Long-><init>[(J)V]
JNUPY_REF_METHOD(MPPJO)
// METHOD: java/lang/Long->longValue[()J]
JNUPY_REF_METHOD(ME7YL)
// METHOD: java/lang/Short->intValue[()I]
JNUPY_REF_METHOD(ML5BW)
// METHOD: java/lang/String-><init>[([BIILjava/lang/String;)V]
JNUPY_REF_METHOD(MT7JN)
// METHOD: java/lang/String->getBytes[(Ljava/lang/String;)[B]
JNUPY_REF_METHOD(MNONY)
// METHOD: java/util/HashMap-><init>[(I)V]
JNUPY_REF_METHOD(M4HPE)
// METHOD: java/util/HashMap->put[(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;]
JNUPY_REF_METHOD(MSG5B)
// METHOD: org/micropython/jnupy/JavaFunction->checkInvoke[(Lorg/micropython/jnupy/PythonState;II)V]
JNUPY_REF_METHOD(MBSG4)
// METHOD: org/micropython/jnupy/JavaFunction->invoke[(Lorg/micropython/jnupy/PythonState;Lorg/micropython/jnupy/PythonArguments;)Ljava/lang/Object;]
JNUPY_REF_METHOD(MX2DM)
// METHOD: org/micropython/jnupy/PythonArguments-><init>[([Lorg/micropython/jnupy/PythonObject;Ljava/util/HashMap;)V]
JNUPY_REF_METHOD(MHXVF)
// METHOD: org/micropython/jnupy/PythonImportStat->ordinal[()I]
JNUPY_REF_METHOD(M6CJ5)
// METHOD: org/micropython/jnupy/PythonModule-><init>[(Lorg/micropython/jnupy/PythonState;JJ)V]
JNUPY_REF_METHOD(MTE4H)
// METHOD: org/micropython/jnupy/PythonNativeException-><init>[(Ljava/lang/String;)V]
JNUPY_REF_METHOD(MOBLI)
// METHOD: org/micropython/jnupy/PythonNativeException-><init>[(Ljava/lang/String;Lorg/micropython/jnupy/PythonObject;)V]
JNUPY_REF_METHOD(MZUSH)
// METHOD: org/micropython/jnupy/PythonNativeState->print[([B)V]
JNUPY_REF_METHOD(MOUBH)
// METHOD: org/micropython/jnupy/PythonNativeState->readFile[(Ljava/lang/String;)Ljava/lang/String;]
JNUPY_REF_METHOD(M3LT6)
// METHOD: org/micropython/jnupy/PythonNativeState->readStat[(Ljava/lang/String;)Lorg/micropython/jnupy/PythonImportStat;]
JNUPY_REF_METHOD(MEUPZ)
// METHOD: org/micropython/jnupy/PythonObject-><init>[(Lorg/micropython/jnupy/PythonState;JJ)V]
JNUPY_REF_METHOD(MHTAY)
// METHOD: org/micropython/jnupy/PythonParseInputKind->ordinal[()I]
JNUPY_REF_METHOD(MAUVV)
// STATICFIELD: java/lang/Boolean->FALSE[Ljava/lang/Boolean;]
JNUPY_REF_STATICFIELD(SYCJ2)
// STATICFIELD: java/lang/Boolean->TRUE[Ljava/lang/Boolean;]
JNUPY_REF_STATICFIELD(S3RTH)
JNUPY_AP(REF, END)

/** JNI CLASS/VALUE NON-AUTO ANY REFERENCE **/
JNUPY_REF_ANY(jstring, RUTF8, NULL)
// TODO: use StandardCharsets.UTF_8

JNUPY_AP(EXPORT)

/** JNI CLASS/VALUE MANUAL REFERENCE MECRO **/
#define JCLASS(x) JCLASS_##x
#define JFIELD(x, y) ((void)JCLASS(x), JFIELD_##x##_##y)
#define JMETHOD(x, y) ((void)JCLASS(x), JMETHOD_##x##_##y)
#define JMETHODV(x, y, z) ((void)JCLASS(x), JMETHOD_##x##_##y##_##z)
#define JSTATICFIELD(x, y) ((void)JCLASS(x), JSTATICFIELD_##x##_##y)
#define JSTATICMETHOD(x, y) ((void)JCLASS(x), JSTATICMETHOD_##x##_##y)
#define JENUM(x) JENUM_##x
#define JOBJECT(x) JOBJECT_##x
#define JANY(x) JNUPY_ANY(x)

/** JNI CLASS/VALUE MANUAL REFERENCE **/
#define JCLASS_Boolean JNUPY_CLASS("java/lang/Boolean", CDKHI)
#define JMETHOD_Boolean_booleanValue JNUPY_METHOD("java/lang/Boolean", "booleanValue", "()Z", ME2HS)
#define JSTATICFIELD_Boolean_TRUE JNUPY_STATICFIELD("java/lang/Boolean", "TRUE", "Ljava/lang/Boolean;", S3RTH)
#define JSTATICFIELD_Boolean_FALSE JNUPY_STATICFIELD("java/lang/Boolean", "FALSE", "Ljava/lang/Boolean;", SYCJ2)

#define JCLASS_PythonNativeState JNUPY_CLASS("org/micropython/jnupy/PythonNativeState", CEYRH)
#define JMETHOD_PythonNativeState_print JNUPY_METHOD("org/micropython/jnupy/PythonNativeState", "print", "([B)V", MOUBH)
#define JMETHOD_PythonNativeState_readStat JNUPY_METHOD("org/micropython/jnupy/PythonNativeState", "readStat", "(Ljava/lang/String;)Lorg/micropython/jnupy/PythonImportStat;", MEUPZ)
#define JMETHOD_PythonNativeState_readFile JNUPY_METHOD("org/micropython/jnupy/PythonNativeState", "readFile", "(Ljava/lang/String;)Ljava/lang/String;", M3LT6)
#define JFIELD_PythonNativeState_mpState JNUPY_FIELD("org/micropython/jnupy/PythonNativeState", "mpState", "J", FJ2IH)

#define JCLASS_PythonObject JNUPY_CLASS("org/micropython/jnupy/PythonObject", CKZG7)
#define JMETHOD_PythonObject_INIT JNUPY_METHOD("org/micropython/jnupy/PythonObject", "<init>", "(Lorg/micropython/jnupy/PythonState;JJ)V", MHTAY)
#define JFIELD_PythonObject_refObject JNUPY_FIELD("org/micropython/jnupy/PythonObject", "refObject", "J", FB723)
#define JFIELD_PythonObject_pythonState JNUPY_FIELD("org/micropython/jnupy/PythonObject", "pythonState", "Lorg/micropython/jnupy/PythonState;", F4HBX)

#define JCLASS_PythonArguments JNUPY_CLASS("org/micropython/jnupy/PythonArguments", CGRO2)
#define JMETHOD_PythonArguments_INIT JNUPY_METHOD("org/micropython/jnupy/PythonArguments", "<init>", "([Lorg/micropython/jnupy/PythonObject;Ljava/util/HashMap;)V", MHXVF)

#define JCLASS_PythonModule JNUPY_CLASS("org/micropython/jnupy/PythonModule", CL6LM)
#define JMETHOD_PythonModule_INIT JNUPY_METHOD("org/micropython/jnupy/PythonModule", "<init>", "(Lorg/micropython/jnupy/PythonState;JJ)V", MTE4H)

// TODO: adding Tuple, List, etc...?

#define JCLASS_PythonNativeException JNUPY_CLASS("org/micropython/jnupy/PythonNativeException", CLGS4)
#define JMETHOD_PythonNativeException_INIT JNUPY_METHOD("org/micropython/jnupy/PythonNativeException", "<init>", "(Ljava/lang/String;)V", MOBLI)
#define JMETHOD_PythonNativeException_INIT_with_pyobj JNUPY_METHOD("org/micropython/jnupy/PythonNativeException", "<init>", "(Ljava/lang/String;Lorg/micropython/jnupy/PythonObject;)V", MZUSH)
#define JFIELD_PythonNativeException_pyobj JNUPY_FIELD("org/micropython/jnupy/PythonNativeException", "pyobj", "Lorg/micropython/jnupy/PythonObject;", FWOHZ)

#define JCLASS_JavaFunction JNUPY_CLASS("org/micropython/jnupy/JavaFunction", CRBZE)
#define JMETHOD_JavaFunction_invoke JNUPY_METHOD("org/micropython/jnupy/JavaFunction", "invoke", "(Lorg/micropython/jnupy/PythonState;Lorg/micropython/jnupy/PythonArguments;)Ljava/lang/Object;", MX2DM)
#define JMETHOD_JavaFunction_checkInvoke JNUPY_METHOD("org/micropython/jnupy/JavaFunction", "checkInvoke", "(Lorg/micropython/jnupy/PythonState;II)V", MBSG4)

#define JCLASS_JavaObject JNUPY_CLASS("org/micropython/jnupy/JavaObject", COLFU)
#define JFIELD_JavaObject_obj JNUPY_FIELD("org/micropython/jnupy/JavaObject", "obj", "Ljava/lang/Object;", FKU6D)

#define JCLASS_PythonImportStat JNUPY_CLASS("org/micropython/jnupy/PythonImportStat", CEIHH)
#define JENUM_MP_IMPORT_STAT_NO_EXIST JNUPY_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_NO_EXIST", E3MMX)
#define JENUM_MP_IMPORT_STAT_DIR JNUPY_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_DIR", E4DOQ)
#define JENUM_MP_IMPORT_STAT_FILE JNUPY_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_FILE", EBJGE)
#define JMETHOD_PythonImportStat_ordinal JNUPY_METHOD("org/micropython/jnupy/PythonImportStat", "ordinal", "()I", M6CJ5)

#define JCLASS_PythonParseInputKind JNUPY_CLASS("org/micropython/jnupy/PythonParseInputKind", C6GOM)
#define JENUM_MP_PARSE_SINGLE_INPUT JNUPY_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_SINGLE_INPUT", EGOM6)
#define JENUM_MP_PARSE_FILE_INPUT JNUPY_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_FILE_INPUT", EEPIP)
#define JENUM_MP_PARSE_EVAL_INPUT JNUPY_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_EVAL_INPUT", E6JE6)
#define JMETHOD_PythonParseInputKind_ordinal JNUPY_METHOD("org/micropython/jnupy/PythonParseInputKind", "ordinal", "()I", MAUVV)

#define JCLASS_Class JNUPY_CLASS("java/lang/Class", CWQVW)
#define JMETHOD_Class_getName JNUPY_METHOD("java/lang/Class", "getName", "()Ljava/lang/String;", MNDS4)

#define JCLASS_Object JNUPY_CLASS("java/lang/Object", CVNFN)

#define JCLASS_ObjectArray JNUPY_CLASS("[Ljava/lang/Object;", CWN4U)

#define JCLASS_Short JNUPY_CLASS("java/lang/Short", CMUMS)
#define JMETHOD_Short_intValue JNUPY_METHOD("java/lang/Short", "intValue", "()I", ML5BW)

#define JCLASS_Integer JNUPY_CLASS("java/lang/Integer", CTOBT)
#define JMETHOD_Integer_INIT JNUPY_METHOD("java/lang/Integer", "<init>", "(I)V", MMSNU)
#define JMETHOD_Integer_intValue JNUPY_METHOD("java/lang/Integer", "intValue", "()I", MIDRV)

#define JCLASS_Long JNUPY_CLASS("java/lang/Long", CJACF)
#define JMETHOD_Long_INIT JNUPY_METHOD("java/lang/Long", "<init>", "(J)V", MPPJO)
#define JMETHOD_Long_longValue JNUPY_METHOD("java/lang/Long", "longValue", "()J", ME7YL)

#define JCLASS_Float JNUPY_CLASS("java/lang/Float", CLJBD)
#define JMETHOD_Float_INIT JNUPY_METHOD("java/lang/Float", "<init>", "(F)V", MT3CM)
#define JMETHOD_Float_floatValue JNUPY_METHOD("java/lang/Float", "floatValue", "()F", MAHUY)

#define JCLASS_Double JNUPY_CLASS("java/lang/Double", CMCKJ)
// java.lang.Double can't convert from float... WHAT??
#define JMETHOD_Double_INIT JNUPY_METHOD("java/lang/Double", "<init>", "(D)V", MONM4)
#define JMETHOD_Double_doubleValue JNUPY_METHOD("java/lang/Double", "doubleValue", "()D", MRBT7)
#define JMETHOD_Double_floatValue JNUPY_METHOD("java/lang/Double", "floatValue", "()F", MTEKP)

#define JCLASS_String JNUPY_CLASS("java/lang/String", CCHCW)
#define JMETHOD_String_INIT_str JNUPY_METHOD("java/lang/String", "<init>", "([BIILjava/lang/String;)V", MT7JN)
#define JMETHOD_String_getBytes JNUPY_METHOD("java/lang/String", "getBytes", "(Ljava/lang/String;)[B", MNONY)

#define JCLASS_ByteArray JNUPY_CLASS("[B", CWUC3)

#define JCLASS_ByteArrayInputStream JNUPY_CLASS("java/io/ByteArrayInputStream", CPYWG)
#define JMETHOD_ByteArrayInputStream_INIT JNUPY_METHOD("java/io/ByteArrayInputStream", "<init>", "([B)V", MUZ6M)

#define JCLASS_ByteArrayOutputStream JNUPY_CLASS("java/io/ByteArrayOutputStream", CPITF)
#define JMETHOD_ByteArrayOutputStream_toByteArray JNUPY_METHOD("java/io/ByteArrayOutputStream", "toByteArray", "()[B", MHUAS)

#define JCLASS_HashMap JNUPY_CLASS("java/util/HashMap", CEKJY)
#define JMETHOD_HashMap_INIT JNUPY_METHOD("java/util/HashMap", "<init>", "(I)V", M4HPE)
#define JMETHOD_HashMap_put JNUPY_METHOD("java/util/HashMap", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", MSG5B)

#define JOBJECT_TRUE JNUPY_CALL(GetStaticObjectField, JCLASS(Boolean), JSTATICFIELD(Boolean, TRUE))
#define JOBJECT_FALSE JNUPY_CALL(GetStaticObjectField, JCLASS(Boolean), JSTATICFIELD(Boolean, FALSE))

// TODO: jnupy ap should load ENUM also?
// #define JENUM_

/** JNI LOAD/UNLOAD FUNCTIONS **/
STATIC jclass jnupy_refclass(const char *className) {
	jclass class_ = JNUPY_RAW_CALL(FindClass, className);
	if (!class_) {
		return NULL;
	}

	return JNUPY_RAW_CALL(NewGlobalRef, class_);
}

STATIC jobject jnupy_refenum(jclass class_, const char *enumName, const char *enumType) {
    // enumType is just "Lclass;"
    jfieldID fid = JNUPY_RAW_CALL(GetStaticFieldID, class_, enumName, enumType);
    if (!fid) {
        return NULL;
    }

    jobject enum_ = JNUPY_RAW_CALL(GetStaticObjectField, class_, fid);
    if (!enum_) {
        return enum_;
    }

	return JNUPY_RAW_CALL(NewGlobalRef, enum_);
}

void main(void) {

}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
	// printf("hello world\n");
    JNUPY_G_VM = vm;

    if (JNUPY_RAW_CALL_WITH(JNUPY_G_VM, GetEnv, (void **) &JNUPY_ENV, JNUPY_JNIVERSION) != JNI_OK) {
		return JNUPY_JNIVERSION;
	}

    JNUPY_G_ENV = JNUPY_ENV;

	do {
    // section for load (DO NOT MODIFY)
    JNUPY_AP(LOAD, START)
    JNUPY_LOAD_CLASS("[B", CWUC3)
    JNUPY_LOAD_CLASS("[Ljava/lang/Object;", CWN4U)
    JNUPY_LOAD_CLASS("java/io/ByteArrayInputStream", CPYWG)
    JNUPY_LOAD_CLASS("java/io/ByteArrayOutputStream", CPITF)
    JNUPY_LOAD_CLASS("java/lang/AssertionError", CM4H2)
    JNUPY_LOAD_CLASS("java/lang/Boolean", CDKHI)
    JNUPY_LOAD_CLASS("java/lang/Class", CWQVW)
    JNUPY_LOAD_CLASS("java/lang/Double", CMCKJ)
    JNUPY_LOAD_CLASS("java/lang/Float", CLJBD)
    JNUPY_LOAD_CLASS("java/lang/IllegalArgumentException", C5K3P)
    JNUPY_LOAD_CLASS("java/lang/IllegalStateException", CQIAK)
    JNUPY_LOAD_CLASS("java/lang/Integer", CTOBT)
    JNUPY_LOAD_CLASS("java/lang/Long", CJACF)
    JNUPY_LOAD_CLASS("java/lang/Object", CVNFN)
    JNUPY_LOAD_CLASS("java/lang/Short", CMUMS)
    JNUPY_LOAD_CLASS("java/lang/String", CCHCW)
    JNUPY_LOAD_CLASS("java/util/HashMap", CEKJY)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/InternalException", CXMH3)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/JavaFunction", CRBZE)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/JavaObject", COLFU)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonArguments", CGRO2)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonImportStat", CEIHH)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonModule", CL6LM)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonNativeException", CLGS4)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonNativeState", CEYRH)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonObject", CKZG7)
    JNUPY_LOAD_CLASS("org/micropython/jnupy/PythonParseInputKind", C6GOM)
    JNUPY_LOAD_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_DIR", CEIHH, E4DOQ)
    JNUPY_LOAD_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_FILE", CEIHH, EBJGE)
    JNUPY_LOAD_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_NO_EXIST", CEIHH, E3MMX)
    JNUPY_LOAD_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_EVAL_INPUT", C6GOM, E6JE6)
    JNUPY_LOAD_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_FILE_INPUT", C6GOM, EEPIP)
    JNUPY_LOAD_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_SINGLE_INPUT", C6GOM, EGOM6)
    JNUPY_LOAD_FIELD("org/micropython/jnupy/JavaObject", "obj", "Ljava/lang/Object;", COLFU, FKU6D)
    JNUPY_LOAD_FIELD("org/micropython/jnupy/PythonNativeException", "pyobj", "Lorg/micropython/jnupy/PythonObject;", CLGS4, FWOHZ)
    JNUPY_LOAD_FIELD("org/micropython/jnupy/PythonNativeState", "mpState", "J", CEYRH, FJ2IH)
    JNUPY_LOAD_FIELD("org/micropython/jnupy/PythonObject", "pythonState", "Lorg/micropython/jnupy/PythonState;", CKZG7, F4HBX)
    JNUPY_LOAD_FIELD("org/micropython/jnupy/PythonObject", "refObject", "J", CKZG7, FB723)
    JNUPY_LOAD_METHOD("java/io/ByteArrayInputStream", "<init>", "([B)V", CPYWG, MUZ6M)
    JNUPY_LOAD_METHOD("java/io/ByteArrayOutputStream", "toByteArray", "()[B", CPITF, MHUAS)
    JNUPY_LOAD_METHOD("java/lang/Boolean", "booleanValue", "()Z", CDKHI, ME2HS)
    JNUPY_LOAD_METHOD("java/lang/Class", "getName", "()Ljava/lang/String;", CWQVW, MNDS4)
    JNUPY_LOAD_METHOD("java/lang/Double", "<init>", "(D)V", CMCKJ, MONM4)
    JNUPY_LOAD_METHOD("java/lang/Double", "doubleValue", "()D", CMCKJ, MRBT7)
    JNUPY_LOAD_METHOD("java/lang/Double", "floatValue", "()F", CMCKJ, MTEKP)
    JNUPY_LOAD_METHOD("java/lang/Float", "<init>", "(F)V", CLJBD, MT3CM)
    JNUPY_LOAD_METHOD("java/lang/Float", "floatValue", "()F", CLJBD, MAHUY)
    JNUPY_LOAD_METHOD("java/lang/Integer", "<init>", "(I)V", CTOBT, MMSNU)
    JNUPY_LOAD_METHOD("java/lang/Integer", "intValue", "()I", CTOBT, MIDRV)
    JNUPY_LOAD_METHOD("java/lang/Long", "<init>", "(J)V", CJACF, MPPJO)
    JNUPY_LOAD_METHOD("java/lang/Long", "longValue", "()J", CJACF, ME7YL)
    JNUPY_LOAD_METHOD("java/lang/Short", "intValue", "()I", CMUMS, ML5BW)
    JNUPY_LOAD_METHOD("java/lang/String", "<init>", "([BIILjava/lang/String;)V", CCHCW, MT7JN)
    JNUPY_LOAD_METHOD("java/lang/String", "getBytes", "(Ljava/lang/String;)[B", CCHCW, MNONY)
    JNUPY_LOAD_METHOD("java/util/HashMap", "<init>", "(I)V", CEKJY, M4HPE)
    JNUPY_LOAD_METHOD("java/util/HashMap", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", CEKJY, MSG5B)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/JavaFunction", "checkInvoke", "(Lorg/micropython/jnupy/PythonState;II)V", CRBZE, MBSG4)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/JavaFunction", "invoke", "(Lorg/micropython/jnupy/PythonState;Lorg/micropython/jnupy/PythonArguments;)Ljava/lang/Object;", CRBZE, MX2DM)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonArguments", "<init>", "([Lorg/micropython/jnupy/PythonObject;Ljava/util/HashMap;)V", CGRO2, MHXVF)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonImportStat", "ordinal", "()I", CEIHH, M6CJ5)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonModule", "<init>", "(Lorg/micropython/jnupy/PythonState;JJ)V", CL6LM, MTE4H)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonNativeException", "<init>", "(Ljava/lang/String;)V", CLGS4, MOBLI)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonNativeException", "<init>", "(Ljava/lang/String;Lorg/micropython/jnupy/PythonObject;)V", CLGS4, MZUSH)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonNativeState", "print", "([B)V", CEYRH, MOUBH)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonNativeState", "readFile", "(Ljava/lang/String;)Ljava/lang/String;", CEYRH, M3LT6)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonNativeState", "readStat", "(Ljava/lang/String;)Lorg/micropython/jnupy/PythonImportStat;", CEYRH, MEUPZ)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonObject", "<init>", "(Lorg/micropython/jnupy/PythonState;JJ)V", CKZG7, MHTAY)
    JNUPY_LOAD_METHOD("org/micropython/jnupy/PythonParseInputKind", "ordinal", "()I", C6GOM, MAUVV)
    JNUPY_LOAD_STATICFIELD("java/lang/Boolean", "FALSE", "Ljava/lang/Boolean;", CDKHI, SYCJ2)
    JNUPY_LOAD_STATICFIELD("java/lang/Boolean", "TRUE", "Ljava/lang/Boolean;", CDKHI, S3RTH)
	JNUPY_AP(LOAD, END)

	// section for load jany
	JNUPY_LOAD_ANY(RUTF8, JNUPY_RAW_CALL(NewGlobalRef, JNUPY_RAW_CALL(NewStringUTF, "utf-8")))
	// Question: NewStringUTF require NewGlobalRef? [YES]

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
    JNUPY_UNLOAD_CLASS("[B", CWUC3)
    JNUPY_UNLOAD_CLASS("[Ljava/lang/Object;", CWN4U)
    JNUPY_UNLOAD_CLASS("java/io/ByteArrayInputStream", CPYWG)
    JNUPY_UNLOAD_CLASS("java/io/ByteArrayOutputStream", CPITF)
    JNUPY_UNLOAD_CLASS("java/lang/AssertionError", CM4H2)
    JNUPY_UNLOAD_CLASS("java/lang/Boolean", CDKHI)
    JNUPY_UNLOAD_CLASS("java/lang/Class", CWQVW)
    JNUPY_UNLOAD_CLASS("java/lang/Double", CMCKJ)
    JNUPY_UNLOAD_CLASS("java/lang/Float", CLJBD)
    JNUPY_UNLOAD_CLASS("java/lang/IllegalArgumentException", C5K3P)
    JNUPY_UNLOAD_CLASS("java/lang/IllegalStateException", CQIAK)
    JNUPY_UNLOAD_CLASS("java/lang/Integer", CTOBT)
    JNUPY_UNLOAD_CLASS("java/lang/Long", CJACF)
    JNUPY_UNLOAD_CLASS("java/lang/Object", CVNFN)
    JNUPY_UNLOAD_CLASS("java/lang/Short", CMUMS)
    JNUPY_UNLOAD_CLASS("java/lang/String", CCHCW)
    JNUPY_UNLOAD_CLASS("java/util/HashMap", CEKJY)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/InternalException", CXMH3)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/JavaFunction", CRBZE)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/JavaObject", COLFU)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonArguments", CGRO2)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonImportStat", CEIHH)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonModule", CL6LM)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonNativeException", CLGS4)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonNativeState", CEYRH)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonObject", CKZG7)
    JNUPY_UNLOAD_CLASS("org/micropython/jnupy/PythonParseInputKind", C6GOM)
    JNUPY_UNLOAD_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_DIR", CEIHH, E4DOQ)
    JNUPY_UNLOAD_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_FILE", CEIHH, EBJGE)
    JNUPY_UNLOAD_ENUM("org/micropython/jnupy/PythonImportStat", "MP_IMPORT_STAT_NO_EXIST", CEIHH, E3MMX)
    JNUPY_UNLOAD_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_EVAL_INPUT", C6GOM, E6JE6)
    JNUPY_UNLOAD_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_FILE_INPUT", C6GOM, EEPIP)
    JNUPY_UNLOAD_ENUM("org/micropython/jnupy/PythonParseInputKind", "MP_PARSE_SINGLE_INPUT", C6GOM, EGOM6)
    JNUPY_UNLOAD_FIELD("org/micropython/jnupy/JavaObject", "obj", "Ljava/lang/Object;", COLFU, FKU6D)
    JNUPY_UNLOAD_FIELD("org/micropython/jnupy/PythonNativeException", "pyobj", "Lorg/micropython/jnupy/PythonObject;", CLGS4, FWOHZ)
    JNUPY_UNLOAD_FIELD("org/micropython/jnupy/PythonNativeState", "mpState", "J", CEYRH, FJ2IH)
    JNUPY_UNLOAD_FIELD("org/micropython/jnupy/PythonObject", "pythonState", "Lorg/micropython/jnupy/PythonState;", CKZG7, F4HBX)
    JNUPY_UNLOAD_FIELD("org/micropython/jnupy/PythonObject", "refObject", "J", CKZG7, FB723)
    JNUPY_UNLOAD_METHOD("java/io/ByteArrayInputStream", "<init>", "([B)V", CPYWG, MUZ6M)
    JNUPY_UNLOAD_METHOD("java/io/ByteArrayOutputStream", "toByteArray", "()[B", CPITF, MHUAS)
    JNUPY_UNLOAD_METHOD("java/lang/Boolean", "booleanValue", "()Z", CDKHI, ME2HS)
    JNUPY_UNLOAD_METHOD("java/lang/Class", "getName", "()Ljava/lang/String;", CWQVW, MNDS4)
    JNUPY_UNLOAD_METHOD("java/lang/Double", "<init>", "(D)V", CMCKJ, MONM4)
    JNUPY_UNLOAD_METHOD("java/lang/Double", "doubleValue", "()D", CMCKJ, MRBT7)
    JNUPY_UNLOAD_METHOD("java/lang/Double", "floatValue", "()F", CMCKJ, MTEKP)
    JNUPY_UNLOAD_METHOD("java/lang/Float", "<init>", "(F)V", CLJBD, MT3CM)
    JNUPY_UNLOAD_METHOD("java/lang/Float", "floatValue", "()F", CLJBD, MAHUY)
    JNUPY_UNLOAD_METHOD("java/lang/Integer", "<init>", "(I)V", CTOBT, MMSNU)
    JNUPY_UNLOAD_METHOD("java/lang/Integer", "intValue", "()I", CTOBT, MIDRV)
    JNUPY_UNLOAD_METHOD("java/lang/Long", "<init>", "(J)V", CJACF, MPPJO)
    JNUPY_UNLOAD_METHOD("java/lang/Long", "longValue", "()J", CJACF, ME7YL)
    JNUPY_UNLOAD_METHOD("java/lang/Short", "intValue", "()I", CMUMS, ML5BW)
    JNUPY_UNLOAD_METHOD("java/lang/String", "<init>", "([BIILjava/lang/String;)V", CCHCW, MT7JN)
    JNUPY_UNLOAD_METHOD("java/lang/String", "getBytes", "(Ljava/lang/String;)[B", CCHCW, MNONY)
    JNUPY_UNLOAD_METHOD("java/util/HashMap", "<init>", "(I)V", CEKJY, M4HPE)
    JNUPY_UNLOAD_METHOD("java/util/HashMap", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", CEKJY, MSG5B)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/JavaFunction", "checkInvoke", "(Lorg/micropython/jnupy/PythonState;II)V", CRBZE, MBSG4)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/JavaFunction", "invoke", "(Lorg/micropython/jnupy/PythonState;Lorg/micropython/jnupy/PythonArguments;)Ljava/lang/Object;", CRBZE, MX2DM)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonArguments", "<init>", "([Lorg/micropython/jnupy/PythonObject;Ljava/util/HashMap;)V", CGRO2, MHXVF)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonImportStat", "ordinal", "()I", CEIHH, M6CJ5)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonModule", "<init>", "(Lorg/micropython/jnupy/PythonState;JJ)V", CL6LM, MTE4H)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonNativeException", "<init>", "(Ljava/lang/String;)V", CLGS4, MOBLI)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonNativeException", "<init>", "(Ljava/lang/String;Lorg/micropython/jnupy/PythonObject;)V", CLGS4, MZUSH)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonNativeState", "print", "([B)V", CEYRH, MOUBH)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonNativeState", "readFile", "(Ljava/lang/String;)Ljava/lang/String;", CEYRH, M3LT6)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonNativeState", "readStat", "(Ljava/lang/String;)Lorg/micropython/jnupy/PythonImportStat;", CEYRH, MEUPZ)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonObject", "<init>", "(Lorg/micropython/jnupy/PythonState;JJ)V", CKZG7, MHTAY)
    JNUPY_UNLOAD_METHOD("org/micropython/jnupy/PythonParseInputKind", "ordinal", "()I", C6GOM, MAUVV)
    JNUPY_UNLOAD_STATICFIELD("java/lang/Boolean", "FALSE", "Ljava/lang/Boolean;", CDKHI, SYCJ2)
    JNUPY_UNLOAD_STATICFIELD("java/lang/Boolean", "TRUE", "Ljava/lang/Boolean;", CDKHI, S3RTH)
	JNUPY_AP(UNLOAD, END)

	// section for unload jany
	JNUPY_UNLOAD_ANY(RUTF8, JNUPY_RAW_CALL(ReleaseStringUTFChars, JANY(RUTF8), NULL), 0)

	} while (false);

	return;
}

/** JNUPY INTERNAL FUNCTION **/
bool jnupy_load_state(mp_state_ctx_t *state) {
    if (state == NULL) {
        JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/IllegalStateException", CQIAK), "Python state is closed.");

        // just return, it will throw error.
        return false;
    }

    JNUPY_MP_STATE = state;
    return true;
}

mp_state_ctx_t *jnupy_get_state_from_pythonnativestate(jobject pythonNativeState) {
    jlong pythonNativeStateId = JNUPY_CALL(GetLongField, pythonNativeState, JFIELD(PythonNativeState, mpState));
    return (mp_state_ctx_t *)pythonNativeStateId;
}

bool jnupy_load_state_from_pythonnativestate(jobject self) {
    JNUPY_PY_JSTATE = JNUPY_RAW_CALL(NewGlobalRef, self);
    mp_state_ctx_t *state = jnupy_get_state_from_pythonnativestate(JNUPY_PY_JSTATE);
    return jnupy_load_state(state);
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

/** PORT IMPL VALUE/FUNCTIONS **/
STATIC bool _jnupy_attach_env() {
    if (JNUPY_ENV == NULL && JNUPY_G_ENV != NULL) {
        JNUPY_RAW_CALL_WITH(JNUPY_G_VM, AttachCurrentThread, (void **) &JNUPY_G_ENV, NULL);
    }

    return true;
}

NORETURN void mp_assert_fail(const char *assertion, const char *file,
                             unsigned int line, const char *function) {

    const char *fmt = "<JNUPY>: %s:%u %s: Assertion '%s' failed.";
    size_t buf_size = strlen(fmt) + strlen(file) + strlen(function) + strlen(assertion) + 16;
    char *buf = malloc(buf_size);

    #if DEBUG_BACKTRACE
        void *array[DEBUG_BACKTRACE_SIZE];
        size_t size;
        size = backtrace(array, DEBUG_BACKTRACE_SIZE);
        backtrace_symbols_fd(array, size, DEBUG_BACKTRACE_FD);
    #endif

    if (buf != NULL) {
        snprintf(buf, buf_size, fmt, file, line, function, assertion);
    } else {
        buf = "<JNUPY> Allocation buffer failed.";
    }

    if (_jnupy_attach_env()) {
        JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/AssertionError", CM4H2), buf);

        // assert is very special error. so just bypass all handler.
        // (and that's why need nlr goal keeper.)
        nlr_gk_jump(NULL); // THROW NULL MAKE ERROR MAN (must clear NULL behavior)
    } else {
        printf("%s\n", buf);
    }

    abort();
}

void nlr_jump_fail(void *val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "<JNUPY>: FATAL: uncaught NLR %p (mp_state_ctx=%p)", val, MP_STATE_CTX_PTR);

    if (_jnupy_attach_env()) {
        JNUPY_RAW_CALL(FatalError, buf);
    } else {
        printf("%s\n", buf);
    }

    abort();
}

typedef jmp_buf regs_t;

STATIC void gc_helper_get_regs(regs_t arr) {
	setjmp(arr);
}

void gc_collect(void) {
	volatile int stack_dummy;
	//gc_dump_info();

	gc_collect_start();
	regs_t regs;

	gc_helper_get_regs(regs);
	// GC stack (and regs because we captured them)
	void **regs_ptr = (void**)(void*)&regs;
	gc_collect_root(regs_ptr, ((mp_uint_t)MP_STATE_VM(stack_top) - (mp_uint_t)&regs) / sizeof(mp_uint_t));

	if (JNUPY_JFS_TOP != NULL) {
		assert(JNUPY_JFS_TOP->stack_last == NULL);
		JNUPY_JFS_TOP->stack_last = &stack_dummy;
	}
	
	jnupy_func_stack_t *jfs = JNUPY_JFS_TOP;
	while (jfs != NULL) {
		size_t stack = ((mp_uint_t)MP_STATE_VM(stack_top) - (mp_uint_t)jfs->stack_last) / sizeof(mp_uint_t);
		gc_collect_root((void **)(void *)jfs->stack_last, stack);
		jfs = jfs->prev;
	}

	if (JNUPY_JFS_TOP != NULL) {
		JNUPY_JFS_TOP->stack_last = NULL;
	}

	gc_collect_end();

	//printf("-----\n");
	//gc_dump_info();
}

// mp_plat_print_strn call it for print to stdout.
void jnupy_print_strn(const char *str, mp_uint_t len) {
    if (MP_STATE_CTX_PTR == NULL || !MP_STATE_VM(is_state_loaded) || (JNUPY_PY_JSTATE == NULL)) {
        // state is unloaded.
        printf("%.*s", (int)len, str);
    } else {
        jbyteArray bytearr = JNUPY_CALL(NewByteArray, len);
        JNUPY_CALL(SetByteArrayRegion, bytearr, 0, len, (const jbyte *)str);

		// [!:JNUPY_NESTED_CALL] lead to nested call
        JNUPY_VOID_NESTED_CALL(CallVoidMethod, JNUPY_PY_JSTATE, JMETHOD(PythonNativeState, print), bytearr);

        JNUPY_CALL(ReleaseByteArrayElements, bytearr, NULL, JNI_ABORT);
    }
}

uint mp_import_stat(const char *path) {
    jstring jpath = JNUPY_CALL(NewStringUTF, path);

	// [!:JNUPY_NESTED_CALL] lead to nested call
	jobject jresult;
	JNUPY_ASSIGN_WITH_NESTED_CALL(jresult, CallObjectMethod, JNUPY_PY_JSTATE, JMETHOD(PythonNativeState, readStat), jpath);

    JNUPY_CALL(ReleaseStringUTFChars, jpath, NULL);

    jint result = JNUPY_CALL(CallIntMethod, jresult, JMETHOD(PythonImportStat, ordinal));
    switch (result) {
        case MP_IMPORT_STAT_DIR:
        case MP_IMPORT_STAT_FILE:
        case MP_IMPORT_STAT_NO_EXIST:
            return result;
        default:
            return MP_IMPORT_STAT_NO_EXIST;
    }
}

mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
    jstring jfilename = JNUPY_CALL(NewStringUTF, filename);

	// [!:JNUPY_NESTED_CALL] lead to nested call
	jstring jcodestr;
	JNUPY_ASSIGN_WITH_NESTED_CALL(jcodestr, CallObjectMethod, JNUPY_PY_JSTATE, JMETHOD(PythonNativeState, readFile), jfilename);

    jsize codelen = JNUPY_CALL(GetStringUTFLength, jcodestr);
    const char* codetmpstr = JNUPY_CALL(GetStringUTFChars, jcodestr, JNI_FALSE);
    char* codestr = m_new(char, codelen);

    memcpy(codestr, codetmpstr, codelen * sizeof(char));

    JNUPY_CALL(ReleaseStringUTFChars, jfilename, NULL);
    JNUPY_CALL(ReleaseStringUTFChars, jcodestr, codetmpstr);

    return mp_lexer_new_from_str_len(qstr_from_str(filename), codestr, codelen, codelen);
}

#if MICROPY_PY_IO
// check unix/file.c

STATIC mp_uint_t jnupy_stdout_write(mp_obj_t self_in, const void *buf, mp_uint_t len, int *errcode) {
    (void)errcode;
    // assert(mp_sys_stdout_obj == self_in);
    jnupy_print_strn((const char *)buf, len);
    return len;
}

STATIC const mp_stream_p_t jnupy_stdout_stream_p = {
    .write = jnupy_stdout_write,
    .is_text = true,
};

const mp_obj_type_t mp_type_jnupy_stdout = {
    { &mp_type_type },
    .stream_p = &jnupy_stdout_stream_p,
};

typedef struct _mp_type_jnupy_stdout_t {
    mp_obj_base_t base;
} mp_type_jnupy_stdout_t;

const mp_type_jnupy_stdout_t mp_sys_stdout_obj = {{&mp_type_jnupy_stdout}};

#if MICROPY_PY_IO_FILEIO
// TODO: JavaStream in java-jnupy?

// mp_type_fileio;
// mp_type_textio;

// mp_sys_stdin_obj
// mp_sys_stdout_obj
// mp_sys_stderr_obj
#endif // MICROPY_PY_IO_FILEIO

mp_obj_t mp_builtin_open(mp_uint_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    // TODO: get JNUPY_PY_JSTATE and call it.
    // ...
    nlr_raise(mp_obj_new_exception_msg(&mp_type_JavaError, "builtin open are not override. (by java side)"));
}

MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 0, mp_builtin_open);
#endif // MICROPY_PY_IO

/** JNUPY INTERNAL MODULE **/

typedef struct _jnupy_jobj_t {
    mp_obj_base_t base;
    jobject jobj;
} jnupy_jobj_t;

const mp_obj_type_t mp_type_jobject;
mp_obj_t jnupy_jobj_new(jobject jobj);
jobject jnupy_jobj_get(mp_obj_t self_in);

typedef struct _mp_obj_jfunc_t {
    mp_obj_base_t base;
    jobject jstate;
    jobject jfunc;
} mp_obj_jfunc_t;

const mp_obj_type_t mp_type_jfunc;
mp_obj_t mp_obj_jfunc_new(jobject jstate, jobject jfunc);
jobject mp_obj_jfunc_get(mp_obj_t self_in);

typedef struct _jnupy_pyref_t {
    mp_obj_base_t base;
    mp_obj_t obj;
    struct _jnupy_pyref_t *prev;
    struct _jnupy_pyref_t *next;
    mp_uint_t count;
} jnupy_pyref_t;

const mp_obj_type_t mp_type_pyref;
mp_obj_t jnupy_pyref_new(mp_obj_t obj);
jnupy_pyref_t *jnupy_pyref_get(jobject jobj);
void jnupy_pyref_clear(jnupy_pyref_t *pyref);

jobject jnupy_obj_py2j_raw(mp_obj_t obj);
jobject jnupy_obj_py2j(mp_obj_t obj);
mp_obj_t jnupy_obj_j2py(jobject obj);

mp_obj_t jnupy_obj_str_new(jstring jstr) {
    const char *strbuf = JNUPY_CALL(GetStringUTFChars, jstr, 0);
    mp_obj_t str_obj = mp_obj_new_str(strbuf, strlen(strbuf), true);
    JNUPY_CALL(ReleaseStringUTFChars, jstr, strbuf);

    return str_obj;
}

mp_obj_t jnupy_getattr(qstr attr) {
    mp_obj_t module_jnupy = mp_module_get(MP_QSTR_jnupy);
    return mp_load_attr(module_jnupy, attr);
}

void jnupy_obj_do_exception(mp_obj_t exception) {
    nlr_gk_buf_t nlr_gk;
    if (nlr_gk_push(&nlr_gk) == 0) {
        do {
            if (exception == NULL) {
                // already exception are thowed (java)
                break;
            }

            if (mp_obj_exception_match(exception, &mp_type_JavaError)) {
                mp_obj_t value = mp_obj_exception_get_value(exception);
                if (MP_OBJ_IS_TYPE(value, &mp_type_jobject)) {
                    jthrowable exc = jnupy_obj_py2j_raw(value);
                    JNUPY_RAW_CALL(Throw, exc);
                    break;
                }
            }

            // TODO: clean up this code...

            vstr_t vstr;
            mp_print_t print;

            vstr_init_print(&vstr, 16, &print);
            mp_obj_print_helper(&print, exception, PRINT_EXC);
            const char *message = vstr_null_terminated_str(&vstr);

            jstring jmsg = JNUPY_CALL(NewStringUTF, message);
            jobject pyref = jnupy_obj_py2j_raw(exception);

            // TODO: detail exception for catch that.
            jthrowable exc = JNUPY_CALL(NewObject, JCLASS(PythonNativeException), JMETHODV(PythonNativeException, INIT, with_pyobj), jmsg, pyref);

            JNUPY_CALL(ReleaseStringUTFChars, jmsg, NULL);
            JNUPY_RAW_CALL(Throw, exc);

            vstr_clear(&vstr);
            break;
        } while (0);
        nlr_gk_pop(&nlr_gk);
    } else {
        JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("org/micropython/jnupy/InternalException", CXMH3), "invaild exception");
    }
}

// TODO: jnupy_pyobj_new -> give old value if already exists.
jobject jnupy_pyobj_new(jobject pythonNativeState, mp_obj_t pyobj) {
    jobject pyState = JNUPY_CALL(NewGlobalRef, pythonNativeState); // is correct?
    mp_obj_t pyref_obj = jnupy_pyref_new(pyobj);

    jobject jobj = JNUPY_CALL(NewObject, JCLASS(PythonObject), JMETHOD(PythonObject, INIT), pyState, (jlong)(void *)JNUPY_MP_STATE, (jlong)(void *)pyref_obj);
    return jobj;
}

jobject jnupy_pyobj_new_from_class(jobject pythonNativeState, mp_obj_t pyobj, jclass class_, jmethodID mid) {
    // class_ must be from PythonObject (should check them...?)
    // JCLASS(PythonObject)
    jobject pyState = JNUPY_CALL(NewGlobalRef, pythonNativeState);
    mp_obj_t pyref_obj = jnupy_pyref_new(pyobj);

    jobject jobj = JNUPY_CALL(NewObject, class_, mid, pyState, (jlong)(void *)JNUPY_MP_STATE, (jlong)(void *)pyref_obj);
    return jobj;
}

mp_obj_t jnupy_pyobj_get(jobject jobj) {
    jnupy_pyref_t *ref = jnupy_pyref_get(jobj);

    if (ref == MP_OBJ_NULL) {
        // if PythonObject.close are called; then refence are now invaild.
        // (but normal java code will never touch refernce counting)
        // TODO: replace exception message?
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ReferenceError, "invaild reference; org.micropython.jnupy.PythonObject are closed."));
    }

    mp_obj_t obj = ref->obj;
    return obj;
}

#define IsInstanceOf(obj, class_) (JNUPY_RAW_CALL(IsInstanceOf, obj, class_) == JNI_TRUE)

mp_obj_t jnupy_obj_j2py(jobject obj) {
    MP_STACK_CHECK();

    // TODO: warp handler for java exception

    if (0) {
    } else if (obj == NULL) {
        return mp_const_none;
    } else if (IsInstanceOf(obj, JCLASS(JavaFunction))) {
        return mp_obj_jfunc_new(JNUPY_PY_JSTATE, obj);
    } else if (IsInstanceOf(obj, JCLASS(JavaObject))) {
        return jnupy_jobj_new(obj);
    } else if (IsInstanceOf(obj, JCLASS(PythonObject))) {
        return jnupy_pyobj_get(obj);
    } else if (IsInstanceOf(obj, JCLASS(Boolean))) {
        jboolean val = JNUPY_CALL(CallBooleanMethod, obj, JMETHOD(Boolean, booleanValue));

        if (val == JNI_TRUE) {
            return mp_const_true;
        } else if (val == JNI_FALSE) {
            return mp_const_false;
        }

        assert(! "invaild control flow");
    } else if (IsInstanceOf(obj, JCLASS(Short))) {
        jint val = JNUPY_CALL(CallIntMethod, obj, JMETHOD(Short, intValue));

        return mp_obj_new_int(val);
    } else if (IsInstanceOf(obj, JCLASS(Integer))) {
        jint val = JNUPY_CALL(CallIntMethod, obj, JMETHOD(Integer, intValue));

        return mp_obj_new_int(val);
    #if MICROPY_LONGINT_IMPL == MICROPY_LONGINT_IMPL_LONGLONG
    } else if (IsInstanceOf(obj, JCLASS(Long))) {
        jlong val = JNUPY_CALL(CallLongMethod, obj, JMETHOD(Long, longValue));

        return mp_obj_new_int_from_ll(val);
    #endif
    } else if (IsInstanceOf(obj, JCLASS(Float))) {
        jfloat val = JNUPY_CALL(CallFloatMethod, obj, JMETHOD(Float, floatValue));

        return mp_obj_new_float(val);
    } else if (IsInstanceOf(obj, JCLASS(Double))) {
        jdouble val = JNUPY_CALL(CallDoubleMethod, obj, JMETHOD(Double, doubleValue));

        return mp_obj_new_float(val);
	} else if (IsInstanceOf(obj, JCLASS(String))) {
		const char *buf = JNUPY_CALL(GetStringUTFChars, obj, JNI_FALSE);
		jsize bufsize = JNUPY_CALL(GetStringUTFLength, obj);

		mp_obj_t pobj = mp_obj_new_str(buf, bufsize, false);

		JNUPY_CALL(ReleaseStringUTFChars, obj, buf);
		return pobj;
    } else if (IsInstanceOf(obj, JCLASS(ByteArray))) {
        jbyte *buf = JNUPY_CALL(GetByteArrayElements, obj, NULL);
		jsize bufsize = JNUPY_CALL(GetArrayLength, obj);

        mp_obj_t pobj = mp_obj_new_bytes((byte *)buf, bufsize);

        JNUPY_CALL(ReleaseByteArrayElements, obj, buf, 0);

        return pobj;
    } else if (IsInstanceOf(obj, JCLASS(ObjectArray))) {
		jsize arrsize = JNUPY_CALL(GetArrayLength, obj);
		mp_obj_t *items = m_new(mp_obj_t, arrsize);

		for (mp_uint_t i = 0; i < arrsize; i++) {
			jobject node = JNUPY_CALL(GetObjectArrayElement, obj, i);
			items[i] = jnupy_obj_j2py(node);
		}

		mp_obj_t pobj = mp_obj_new_tuple(arrsize, items);
		m_free(items, sizeof(mp_obj_t) * arrsize);

		return pobj;
    } else if (0) {
        // TODO: handle dictionary
    } else if (0) {
        // TODO: handle set?
    } else {
        // TODO: change exception (with failed object)
        // JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/IllegalArgumentException", C5K3P), "failed convert object");
        // nlr_gk_jump(NULL); // ANA
		nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "failed convert object"));
    }

    return mp_const_none;
}
#undef IsInstanceOf

jobject jnupy_obj_py2j(mp_obj_t obj) {
    MP_STACK_CHECK();

    if (0) {
    } else if (obj == mp_const_none) {
        return NULL;
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_module)) {
        return jnupy_pyobj_new_from_class(JNUPY_PY_JSTATE, obj, JCLASS(PythonModule), JMETHOD(PythonModule, INIT));
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_jobject)) {
        return jnupy_jobj_get(obj);
    } else if (obj == mp_const_true) {
        return JOBJECT(TRUE);
    } else if (obj == mp_const_false) {
        return JOBJECT(FALSE);
    } else if (MP_OBJ_IS_SMALL_INT(obj)) {
        mp_int_t val = MP_OBJ_SMALL_INT_VALUE(obj);

        jobject jobj = JNUPY_CALL(NewObject, JCLASS(Integer), JMETHOD(Integer, INIT), val);
        return jobj;
    #if MICROPY_LONGINT_IMPL == MICROPY_LONGINT_IMPL_LONGLONG
    } else if (MP_OBJ_IS_INT(obj)) {
        mp_obj_int_t *intobj = obj;
        long long val = intobj->val;

        jobject jobj = JNUPY_CALL(NewObject, JCLASS(Long), JMETHOD(Long, INIT), val);
        return jobj;
    #endif
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_float)) {
        mp_float_t val = mp_obj_get_float(obj);

        jobject jobj = JNUPY_CALL(NewObject, JCLASS(Float), JMETHOD(Float, INIT), (jfloat)val);
        return jobj;
    } else if (MP_OBJ_IS_STR_OR_BYTES(obj)) {
        mp_buffer_info_t objbuf;
        mp_get_buffer_raise(obj, &objbuf, MP_BUFFER_READ);

        jbyteArray bytearr = JNUPY_CALL(NewByteArray, objbuf.len);
        JNUPY_CALL(SetByteArrayRegion, bytearr, 0, objbuf.len, objbuf.buf);

        jobject jobj;
        if (MP_OBJ_IS_STR(obj)) {
            jobj = JNUPY_CALL(NewObject, JCLASS(String), JMETHODV(String, INIT, str), bytearr, 0, objbuf.len, JANY(RUTF8));
			JNUPY_CALL(ReleaseByteArrayElements, bytearr, NULL, JNI_ABORT);
		} else {
            jobj = bytearr;
        }

        return jobj;
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_list) || MP_OBJ_IS_TYPE(obj, &mp_type_tuple)) {
        mp_obj_t iter = mp_getiter(obj);
        mp_int_t size = mp_obj_get_int(mp_obj_len(obj));

        jobjectArray jresult = JNUPY_CALL(NewObjectArray, size, JCLASS(Object), NULL);

        for (mp_int_t i = 0; i < size; i++){
            mp_obj_t cur = mp_iternext(iter);
            JNUPY_CALL(SetObjectArrayElement, jresult, i, jnupy_obj_py2j(cur));
        }

        return jresult;
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_dict)) {
        mp_obj_t iter = mp_getiter(mp_call_function_0(mp_load_attr(obj, MP_QSTR_items)));
        mp_int_t size = mp_obj_get_int(mp_obj_len(obj));
        jobject jresult = JNUPY_CALL(NewObject, JCLASS(HashMap), JMETHOD(HashMap, INIT), size);

        while (true) {
            mp_obj_t cur = mp_iternext(iter);
            if (cur == MP_OBJ_STOP_ITERATION) {
                break;
            }

            mp_uint_t csize = 0;
            mp_obj_t *items = NULL;
            mp_obj_tuple_get(cur, &csize, &items);
            assert(csize == 2);

            jobject jkey = jnupy_obj_py2j(items[0]);
            jobject jvalue = jnupy_obj_py2j(items[1]);
            JNUPY_CALL(CallObjectMethod, jresult, JMETHOD(HashMap, put), jkey, jvalue);
        }

        return jresult;
    } else if (0) {
        // TODO: handle set?
    } else {
        return jnupy_pyobj_new(JNUPY_PY_JSTATE, obj);
    }

    return NULL;
}

jobject jnupy_obj_py2j_raw(mp_obj_t obj) {
    MP_STACK_CHECK();

    if (MP_OBJ_IS_TYPE(obj, &mp_type_jobject)) {
        return jnupy_jobj_get(obj);
    } else {
        return jnupy_pyobj_new(JNUPY_PY_JSTATE, obj);
    }
}

mp_obj_t jnupy_jobj_new(jobject jobj) {
    jnupy_jobj_t *o = m_new_obj_with_finaliser(jnupy_jobj_t);
    o->base.type = &mp_type_jobject;
    o->jobj = JNUPY_CALL(NewGlobalRef, jobj);

    return (mp_obj_t)o;
}

jobject jnupy_jobj_get(mp_obj_t self_in) {
    assert(MP_OBJ_IS_TYPE(self_in, &mp_type_jobject));
    jnupy_jobj_t *self = self_in;
    return self->jobj;
}

STATIC void jobject_print(const mp_print_t *print, mp_obj_t o_in, mp_print_kind_t kind) {
    jobject jobj = jnupy_jobj_get(o_in);

    jclass class_ = JNUPY_CALL(GetObjectClass, jobj);
    jmethodID mid = JNUPY_CALL(GetMethodID, class_, "toString", "()Ljava/lang/String;");

	// [!:JNUPY_NESTED_CALL] lead to nested call
	jstring val;
	JNUPY_ASSIGN_WITH_NESTED_CALL(val, CallObjectMethod, jobj, mid);
    const char *buf = JNUPY_CALL(GetStringUTFChars, val, NULL);

    if (kind == PRINT_REPR) {
        mp_printf(print, "<JObject %s>", buf);
    } else if (kind == PRINT_STR) {
        mp_print_str(print, buf);
    } else {
        // TODO: replace exception type/message.
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "invaild print (jobject_print)"));
    }
}

STATIC mp_obj_t jobject_del(mp_obj_t self_in) {
    jnupy_jobj_t *self = self_in;
    JNUPY_CALL(DeleteGlobalRef, self->jobj);

    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(jobject_del_obj, jobject_del);

STATIC const mp_map_elem_t jobject_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___del__), (mp_obj_t)&jobject_del_obj },
};

STATIC MP_DEFINE_CONST_DICT(jobject_locals_dict, jobject_locals_dict_table);

const mp_obj_type_t mp_type_jobject = {
    { &mp_type_type },
    .name = MP_QSTR_JObject,
    .print = jobject_print,
    .locals_dict = (mp_obj_t)&jobject_locals_dict,
};

mp_obj_t mp_obj_jfunc_new(jobject jstate, jobject jfunc) {
    mp_obj_jfunc_t *o = m_new_obj_with_finaliser(mp_obj_jfunc_t);
    o->base.type = &mp_type_jfunc;
    o->jstate = JNUPY_CALL(NewGlobalRef, jstate);
    o->jfunc = JNUPY_CALL(NewGlobalRef, jfunc);
    // TODO: get name?

    return (mp_obj_t)o;
}

jobject mp_obj_jfunc_get(mp_obj_t self_in) {
    assert(MP_OBJ_IS_TYPE(self_in, &mp_type_jfunc));
    mp_obj_jfunc_t *self = self_in;
    return self->jfunc;
}

NORETURN void jnupy_throw_jerror(jthrowable jerror) {
    JNUPY_RAW_CALL1(ExceptionClear);
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t pyexc = MP_OBJ_NULL;
		bool is_exception = JNUPY_RAW_CALL(IsInstanceOf, jerror, JCLASS(PythonNativeException));
	    if (is_exception) {
            jobject pyref = JNUPY_RAW_CALL(GetObjectField, jerror, JFIELD(PythonNativeException, pyobj));
            pyexc = jnupy_obj_j2py(pyref);
            if (pyexc != MP_OBJ_NULL) {
                nlr_pop();
                nlr_raise(pyexc);
            }
        }

	    pyexc = jnupy_obj_j2py(jerror);

	    nlr_pop();
	    nlr_raise(mp_obj_new_exception_arg1(&mp_type_JavaError, pyexc));
    } else {
	    nlr_raise(mp_obj_new_exception_msg(&mp_type_JavaError, "Unknown java error"));
    }

    abort();
}

STATIC mp_obj_t jfunc_call(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_obj_jfunc_t *o = self_in;
    jobject jfunc = o->jfunc;

    JNUPY_PY_JSTATE = o->jstate;

	// [!:JNUPY_NESTED_CALL] lead to nested call
    JNUPY_VOID_NESTED_CALL(CallVoidMethod, jfunc, JMETHOD(JavaFunction, checkInvoke), JNUPY_PY_JSTATE, (jint)n_args, (jint)n_kw);

    jobjectArray jargarr = JNUPY_CALL(NewObjectArray, n_args, JCLASS(PythonObject), NULL);
    jobject jkwargs = NULL;

    for (int i = 0; i < n_args; i++) {
        jobject jarg = jnupy_pyobj_new(JNUPY_PY_JSTATE, args[i]);
        JNUPY_CALL(SetObjectArrayElement, jargarr, i, jarg);
        JNUPY_CALL(DeleteLocalRef, jarg);
    }

    if (n_kw != 0) {
        jkwargs = JNUPY_CALL(NewObject, JCLASS(HashMap), JMETHOD(HashMap, INIT), n_kw);

        const mp_obj_t *kwargs = args[n_args + 1];
        for (int i = 0; i < n_kw; i += 2) {
            jstring jkey = jnupy_obj_py2j(kwargs[i]);
            jobject jvalue = jnupy_pyobj_new(JNUPY_PY_JSTATE, kwargs[i + 1]);
            JNUPY_CALL(CallVoidMethod, jkwargs, JMETHOD(HashMap, put), jkey, jvalue);
            JNUPY_CALL(DeleteLocalRef, jkey);
            JNUPY_CALL(DeleteLocalRef, jvalue);
        }
    }

    jobject jargs = JNUPY_CALL(NewObject, JCLASS(PythonArguments), JMETHOD(PythonArguments, INIT), jargarr, jkwargs);
    jobject jresult = NULL;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
		// [!:JNUPY_NESTED_CALL] lead to nested call
		JNUPY_ASSIGN_WITH_NESTED_CALL(jresult, CallObjectMethod, jfunc, JMETHOD(JavaFunction, invoke), JNUPY_PY_JSTATE, jargs);
        nlr_pop();
    } else {
        // TODO: if jname instanceof NamedJavaFunction, then just call NamedJavaFunction with getName
        //  => JavaFunction, file=<java>; lineno=0; block=classname
        //  => NamedJavaFunction,  file=<java: 'classname'>; lineno=0; block=funcname
        jclass cls = JNUPY_CALL(GetObjectClass, jfunc);
        jstring jname = JNUPY_CALL(CallObjectMethod, cls, JMETHOD(Class, getName));

        const char *name = JNUPY_CALL(GetStringUTFChars, jname, JNI_FALSE);
        qstr qname = qstr_from_str(name);
        JNUPY_CALL(ReleaseStringUTFChars, jname, name);

        mp_obj_exception_add_traceback(nlr.ret_val, MP_QSTR__lt_java_gt_, 0, qname);
        nlr_raise(nlr.ret_val);
    }

    mp_obj_t result = jnupy_obj_j2py(jresult);
	return result;
}

STATIC mp_obj_t jfunc_del(mp_obj_t self_in) {
    mp_obj_jfunc_t *o = self_in;
    JNUPY_CALL(DeleteGlobalRef, o->jstate);
    JNUPY_CALL(DeleteGlobalRef, o->jfunc);

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

mp_obj_t jnupy_pyref_new(mp_obj_t obj) {
    jnupy_pyref_t *o = m_new_obj(jnupy_pyref_t);
    jnupy_pyref_t *last = (jnupy_pyref_t *)MP_STATE_VM(jnupy_last_pyref);

    o->base.type = &mp_type_pyref;
    o->obj = obj;
    o->prev = NULL;
    o->next = NULL;
    o->count = 0;

    if (last != NULL) {
        o->prev = last;
        last->next = o;
    }

    MP_STATE_VM(jnupy_last_pyref) = (mp_obj_t)o;
    return (mp_obj_t)o;
}

jnupy_pyref_t *jnupy_pyref_get(jobject jobj) {
    assert(JNUPY_RAW_CALL(IsInstanceOf, jobj, JCLASS(PythonObject)) == JNI_TRUE);

    mp_obj_t refobj = (mp_obj_t)JNUPY_CALL(GetLongField, jobj, JFIELD(PythonObject, refObject));
    jnupy_pyref_t *ref = refobj;

    return ref;
}

void jnupy_pyref_clear(jnupy_pyref_t *pyref) {
    jnupy_pyref_t *last = (jnupy_pyref_t *)MP_STATE_VM(jnupy_last_pyref);

    if (pyref->prev == NULL && pyref->next == NULL) {
        MP_STATE_VM(jnupy_last_pyref) = NULL;
    } else {
        if (pyref->prev != NULL) {
            pyref->prev->next = pyref->next;
            if (last == pyref) {
                MP_STATE_VM(jnupy_last_pyref) = pyref->prev;
            }
        }

        if (pyref->next != NULL) {
            pyref->next->prev = pyref->prev;
            if (last == pyref) {
                // XXX This is possible?
                MP_STATE_VM(jnupy_last_pyref) = pyref->next;
                assert(! "invaild reference");
            }
        }
    }
}

const mp_obj_type_t mp_type_pyref = {
    { &mp_type_type },
    .name = MP_QSTR_PyRef,
};

STATIC mp_obj_t mod_jnupy_test() {
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_jnupy_test_obj, mod_jnupy_test);

STATIC mp_obj_t mod_jnupy_get_state_ident() {
    #if MICROPY_MULTI_STATE_CONTEXT
    return mp_obj_new_int((mp_uint_t)mp_state_ctx);
    #else
    return mp_const_none;
    #endif
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_jnupy_get_state_ident_obj, mod_jnupy_get_state_ident);

STATIC mp_obj_t mod_jnupy_get_loaded_modules(void) {
    return MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_jnupy_get_loaded_modules_obj, mod_jnupy_get_loaded_modules);

STATIC mp_obj_t mod_jnupy_get_version(mp_obj_t nameobj) {
    qstr qname = mp_obj_str_get_qstr(nameobj);
    const char *buf = NULL;

    if (0) {
    } else if (qname == MP_QSTR_MICROPY_GIT_TAG) {
        buf = MICROPY_GIT_TAG;
    } else if (qname == MP_QSTR_MICROPY_GIT_HASH) {
        buf = MICROPY_GIT_HASH;
    } else if (qname == MP_QSTR_MICROPY_BUILD_DATE) {
        buf = MICROPY_BUILD_DATE;
    } else if (qname == MP_QSTR_MICROPY_VERSION_STRING) {
        buf = MICROPY_VERSION_STRING;
    } else {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "invaild string"));
    }

    return mp_obj_new_str(buf, strlen(buf), false);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_jnupy_get_version_obj, mod_jnupy_get_version);

STATIC const mp_map_elem_t mp_module_ujnupy_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_micropython) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_JObject), (mp_obj_t)&mp_type_jobject },
    { MP_OBJ_NEW_QSTR(MP_QSTR_JFunction), (mp_obj_t)&mp_type_jfunc },
    { MP_OBJ_NEW_QSTR(MP_QSTR_PyRef), (mp_obj_t)&mp_type_pyref },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test), (mp_obj_t)&mod_jnupy_test_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_state_ident), (mp_obj_t)&mod_jnupy_get_state_ident_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_builtin_modules), (mp_obj_t)&mp_builtin_module_dict },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_loaded_modules), (mp_obj_t)&mod_jnupy_get_loaded_modules_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_version), (mp_obj_t)&mod_jnupy_get_version_obj },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_ujnupy_globals, mp_module_ujnupy_globals_table);

const mp_obj_module_t mp_module_ujnupy = {
    .base = { &mp_type_module },
    .name = MP_QSTR_ujnupy,
    .globals = (mp_obj_dict_t*)&mp_module_ujnupy_globals,
};

/** JNI EXPORT FUNCTION HEADER MECRO **/
#define JNUPY_FUNC(name) Java_org_micropython_jnupy_PythonNativeState_##name
#define JNUPY_FUNC_DEF(ret, name) \
    JNIEXPORT ret JNICALL JNUPY_FUNC(name)

/** JNI STATIC EXPORT FUNCTIONS (org.micropython.jnupy.PythonNativeState) **/
JNUPY_FUNC_DEF(jstring, jnupy_1mp_1version)
    (JNIEnv *env, jobject self) {
    return (*env)->NewStringUTF(env, MICROPY_VERSION_STRING);
}

/** JNI EXPORT FUNCTION BODY MECRO **/
// TODO: re-compute stack_limit for setuped stack_top...
// TODO: it is possible compute stack_top?
//	  -> use [!:JNUPY_NESTED_CALL] for deal with nested call.

void jnupy_func_stack_with_state(jnupy_func_stack_t *jfs) {
    jfs->is_first_load = !mp_state_is_loaded(JNUPY_MP_STATE);
    if (jfs->is_first_load) {
        mp_state_load(JNUPY_MP_STATE);
    }

    jfs->stack_top = MP_STATE_VM(stack_top);
	
	jfs->prev = JNUPY_JFS_TOP;
	JNUPY_JFS_TOP = jfs;
}

void jnupy_func_stack_error(jnupy_func_stack_t *jfs) {
    jfs->has_exception = true;
    jnupy_obj_do_exception(jfs->_nlr_gk.buf.ret_val);
}

void jnupy_func_stack_leave(jnupy_func_stack_t *jfs) {
    if (!jfs->before_return) {
        jfs->before_return = true;

        if (jfs->with_state && JNUPY_MP_STATE != NULL && JNUPY_MP_STATE == MP_STATE_CTX_PTR) {
			MP_STATE_VM(stack_top) = jfs->stack_top;
            if (jfs->is_first_load) {
                gc_collect();
                mp_state_store(JNUPY_MP_STATE);
            }
		}
    }

    if (!jfs->has_exception) {
        (*JNUPY_ENV)->ExceptionClear(JNUPY_ENV);
    }

    nlr_gk_pop(&jfs->_nlr_gk);
    mp_nlr_top = jfs->nlr_ptr;
	JNUPY_JFS_TOP = jfs->prev;
}

#define _JNUPY_FUNC_BODY_START(_with_state, init_expr) \
    jnupy_setup_env(env, self); \
    jnupy_func_stack_t _jfs = { \
		.prev = NULL, \
        .with_state = _with_state, \
        .is_first_load = false, \
        .has_exception = false, \
        .before_return = false, \
		.stack_last = NULL, \
        .stack_top = NULL, \
        .nlr_ptr = mp_nlr_top, \
        ._nlr_gk = nlr_gk_new(), \
    }; \
    if (init_expr) { \
        if (_jfs.with_state) { \
            jnupy_func_stack_with_state(&_jfs); \
            mp_stack_ctrl_init(); \
        } \
        do { \
            if (nlr_gk_push(&_jfs._nlr_gk) != 0) { \
                jnupy_func_stack_error(&_jfs); \
                break; \
            }
            /* body */

#define _JNUPY_FUNC_BODY_END(ret_stmt) \
        } while(0); \
    } \
    ret_stmt;

#define return \
    jnupy_func_stack_leave(&_jfs); \
    return

#define JNUPY_FUNC_START_WITH_STATE _JNUPY_FUNC_BODY_START(true, jnupy_load_state_from_pythonnativestate(self))
#define JNUPY_FUNC_START _JNUPY_FUNC_BODY_START(false, true)
#define JNUPY_FUNC_END_VOID _JNUPY_FUNC_BODY_END(return)
#define JNUPY_FUNC_END_VALUE(value) _JNUPY_FUNC_BODY_END(return (value))
#define JNUPY_FUNC_END _JNUPY_FUNC_BODY_END(return 0)

/** JNI EXPORT FUNCTIONS (org.micropython.jnupy.PythonNativeState) **/
JNUPY_FUNC_DEF(jboolean, jnupy_1state_1new)
    (JNIEnv *env, jobject self, jlong stack_size, jlong heap_size) {
    JNUPY_FUNC_START;

    if (!initialized) {
        return JNI_FALSE;
    }

    mp_state_ctx_t *state = NULL;

    state = jnupy_get_state_from_pythonnativestate(JNUPY_SELF);
	if (state != NULL) {
	    // Already state are exists.
	    return JNI_FALSE;
	}

	state = mp_state_new();
	if (state == NULL) {
	    // Create state are failed.
	    return JNI_FALSE;
	}

    char *heap = NULL;

    nlr_gk_buf_t nlr_gk = nlr_gk_new();
    if (nlr_gk_push(&nlr_gk) == 0) {
        mp_state_force_load(state);
        mp_stack_set_limit(stack_size);

        heap = malloc(heap_size);
        assert(heap != NULL);

        gc_init(heap, heap + heap_size);

        assert((char *)MP_STATE_MEM(gc_alloc_table_start) == heap);

        mp_init();

        mp_obj_list_init(mp_sys_path, 0);
        mp_obj_list_init(mp_sys_argv, 0);

        mp_obj_module_t *module_jnupy = mp_obj_new_module(MP_QSTR_jnupy);
        mp_obj_dict_t *module_jnupy_dict = mp_call_function_0(mp_load_attr(mp_module_ujnupy.globals, MP_QSTR_copy));
        module_jnupy->globals = module_jnupy_dict;

        mp_state_store(state);
        JNUPY_CALL(SetLongField, JNUPY_SELF, JFIELD(PythonNativeState, mpState), (mp_uint_t)state);
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

JNUPY_FUNC_DEF(jboolean, jnupy_1state_1check)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START;

    mp_state_ctx_t *state = jnupy_get_state_from_pythonnativestate(JNUPY_SELF);

	if (state != NULL) {
	    return JNI_TRUE;
	} else {
	    return JNI_FALSE;
	}

    JNUPY_FUNC_END;
}

JNUPY_FUNC_DEF(void, jnupy_1state_1free)
    (JNIEnv *env, jobject self) {
    JNUPY_FUNC_START_WITH_STATE;

    assert(mp_state_is_loaded(JNUPY_MP_STATE));
    JNUPY_CALL(SetLongField, JNUPY_SELF, JFIELD(PythonNativeState, mpState), 0);

    gc_collect();

    mp_deinit();
    free(MP_STATE_MEM(gc_alloc_table_start));
    mp_state_store(JNUPY_MP_STATE);

    mp_state_free(JNUPY_MP_STATE);
    JNUPY_MP_STATE = NULL;

    JNUPY_FUNC_END_VOID;
}

JNUPY_FUNC_DEF(jobject, jnupy_1code_1compile)
    (JNIEnv *env, jobject self, jstring code, jobject jkind) {
    JNUPY_FUNC_START_WITH_STATE;

    qstr name = qstr_from_str("<CODE from JAVA>");
    const char *codebuf = JNUPY_CALL(GetStringUTFChars, code, 0);

    mp_lexer_t *lex = mp_lexer_new_from_str_len(name, codebuf, strlen(codebuf), 0);

    if (lex == NULL) {
        JNUPY_CALL(ReleaseStringUTFChars, code, codebuf);
        return NULL;
    }

    jint jflag = JNUPY_CALL(CallIntMethod, jkind, JMETHOD(PythonParseInputKind, ordinal));
    mp_parse_input_kind_t flag;
    switch (jflag) {
        case MP_PARSE_SINGLE_INPUT:
        case MP_PARSE_FILE_INPUT:
        case MP_PARSE_EVAL_INPUT:
            flag = jflag;
            break;
        default:
            JNUPY_RAW_CALL(ThrowNew, JNUPY_CLASS("java/lang/IllegalArgumentException", C5K3P), "compile flag is invaild");
            return NULL;
    }

    qstr source_name = lex->source_name;
    mp_parse_tree_t parse_tree = mp_parse(lex, flag);

    JNUPY_CALL(ReleaseStringUTFChars, code, codebuf);

    mp_obj_t module_fun = mp_compile(&parse_tree, source_name, MP_EMIT_OPT_NONE, false);
    if (module_fun == NULL) {
        return NULL;
    }

    jobject jresult = jnupy_obj_py2j_raw(module_fun);
    return jresult;

    JNUPY_FUNC_END_VALUE(NULL);
}

JNUPY_FUNC_DEF(void, jnupy_1ref_1incr)
    (JNIEnv *env, jobject self, jlong refid) {
    JNUPY_FUNC_START_WITH_STATE;

    jnupy_pyref_t *pyref = (jnupy_pyref_t *)refid;

    if (pyref != NULL) {
        pyref->count++;
    } else {
        // invaild
        assert(! "invaild pointer while reference counting");
    }

    JNUPY_FUNC_END_VOID;
}

JNUPY_FUNC_DEF(void, jnupy_1ref_1derc)
    (JNIEnv *env, jobject self, jlong refid) {
    JNUPY_FUNC_START_WITH_STATE;

    jnupy_pyref_t *pyref = (jnupy_pyref_t *)refid;

    if (pyref != NULL) {
        pyref->count--;
        if (pyref->count <= 0) {
            jnupy_pyref_clear(pyref);
        }
    } else {
        // invaild
        assert(! "invaild pointer while reference counting");
    }

    JNUPY_FUNC_END_VOID;
}

JNUPY_FUNC_DEF(jobject, jnupy_1func_1call) // jnupy_func_call
    (JNIEnv *env, jobject self, jboolean convertResult, jobject pyref, jarray jargs) {
    JNUPY_FUNC_START_WITH_STATE;

    mp_obj_t *args = MP_OBJ_NULL;
    jsize jargs_length = 0;
    jobject jresult = NULL;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t func = jnupy_obj_j2py(pyref);

        jargs_length = JNUPY_CALL(GetArrayLength, jargs);
        args = m_new(mp_obj_t, jargs_length);

        for (mp_uint_t i = 0; i < jargs_length; i++) {
            jobject jarg = JNUPY_CALL(GetObjectArrayElement, jargs, i);
            mp_obj_t arg = jnupy_obj_j2py(jarg);
            args[i] = arg;
        }

        mp_obj_t result = mp_call_function_n_kw(func, jargs_length, 0, args);

        if (convertResult) {
            jresult = jnupy_obj_py2j(result);
        } else {
            jresult = jnupy_obj_py2j_raw(result);
        }

        if (args != MP_OBJ_NULL) {
            // m_free(args, jargs_length);
        }

        nlr_pop();
    } else {
        if (args != MP_OBJ_NULL) {
            // m_free(args, jargs_length);
        }

        nlr_gk_jump(nlr.ret_val);
    }

    return jresult;
    JNUPY_FUNC_END_VALUE(NULL);
}

JNUPY_FUNC_DEF(jobject, jnupy_1module_1new)
    (JNIEnv *env, jobject self, jstring jname) {
    JNUPY_FUNC_START_WITH_STATE;

    const char *namebuf = JNUPY_CALL(GetStringUTFChars, jname, 0);
    jobject jresult = NULL;
    mp_obj_t module = NULL;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        module = mp_obj_new_module(qstr_from_str(namebuf));
        nlr_pop();
        JNUPY_CALL(ReleaseStringUTFChars, jname, namebuf);
    } else {
        JNUPY_CALL(ReleaseStringUTFChars, jname, namebuf);
        nlr_raise(nlr.ret_val);
    }

    jresult = jnupy_obj_py2j_raw(module);

    return jresult;
    JNUPY_FUNC_END_VALUE(NULL);
}

JNUPY_FUNC_DEF(jobject, jnupy_1obj_1py2j)
    (JNIEnv *env, jobject self, jobject jpyref) {
    JNUPY_FUNC_START_WITH_STATE;

    mp_obj_t pyref = jnupy_obj_j2py(jpyref);
    jobject jresult = jnupy_obj_py2j(pyref);

    return jresult;
    JNUPY_FUNC_END_VALUE(NULL);
}

JNUPY_FUNC_DEF(void, jnupy_1func_1arg_1check_1num)
    (JNIEnv *env, jobject self, jint n_args, jint n_kw, jint n_args_min, jint n_args_max, jboolean takes_kw) {
    JNUPY_FUNC_START_WITH_STATE;

    mp_arg_check_num((mp_uint_t)n_args, (mp_uint_t)n_kw, (mp_uint_t)n_args_min, (mp_uint_t)n_args_max, (bool)takes_kw);

    JNUPY_FUNC_END_VOID;
}

/** JNI EXPORT FUNCTION MECRO CLNEAUP **/
#undef return
