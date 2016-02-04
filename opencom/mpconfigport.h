/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

// TODO: crash with windows/mpconfigport.h

#if defined ( _MSC_VER ) || defined ( __MINGW32__ )
#define MP_WIN
#else
#define MP_NWIN
#endif

// for windows only
#ifdef MP_WIN
#define MICROPY_MULTI_STATE_CONTEXT (1)
#define MICROPY_NLR_SETJMP			(1)
#define MICROPY_OVERRIDE_ASSERT_FAIL (1) // ...
#define MICROPY_PY_BUILTINS_FLOAT	(1)
#endif

// for OpenComptuers
#define MICROPY_BUILD_JNI_LIBRARY   (1)
#define MICROPY_ALLOW_PAUSE_VM      (1)
#define MICROPY_LIMIT_CPU           (1)
#define MICROPY_GCREGS_SETJMP       (1)
#define MICROPY_PY_COMPILE_SINGLE_WITH_REPL (1)
#define MICROPY_OBJ_BC_HAVE_RAW_CODE   (1) // for copy bytecode.

// options to control how Micro Python is built
#define MICROPY_COMP_MODULE_CONST   (1)
#define MICROPY_COMP_TRIPLE_TUPLE_ASSIGN (1)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_ENABLE_FINALISER    (1)
#define MICROPY_STACK_CHECK         (1)
#define MICROPY_MALLOC_USES_ALLOCATED_SIZE (1)
#define MICROPY_MEM_STATS           (1)
#define MICROPY_DEBUG_PRINTERS      (1)
#define MICROPY_USE_READLINE_HISTORY (0) // Default: 1
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_HELPER_LEXER_UNIX   (0) // Default: 1
#define MICROPY_ENABLE_SOURCE_LINE  (1)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_LONGLONG) // Default: MICROPY_LONGINT_IMPL_MPZ
#define MICROPY_STREAMS_NON_BLOCK   (1)
#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (1) // Default: 1
#define MICROPY_CAN_OVERRIDE_BUILTINS (1)
#define MICROPY_PY_FUNCTION_ATTRS   (1)
#define MICROPY_PY_DESCRIPTORS      (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_PY_BUILTINS_STR_SPLITLINES (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)
#define MICROPY_PY_BUILTINS_FROZENSET (1)
#define MICROPY_PY_BUILTINS_COMPILE (1)
#define MICROPY_PY_BUILTINS_EXECFILE (1) // Default: 0
#define MICROPY_PY_BUILTINS_NOTIMPLEMENTED (1)
#define MICROPY_PY_MICROPYTHON_MEM_INFO (1)
#define MICROPY_PY_ALL_SPECIAL_METHODS (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN (1)
#define MICROPY_PY_SYS_EXIT         (1)
#define MICROPY_PY_SYS_PLATFORM     "jnupy"
#define MICROPY_PY_SYS_MAXSIZE      (1)
#define MICROPY_PY_SYS_STDFILES     (0) // TODO: jnupy with stdout (lazy) (Default: 1)
#define MICROPY_PY_SYS_EXC_INFO     (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT (1)
#define MICROPY_PY_MATH_SPECIAL_FUNCTIONS (1)
#define MICROPY_PY_CMATH            (1)
#define MICROPY_PY_IO               (1) // TODO: jnupy open file (lazy) (Default: 1)
#define MICROPY_PY_IO_FILEIO        (0) // TODO: jnupy with stdout (lazy) (Default: 1)
#define MICROPY_PY_GC_COLLECT_RETVAL (1)
#define MICROPY_MODULE_FROZEN       (0) // Default: 1

#define MICROPY_STACKLESS           (1) // Default: 0
#define MICROPY_STACKLESS_STRICT    (1) // Default: 0
#define MICROPY_STACKLESS_EXTRA     (1) // Default: ...

#define MICROPY_PY_UCTYPES          (1)
#define MICROPY_PY_UZLIB            (1)
#define MICROPY_PY_UJSON            (1)
#define MICROPY_PY_URE              (1)
#define MICROPY_PY_UHEAPQ           (1)
#define MICROPY_PY_UHASHLIB         (1)
#define MICROPY_PY_UBINASCII        (1)
#define MICROPY_PY_MACHINE          (1)

// Define to MICROPY_ERROR_REPORTING_DETAILED to get function, etc.
// names in exception messages (may require more RAM).
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_WARNINGS            (1)

#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF   (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE  (256)

#ifdef MP_WIN
// #define MICROPY_PORT_INIT_FUNC      init()
// #define MICROPY_PORT_DEINIT_FUNC    deinit()
#endif

// Porting Program's internal modules.
extern const struct _mp_obj_module_t mp_module_microthread;
extern const struct _mp_obj_module_t mp_module_persist;
extern const struct _mp_obj_module_t mp_module_ujnupy;

