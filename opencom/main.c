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
#include "py/gc.h"
#include "py/bc.h"
#include "py/stackctrl.h"
#include "py/pfenv.h"
#include "genhdr/py-version.h"

// Command line options, with their defaults
STATIC uint emit_opt = MP_EMIT_OPT_NONE;
mp_uint_t mp_verbose_flag = 0;

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
    mp_obj_print_exception(printf_wrapper, NULL, exc);
    return 1;
}

// Returns standard error codes: 0 for success, 1 for all other errors,
// except if FORCED_EXIT bit is set then script raised SystemExit and the
// value of the exit is in the lower 8 bits of the return value
STATIC int execute_from_lexer(mp_lexer_t *lex) {
    if (lex == NULL) {
        printf("MemoryError: lexer could not allocate memory\n");
        return 1;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        qstr source_name = lex->source_name;

        #if MICROPY_PY___FILE__
        mp_store_global(MP_QSTR___file__, MP_OBJ_NEW_QSTR(source_name));
        #endif

        mp_parse_node_t pn = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(pn, source_name, emit_opt, false);

        // execute it
        // mp_call_function_0(module_fun);
        // mp_call_function_0(module_fun) are changed to:
        mp_code_state *code_state = mp_obj_fun_bc_prepare_codestate(module_fun, 0, 0, NULL);
        mp_vm_return_kind_t kind = MP_VM_RETURN_PAUSE;
        code_state->current = code_state;

        while (true){ // it will be mainloop.
            kind = mp_resume_bytecode(code_state, code_state->current, NULL);
            if (kind == MP_VM_RETURN_PAUSE){
                // TODO: change object to throw event?
                *(code_state->current->sp) = mp_const_true;
            } else if (kind == MP_VM_RETURN_EXCEPTION) {
                nlr_raise(code_state->current->state[code_state->current->n_state - 1]);
                return 1;
            } else {
                assert(code_state->current == code_state);
                // maybe yield or return?
                break;
            }
        }
        
        code_state->current = NULL;
        nlr_pop();
        
        return 0;
    } else {
        // uncaught exception
        return handle_uncaught_exception((mp_obj_t)nlr.ret_val);
    }
}

STATIC int do_file(const char *file) {
    mp_lexer_t *lex = mp_lexer_new_from_file(file);
    return execute_from_lexer(lex);
}

STATIC void set_sys_argv(char *argv[], int argc, int start_arg) {
    for (int i = start_arg; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, MP_OBJ_NEW_QSTR(qstr_from_str(argv[i])));
    }
}

STATIC int usage(char **argv) {
    printf("usage: ./micropython [-X emit=bytecode] file [arg ...]\n");
    return 1;
}

// Process options which set interpreter init options
STATIC void pre_process_options(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        if (argv[a][0] == '-') {
            if (strcmp(argv[a], "-X") == 0) {
                if (a + 1 >= argc) {
                    exit(usage(argv));
                }
                if (0) {
                } else if (strcmp(argv[a + 1], "emit=bytecode") == 0) {
                    emit_opt = MP_EMIT_OPT_BYTECODE;
                } else {
                    exit(usage(argv));
                }
                a++;
            }
        }
    }
}

int main(int argc, char **argv) {
    pre_process_options(argc, argv);
    mp_stack_set_limit(40000 * (BYTES_PER_WORD / 4));

#if MICROPY_ENABLE_GC
    // Heap size of GC heap (if enabled)
    // Make it larger on a 64 bit machine, because pointers are larger.
    long heap_size = 128*1024 * (sizeof(mp_uint_t) / 4);
    char *heap = malloc(heap_size);
    gc_init(heap, heap + heap_size);
#endif
    
    mp_init();
    mp_obj_list_init(mp_sys_argv, 0);

    const int NOTHING_EXECUTED = -2;
    int ret = NOTHING_EXECUTED;
    for (int a = 1; a < argc; a++) {
        if (argv[a][0] == '-') {
            if (strcmp(argv[a], "-X") == 0) {
                a += 1;
            } else {
                return usage(argv);
            }
        } else {
            if (a + 1 <= argc){
                set_sys_argv(argv, argc, a);
                ret = do_file(argv[a]);
            } else {
                // TODO: get bios path?
                return usage(argv);
            }
        }
    }

    mp_deinit();

#if MICROPY_ENABLE_GC && !defined(NDEBUG)
    // We don't really need to free memory since we are about to exit the
    // process, but doing so helps to find memory leaks.
    free(heap);
#endif

    return ret & 0xff;
}

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

void nlr_jump_fail(void *val) {
    printf("FATAL: uncaught NLR %p\n", val);
    exit(1);
}
