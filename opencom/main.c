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
#include "py/repl.h"
#include "py/gc.h"
#include "py/bc.h"
#include "py/cpuctrl.h"
#include "py/stackctrl.h"
#include "py/statectrl.h"
#include "py/objmodule.h"
#include "modmicrothread.h"
#include "genhdr/mpversion.h"

#define _MEM_SIZE_B  (1)
#define _MEM_SIZE_KB (1024)
#define _MEM_SIZE_MB (1024 * 1024)

#define MEM_SIZE(x, y) ((x) * _MEM_SIZE_##y * (BYTES_PER_WORD / 4))

// Command line options, with their defaults
STATIC uint emit_opt = MP_EMIT_OPT_NONE;
STATIC char *progname = "?";
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

STATIC bool execute(mp_state_ctx_t *state, mp_obj_t thread){
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

STATIC void set_sys_argv(char *argv[], int argc, int start_arg) {
    for (int i = start_arg; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, MP_OBJ_NEW_QSTR(qstr_from_str(argv[i])));
    }
}

STATIC int usage(char **argv) {
    printf("usage: ./micropython [-X emit=bytecode] file|run! [arg ...]\n");
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

#ifdef _WIN32
#define PATHLIST_SEP_CHAR ';'
#else
#define PATHLIST_SEP_CHAR ':'
#endif

mp_state_ctx_t *new_state(mp_uint_t stack_size, mp_uint_t mem_size) {
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

void setup_main_state(int argc, char **argv) {
    char *path = getenv("MICROPYPATH");
    if (path == NULL) {
        path = "";
    }
    
    mp_uint_t path_num = 1; // [0] is for current dir (or base dir of the script)
    for (char *p = path; p != NULL; p = strchr(p, PATHLIST_SEP_CHAR)) {
        path_num++;
        if (p != NULL) {
            p++;
        }
    }
    mp_obj_list_init(mp_sys_path, path_num);
    mp_obj_t *path_items;
    mp_obj_list_get(mp_sys_path, &path_num, &path_items);
    path_items[0] = MP_OBJ_NEW_QSTR(MP_QSTR_);
    {
    char *p = path;
    for (mp_uint_t i = 1; i < path_num; i++) {
        char *p1 = strchr(p, PATHLIST_SEP_CHAR);
        if (p1 == NULL) {
            p1 = p + strlen(p);
        }
        path_items[i] = MP_OBJ_NEW_QSTR(qstr_from_strn(p, p1 - p));
        p = p1 + 1;
    }
    }

    mp_obj_list_init(mp_sys_argv, 0);
}

int execute_main_state(int argc, char **argv) {
    const int NOTHING_EXECUTED = -2;
    int ret = NOTHING_EXECUTED;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        for (int a = 1; a < argc; a++) {
            if (argv[a][0] == '-') {
                if (strcmp(argv[a], "-X") == 0) {
                    a += 1;
                } else {
                    ret = usage(argv);
                    break;
                }
            } else {
                if (a + 1 <= argc){
                    set_sys_argv(argv, argc, a);
                    
                    mp_obj_t module_fun = new_module_from_file(argv[a]);
                    mp_call_function_0(module_fun);
                } else {
                    ret = usage(argv);
                    break;
                }
            }
        }
        
        nlr_pop();
    } else {
        ret = handle_uncaught_exception((mp_obj_t)nlr.ret_val);
    }
    
    return ret;
}

void free_state(mp_state_ctx_t *state) {
    mp_state_load(state);
    mp_deinit();
    
#if MICROPY_ENABLE_GC && !defined(NDEBUG)
    // We don't really need to free memory since we are about to exit the
    // process, but doing so helps to find memory leaks.
    free(MP_STATE_MEM(gc_alloc_table_start));
#endif

    mp_state_store(state);
}

NORETURN void mp_assert_fail(const char *assertion, const char *file,
                             unsigned int line, const char *function) {
    printf("%s: %s:%u %s: Assertion '%s' failed.\n", progname, file, line, function, assertion);
    abort();
}

int main(int argc, char **argv) {
    progname = argv[0];
    
    mp_state_ctx_t *state = new_state(MEM_SIZE(40, KB), MEM_SIZE(256, KB));
    mp_state_load(state);
    
    pre_process_options(argc, argv);
    
    int ret;    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        setup_main_state(argc, argv);
        ret = execute_main_state(argc, argv);
        
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
