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

/* TODO: ?

like the gc. find all that relative from thing.

x86 vs x64:
    - size_t
    - mp_int_t
    - mp_uint_t
    - pointer (but size are limited by 16MB)
    - check eris how persist that (https://github.com/fnuecke/eris)

microthread:
    - ?

code_state:
    - check mp_setup_code_state() on bc.c
    - any thing.

stack:
    - All object is mp_obj_t
    - Exclude GOTO slab? (but it aim to code_state's bytecode thing.)

MP_OBJ_*:
    - just persist as that type
    - problem is MP_OBJ_STOP_ITERATION. (debug=4, ndebug=0)

obj:
    - pointer used. check memory pool from gc.
    - pointer map will check that object are persisted?
    - use gc_start pointer for zero based pointer?
      (ext format requied, even if on x64 == x32)
      ptr size are limited to 48 Bit? maximum size is 16 MB (mpool: 128 KB)
    - builtin object can persist (in obj.h)
    - function object can persist
    - registered object can persist
      (like builtin)
    - instnace object can persist
    - other thing can't persist

qstr:
    - qstr pool required?
    - make qstr map and check that qstr are persisted?

vstr:
    - store as buffer? (with ext)

small_int:
    - store as int.
    - if can't unpersist (by overflow) then just unpersist as int object?
    
function pointer (maybe builtin thing):
    - no way?
    - expensive way requied. (big dict requied...)
    - so getting all function pointer are required.

after pickled.

bitmap: 1 KB can handle 128 KB on 64 Bit System.
*/

#include <stdio.h>
#include <msgpack.h>
#include "opencom/modmicrothread.h"

#if !MICROPY_ENABLE_GC
#error Persist module require gc. (really?)
#endif

#define MP_MSGPACK_EXT_TYPE_MIXIN 's'
#define MP_MSGPACK_EXT_TYPE_PERSIST 'p'
#define MP_MSGPACK_EXT_TYPE_QUITE_POINTER 'q'

void _mod_msgpack_pack(msgpack_packer *pk, mp_obj_t o);
void _mod_msgpack_unpack(msgpack_packer *pk, mp_obj_t o);

#define PERSIST()
#define DO_PERSIST(type, value) _mod_persist_dump_##type((value))
#define UNPERSIST()
#define DO_UNPERSIST()

typedef struct _mp_obj_persister_t {
    mp_obj_base_t base;

    byte *regmap;
    mp_uint_t regmap_size;
    
    // main?
    msgpack_sbuffer *sbuf;
    msgpack_packer *pk;

    // sub?

} mp_obj_persister_t;

const mp_obj_type_t mp_type_persister;

// some type require for dump thing, like printer?

mp_obj_t persister_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    // TODO: make mp_obj_t type. (like Packer, for share packed thing)
    /* TODO: disable gc auto collect when dumping.
             before disable gc, just gc.collect call?
    */
    // TODO: make map that targeting pointers?
    // TODO: registered type can persist by calling some?

    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    
    mp_uint_t memsize = ((mp_uint_t)MP_STATE_MEM(gc_pool_end) - (mp_uint_t)MP_STATE_MEM(gc_pool_start));
    mp_uint_t regmap_size = (memsize + BITS_PER_WORD - 1) / BITS_PER_WORD;
    
    byte *regmap = m_new0(byte, regmap_size);

    #if mp_int_t == int32_t
    #define MAP_ROW_SHIFT 3
    #elif mp_int_t == int64_t
    #define MAP_ROW_SHIFT 4
    #endif
    
    mp_uint_t start = (mp_uint_t)MP_STATE_MEM(gc_pool_start);
    mp_uint_t end = (mp_uint_t)MP_STATE_MEM(gc_pool_end);
    
    mp_uint_t i = start;
    for (; i < end; i += BYTES_PER_WORD) {
        mp_uint_t pos = (i - start) >> (MAP_ROW_SHIFT);
        regmap[pos >> 3] |= 1 << (pos & (BITS_PER_BYTE - 1));
    }
    
    mp_obj_persister_t *persister = m_new_obj_with_finaliser(mp_obj_persister_t);
    persister->base.type = &mp_type_persister;
    persister->regmap = regmap;
    persister->regmap_size = regmap_size;
    
    if (persister == NULL) {
        // memory error.
    }
    
    return persister;
}

void _mod_persist_dump_microthread(mp_obj_persister_t *packer, mp_obj_t thread_obj) {
    assert(MP_OBJ_IS_TYPE(thread_obj, &mp_type_microthread));
    mp_obj_microthread_t *thread = thread_obj;

    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);

    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
    
    // sbuf.data, sbuf.size
    // _mod_msgpack_pack(&pk, object);
    
    msgpack_sbuffer_destroy(&sbuf);
    
    (void)thread;
}

void _mod_persist_dump_dict() {
    
}

void _mod_persist_dump_code_state(mp_code_state *code_state) {
    
}

void _mod_persist_dump_stack() {
    
}

void _mod_persist_dump_vm_state() {
    
}

void _mod_persist_dump_qstr() {
    
}

void _mod_persist_dump_small_int() {
    
}

void _mod_persist_dump_obj(mp_obj_t obj) {
    
}

void _mod_persist_dump_any(mp_obj_t any) {
    if (any == MP_OBJ_NULL) {

    } else if (any == MP_OBJ_STOP_ITERATION) {
        
    } else if (any == MP_OBJ_SENTINEL) {
        
    #if MICROPY_ALLOW_PAUSE_VM
    } else if (any == MP_OBJ_PAUSE_VM) {
        
    #endif
    } else if (MP_OBJ_IS_SMALL_INT(any)) {
        DO_PERSIST(small_int, any);
    } else if (MP_OBJ_IS_QSTR(any)) {
        DO_PERSIST(qstr, any);
    } else if (MP_OBJ_IS_OBJ(any)) {
        DO_PERSIST(obj, any);
    }
}

// void _mod_persist_dump_

STATIC mp_obj_t mod_persist_test(mp_obj_t test) {
    return mp_obj_new_int((mp_int_t)&MP_STATE_CTX(dict_globals));
    // return mp_const_none;
}



MP_DEFINE_CONST_FUN_OBJ_1(mod_persist_test_obj, mod_persist_test);

const mp_obj_type_t mp_type_persister = {
    { &mp_type_type },
    .name = MP_QSTR_Persister,
    .make_new = persister_make_new,
};

STATIC const mp_map_elem_t mp_module_persist_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_upersist) },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_dumps), (mp_obj_t)&mod_persist_dumps_obj },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_loads), (mp_obj_t)&mod_persist_loads_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Persister), (mp_obj_t)&mp_type_persister },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test), (mp_obj_t)&mod_persist_test_obj },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_test2), (mp_obj_t)&mod_persist_test2_obj },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_persist_globals, mp_module_persist_globals_table);

const mp_obj_module_t mp_module_persist = {
    .base = { &mp_type_module },
    .name = MP_QSTR_upersist,
    .globals = (mp_obj_dict_t*)&mp_module_persist_globals,
};