#if MICROPY_BUILD_JNI_LIBRARY
#define MICROPY_PY_JNUPY_DEF { MP_OBJ_NEW_QSTR(MP_QSTR_ujnupy), (mp_obj_t)&mp_module_ujnupy },
#else
#define MICROPY_PY_JNUPY_DEF
#endif
#if MICROPY_PY_PERSIST
#define MICROPY_PY_PERSIST_DEF { MP_OBJ_NEW_QSTR(MP_QSTR_upersist), (mp_obj_t)&mp_module_persist },
#else
#define MICROPY_PY_PERSIST_DEF
#endif
#if MICROPY_ALLOW_PAUSE_VM
#define MICROPY_PY_MICROTHREAD_DEF { MP_OBJ_NEW_QSTR(MP_QSTR_umicrothread), (mp_obj_t)&mp_module_microthread },
#else
#define MICROPY_PY_MICROTHREAD_DEF
#endif

#define MICROPY_PORT_BUILTIN_MODULES \
    MICROPY_PY_JNUPY_DEF \
    MICROPY_PY_MICROTHREAD_DEF \
    MICROPY_PY_PERSIST_DEF

// type definitions for the specific machine
#if defined ( _MSC_VER ) && defined( _WIN64 )
typedef __int64 mp_int_t;
typedef unsigned __int64 mp_uint_t;
#elif defined( __LP64__ )
typedef long mp_int_t; // must be pointer size
typedef unsigned long mp_uint_t; // must be pointer size
#else
// These are definitions for machines where sizeof(int) == sizeof(void*),
// regardless for actual size.
typedef int mp_int_t; // must be pointer size
typedef unsigned int mp_uint_t; // must be pointer size
#endif

#ifdef MP_WIN
#define MICROPY_OPT_COMPUTED_GOTO   (0) // Default: 1
#else
#define MICROPY_OPT_COMPUTED_GOTO   (1) // Default: 1
#endif

#define BYTES_PER_WORD sizeof(mp_int_t)

#ifdef MP_WIN
// Just assume Windows is little-endian - mingw32 gcc doesn't
// define standard endianness macros.
#define MP_ENDIANNESS_LITTLE (1)
#endif

#ifdef MP_WIN
#define THREAD __declspec(thread)
#define NORETURN __declspec(noreturn)
#endif

// Cannot include <sys/types.h>, as it may lead to symbol name clashes
#if _FILE_OFFSET_BITS == 64 && !defined(__LP64__)
typedef long long mp_off_t;
#else
typedef long mp_off_t;
#endif

typedef void *machine_ptr_t; // must be of pointer size
typedef const void *machine_const_ptr_t; // must be of pointer size

extern const struct _mp_obj_fun_builtin_t mp_builtin_open_obj;

#define MICROPY_PORT_BUILTINS

// So, gc can scan pyref linked list.
#if MICROPY_BUILD_JNI_LIBRARY
#define MICROPY_PORT_ROOT_POINTERS \
    mp_obj_t jnupy_last_pyref;
#else
#define MICROPY_PORT_ROOT_POINTERS
#endif

// We need to provide a declaration/definition of alloca()
#if defined( MP_WIN )
#include <malloc.h>
#elif defined( __FreeBSD__ )
#include <stdlib.h>
#else
#include <alloca.h>
#endif

// MSVC specifics
#ifdef _MSC_VER

// Sanity check

#if ( _MSC_VER < 1800 )
#error Can only build with Visual Studio 2013 toolset
#endif


// CL specific overrides from mpconfig

#define NORETURN                    __declspec(noreturn)
#define MP_LIKELY(x)                (x)
#define MP_UNLIKELY(x)              (x)
#define MICROPY_PORT_CONSTANTS      { "dummy", 0 } //can't have zero-sized array
#ifdef _WIN64
#define MP_SSIZE_MAX                _I64_MAX
#else
#define MP_SSIZE_MAX                _I32_MAX
#endif


// CL specific definitions

#define restrict
#define inline                      __inline
#define alignof(t)                  __alignof(t)
#define STDIN_FILENO                0
#define STDOUT_FILENO               1
#define STDERR_FILENO               2
#define PATH_MAX                    MICROPY_ALLOC_PATH_MAX
#define S_ISREG(m)                  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)                  (((m) & S_IFMT) == S_IFDIR)


// System headers (needed e.g. for nlr.h)

#include <stddef.h> //for NULL
#include <assert.h> //for assert

#endif

#define MP_PLAT_PRINT_STRN(str, len) jnupy_print_strn(str, len)
void jnupy_print_strn(const char *str, mp_uint_t len);
