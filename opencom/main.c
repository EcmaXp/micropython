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
#include "microthread.h"
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

STATIC mp_microthread_t *new_microthread_from_file(const char *threadname, const char *filename) {
    mp_lexer_t *lex = mp_lexer_new_from_file(filename);
    if (lex == NULL) {
        printf("MemoryError: lexer could not allocate memory\n");
        return NULL;
    }
    
    mp_obj_t module_fun = new_module_from_lexer(lex);
    if (module_fun == NULL) {
        printf("MemoryError: new_module_from_lexer could not allocate memory\n");
        return NULL;
    }
    
    mp_microthread_t *microthread = mp_new_microthread(threadname, module_fun);
    
    #if MICROPY_PY___FILE__
    MP_ENTER_MICROTHREAD(microthread);
    mp_store_global(MP_QSTR___file__, MP_OBJ_NEW_QSTR(qstr_from_str(filename)));
    // TODO: find what cause error when throw MP_OBJ_NEW_QSTR(lex->source_name)?
    // ERROR: MP_OBJ_NEW_QSTR(lex->source_name) == MP_OBJ_NEW_QSTR(qstr_from_str(filename))?
    MP_EXIT_MICROTHREAD(microthread);
    #endif

    return microthread;
}

STATIC int do_file(const char *filename) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_file(filename);
        if (lex == NULL) {
            printf("MemoryError: lexer could not allocate memory\n");
            return 1;
        }
        
        mp_obj_t module_fun = new_module_from_lexer(lex);
        if (module_fun == NULL) {
            printf("MemoryError: new_module_from_lexer could not allocate memory\n");
            return 1;
        }
        
        #if MICROPY_PY___FILE__
        mp_store_global(MP_QSTR___file__, MP_OBJ_NEW_QSTR(lex->source_name));
        #endif

        // execute it
        mp_call_function_0(module_fun);
        
        nlr_pop();
        return 0;
    } else {
        // uncaught exception
        return handle_uncaught_exception((mp_obj_t)nlr.ret_val);
    }
}

STATIC int run_bios(void) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // execute it

        mp_microthread_t *thread_biosA = new_microthread_from_file("bios", "../../mpoc-rom/bios.py");
        if (thread_biosA == NULL){
            nlr_pop();
            return 1;
        }
        
        mp_microthread_t *thread_biosB = new_microthread_from_file("biosB", "../../mpoc-rom/bios.py");
        if (thread_biosB == NULL){
            nlr_pop();
            return 1;
        }

        mp_microthread_t *current_thread = thread_biosA;

        while (true){ // it will be mainloop.
            mp_code_state *code_state = current_thread->code_state;
            mp_vm_return_kind_t kind = mp_resume_microthread(current_thread);
            
            // it will replace by custom manager.
            if (current_thread == thread_biosA){
                current_thread = thread_biosB;
            } else {
                current_thread = thread_biosA;
            }
             
            if (kind == MP_VM_RETURN_PAUSE){
                mp_cpu_clear_usage();
                // TODO: change object to throw event?
                *(code_state->sp) = mp_const_true;
            } else if (kind == MP_VM_RETURN_FORCE_PAUSE) {
                mp_cpu_clear_usage();
            } else if (kind == MP_VM_RETURN_EXCEPTION) {
                nlr_raise(code_state->state[code_state->n_state - 1]);
                return 1;
            } else {
                assert(code_state == code_state);
                code_state->current = NULL;
                // maybe yield or return?
                break;
            }
        }

        nlr_pop();
        return 0;
    } else {
        // uncaught exception
        return handle_uncaught_exception((mp_obj_t)nlr.ret_val);
    }
}


STATIC void set_sys_argv(char *argv[], int argc, int start_arg) {
    for (int i = start_arg; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, MP_OBJ_NEW_QSTR(qstr_from_str(argv[i])));
    }
}

STATIC int usage(char **argv) {
    printf("usage: ./micropython [-X emit=bytecode] file|bios!! [arg ...]\n");
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
    mp_stack_set_limit(40 * 1024 * (BYTES_PER_WORD / 4));

#if MICROPY_ENABLE_GC
    // Heap size of GC heap (if enabled)
    // Make it larger on a 64 bit machine, because pointers are larger.
    long heap_size = 128 * 1024 * (BYTES_PER_WORD / 4);
    char *heap = malloc(heap_size);
    gc_init(heap, heap + heap_size);
#endif
    
    mp_init();
    mp_obj_list_init(mp_sys_argv, 0);
    mp_cpu_set_limit(0xFFFFF);
    mp_cpu_set_soft_limit(mp_cpu_get_limit() >> 2);
    mp_init_microthread();

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
                if (strcmp(argv[a], "bios!!")){
                    ret = run_bios();                
                } else {
                    ret = do_file(argv[a]);
                }
            } else {
                exit(usage(argv));
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
