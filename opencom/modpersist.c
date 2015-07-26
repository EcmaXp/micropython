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
#include <stdlib.h>
#include <string.h>
#include "opencom/modmicrothread.h"

#if !MICROPY_ENABLE_GC
#error Persist module require gc. (really?)
#endif

#define MP_PERSIST_MAGIC "MP\x80\x01"

#if mp_int_t == int32_t
#define MP_PERSIST_MAP_ROW_SHIFT 3
#elif mp_int_t == int64_t
#define MP_PERSIST_MAP_ROW_SHIFT 4
#endif

typedef union _persist_union_int_t {
    char c[8];
    int8_t int8;
    int16_t int16;
    int32_t int32;
    int64_t int64;
    uint8_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint64;
} persist_union_int_t;

inline mp_uint_t _modpersist_uint_first_(mp_uint_t first, mp_uint_t last) {
    return first;
}

// TODO: WRITE_INTn and WRITE_UINTn will be platform depend thing!
#define WRITE_(data) system_buf_write(persister->sysbuf, (data), strlen((data)))
#define WRITEN_(data, size) system_buf_write(persister->sysbuf, (data), (size))

#define WRITE_INT8(data) system_buf_write_int(persister->sysbuf, (data), (1))
#define WRITE_UINT8(data) system_buf_write_int(persister->sysbuf, (data), (1))
#define WRITE_INT16(data) system_buf_write_int(persister->sysbuf, (data), (2))
#define WRITE_UINT16(data) system_buf_write_int(persister->sysbuf, (data), (2))
#define WRITE_INT32(data) system_buf_write_int(persister->sysbuf, (data), (4))
#define WRITE_UINT32(data) system_buf_write_int(persister->sysbuf, (data), (4))
#define WRITE_INT64(data) system_buf_write_int(persister->sysbuf, (data), (8))
#define WRITE_UINT64(data) system_buf_write_int(persister->sysbuf, (data), (8))
#define WRITE_PTR(data) WRITE_UINT32(data)

// 3-way tagging

#define PACK(type, obj) persister_dump_##type(persister, (obj))
#define PACK_ANY(obj) PACK(any, (obj))
#define PACK_PTR(obj, ref) persister_dump_ptr(persister, (obj), (ref))

// TODO: BASEPTR should accept ?
#define PACK_BASEPTR(baseptr, ptr, size) persister_dump_baseptr(persister, (baseptr), (char *)(ptr), size)
#define PACK_ERROR(message) _modpersist_uint_first_(WRITE_("E:"), (WRITE_(message), WRITE_INT8(0)))

typedef struct _mp_obj_system_buf_t {
    // system malloc
    mp_obj_base_t base;

    char *buf;
    mp_uint_t alloc;
    mp_uint_t len;

    // TODO: impl read operation?
    // mp_uint_t pos;
} mp_obj_system_buf_t;

const mp_obj_type_t mp_type_system_buf;

STATIC mp_obj_system_buf_t *system_buf_new() {
    mp_uint_t default_size = 16; 
    
    mp_obj_system_buf_t *pbuf = m_new_obj_with_finaliser(mp_obj_system_buf_t);
    pbuf->base.type = &mp_type_system_buf;
    pbuf->buf = malloc(default_size);
    if (pbuf->buf == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_MemoryError, "failed create PersisterBuffer (malloc fail)"));        
    }
    
    pbuf->alloc = default_size;
    pbuf->len = 0;
    
    return pbuf;
}

STATIC mp_uint_t system_buf_write(mp_obj_system_buf_t *system_buf, const char *str, unsigned int size) {
    mp_uint_t new_alloc = system_buf->alloc;
    while (new_alloc < system_buf->len + size) {
        new_alloc = sizeof(char) * new_alloc * 3 + 1;    
    }

    char *new_buf = realloc(system_buf->buf, new_alloc);
    assert(new_buf != NULL);
    
    memset(new_buf + system_buf->len, 0, new_alloc - system_buf->len);
    system_buf->buf = new_buf;
    system_buf->alloc = new_alloc;

    memcpy(system_buf->buf + system_buf->len, str, size);
    
    mp_uint_t last_len = system_buf->len;
    system_buf->len += size;
    
    return last_len;
}

#define PUI_SWAP(a, b) do { \
    char tmp = pui.c[a]; \
    pui.c[a] = pui.c[b]; \
    pui.c[b] = tmp; \
} while(0)

STATIC mp_uint_t system_buf_write_int(mp_obj_system_buf_t *system_buf, unsigned long long num, unsigned int size) {
    persist_union_int_t pui;
    
    switch (size) {
        case 1:
            pui.uint8 = num;
            break;
        case 2:
            pui.uint16 = num;
            break;
        case 4:
            pui.uint32 = num;
            break;
        case 8:
            pui.uint64 = num;
            break;
        default:
            assert(! "invaild int size");
            abort();
    }
    
    #if MP_ENDIANNESS_LITTLE
    switch (size) {
        case 1:
            break;
        case 2:
            PUI_SWAP(0, 1);       
            break;
        case 4:
            PUI_SWAP(0, 3); 
            PUI_SWAP(1, 2);
            break;
        case 8:
            PUI_SWAP(0, 7);
            PUI_SWAP(1, 6);
            PUI_SWAP(2, 5);
            PUI_SWAP(3, 4);
            break;
    }
    #endif
    
    return system_buf_write(system_buf, (char *)&pui.c, size);
}

STATIC mp_int_t system_buf_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    if (flags == MP_BUFFER_READ) {
        mp_obj_system_buf_t *system_buf = self_in;
        bufinfo->buf = (void*)system_buf->buf;
        bufinfo->len = system_buf->len;
        bufinfo->typecode = 'b';
        return 0;
    } else {
        // can't write to a string
        bufinfo->buf = NULL;
        bufinfo->len = 0;
        bufinfo->typecode = -1;
        return 1;
    }
}

STATIC mp_obj_t system_buf_del(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_obj_system_buf_t *pbuf = self_in;

    free(pbuf->buf);

    pbuf->alloc = 0;
    pbuf->buf = NULL;
    pbuf->len = 0;
    
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(system_buf_del_obj, system_buf_del);

STATIC const mp_map_elem_t system_buf_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___del__), (mp_obj_t)&system_buf_del_obj },
};

const mp_obj_type_t mp_type_system_buf = {
    { &mp_type_type },
    .name = MP_QSTR_SystemBuffer,
    .buffer_p = { .get_buffer = system_buf_get_buffer },
    .locals_dict = (mp_obj_t)&system_buf_locals_dict_table,
};

// not impl yet and using map.
#define MP_PERSIST_USE_REGMAP 0

typedef struct _mp_obj_persister_t {
    mp_obj_base_t base;

    #if MP_PERSIST_USE_REGMAP
    byte *regmap;
    mp_uint_t regmap_size;
    #endif

    mp_map_t *objmap;    
    mp_obj_system_buf_t *sysbuf;
} mp_obj_persister_t;

const mp_obj_type_t mp_type_persister;

// some type require for dump thing, like printer?

#if MP_PERSIST_USE_REGMAP
#define MP_RM_INIT(x) \
    (void)0;
#define MP_RM_CHECK(x) ((start <= (x)) && ((x) < end))
#define MP_RM_OFFSET(x) (((mp_uint_t)x - (mp_uint_t)start) >> (MP_PERSIST_MAP_ROW_SHIFT))
#define MP_RM_SHIFT 3 // = log2(BITS_PER_BYTE)
#define MP_RM_MASK 7 // = BITS_PER_BYTE - 1
#define MP_RM_SET(regmap, i) ((regmap)[MP_RM_OFFSET((i)) >> MP_RM_SHIFT] |= 1 << (MP_RM_OFFSET((i)) & MP_RM_MASK))
#define MP_RM_GET(regmap, i) ((regmap)[MP_RM_OFFSET((i)) >> MP_RM_SHIFT] & (1 << (MP_RM_OFFSET((i)) & MP_RM_MASK)))
#endif

STATIC mp_obj_t persister_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    // TODO: make mp_obj_t type. (like Packer, for share packed thing)
    /* TODO: disable gc auto collect when dumping.
             before disable gc, just gc.collect call?
    */
    // TODO: make map that targeting pointers?
    // TODO: registered type can persist by calling some?

    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    
    #if MP_PERSIST_USE_REGMAP
    mp_uint_t memsize = ((mp_uint_t)MP_STATE_MEM(gc_pool_end) - (mp_uint_t)MP_STATE_MEM(gc_pool_start));
    mp_uint_t regmap_size = (memsize + BITS_PER_WORD - 1) / BITS_PER_WORD;
    
    byte *regmap = m_new0(byte, regmap_size);
    
    mp_uint_t start = (mp_uint_t)MP_STATE_MEM(gc_pool_start);
    mp_uint_t end = (mp_uint_t)MP_STATE_MEM(gc_pool_end);
    
    mp_uint_t i = start;
    for (; i < end; i += BYTES_PER_WORD) {
        mp_uint_t pos = (i - start) >> (MP_PERSIST_MAP_ROW_SHIFT);
        regmap[pos >> 3] |= 1 << (pos & (BITS_PER_BYTE - 1));
    }
    #endif
    
    mp_obj_persister_t *persister = m_new_obj_with_finaliser(mp_obj_persister_t);
    persister->base.type = &mp_type_persister;
    #if MP_PERSIST_USE_REGMAP
    persister->regmap = regmap;
    persister->regmap_size = regmap_size;
    #endif
    persister->sysbuf = system_buf_new();
    persister->objmap = mp_map_new(16);

    return persister;
}

STATIC void persister_dump_microthread(mp_obj_persister_t *persister, mp_obj_t thread_obj) {
    assert(MP_OBJ_IS_TYPE(thread_obj, &mp_type_microthread));

    mp_obj_microthread_t *thread = thread_obj;
    
    // PACK(array, 1);
    // PACK_PTR();
    // PACK_BASEPTR();
    // PACK(int64, 32);

    (void)thread;
}

STATIC void persister_dump_mp_int_t(mp_obj_persister_t *persister, mp_int_t num) {
}

STATIC void persister_dump_mp_uint_t(mp_obj_persister_t *persister, mp_uint_t num) {
}

STATIC mp_uint_t persister_dump_code_state(mp_obj_persister_t *persister, mp_code_state *code_state) {
    return 0;
}

STATIC mp_uint_t persister_dump_stack(mp_obj_persister_t *persister) {
    return 0;
}

STATIC mp_uint_t persister_dump_vm_state(mp_obj_persister_t *persister) {
    return 0;
}

STATIC mp_uint_t persister_dump_str(mp_obj_persister_t *persister, mp_obj_t obj) {
    return 0;
}

STATIC mp_uint_t persister_dump_qstr(mp_obj_persister_t *persister, mp_obj_t qstr) {
    // should check already registerd!
    return 0;
}

STATIC mp_uint_t persister_dump_small_int(mp_obj_persister_t *persister, mp_obj_t small_int) {
    // should check already registerd!
    return 0;
}

STATIC mp_uint_t persister_dump_obj(mp_obj_persister_t *persister, mp_obj_t o) {
    // should check already registerd!
    
    void *start = MP_STATE_MEM(gc_pool_start);
    void *end = MP_STATE_MEM(gc_pool_end);

    if (!(start <= o && o < end)) {
        return 0;
    }

    mp_map_elem_t *elem = mp_map_lookup(
        persister->objmap,
        o,
        MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
    
    if (elem->value != MP_OBJ_NULL) {
        
    }
    if (MP_OBJ_IS_STR(o)) {
        return PACK(str, o);
    }
    
    PACK_ERROR("unknown");
    return 0;
}

STATIC void persister_dump_baseptr(mp_obj_t baseptr, char *ptr, mp_uint_t size) {
    mp_uint_t offset = (mp_uint_t)baseptr - (mp_uint_t)ptr;
    assert(offset < 0 || size <= offset);

    (void)offset;
}

STATIC mp_uint_t persister_dump_any(mp_obj_persister_t *persister, mp_obj_t o) {
    if (o == MP_OBJ_NULL) {
        return 0;
    } else if (o == MP_OBJ_STOP_ITERATION) {
        return 0;
    } else if (o == MP_OBJ_SENTINEL) {
        return 0;
    #if MICROPY_ALLOW_PAUSE_VM
    } else if (o == MP_OBJ_PAUSE_VM) {
        return 0;
    #endif
    } else if (MP_OBJ_IS_SMALL_INT(o)) {
        return PACK(small_int, o);
    } else if (MP_OBJ_IS_QSTR(o)) {
        return PACK(qstr, o);
    } else if (MP_OBJ_IS_OBJ(o)) {
        return PACK(obj, o);
    } else {
        return PACK_ERROR("unknown type");
    }
    
    // do not need persist pointer
    return 0;
}

STATIC void persister_dump_ptr(mp_obj_persister_t *persister, mp_obj_t obj, mp_uint_t ref) {
    if (obj == MP_OBJ_NULL) {
        WRITE_INT8('X');
        WRITE_PTR(0);
    } else if (obj == MP_OBJ_STOP_ITERATION) {
        WRITE_INT8('X');
        WRITE_PTR(4);
    } else if (obj == MP_OBJ_SENTINEL) {
        WRITE_INT8('X');
        WRITE_PTR(8);
    #if MICROPY_ALLOW_PAUSE_VM
    } else if (obj == MP_OBJ_PAUSE_VM) {
        WRITE_INT8('X');
        WRITE_PTR(16);
    #endif
    } else if (MP_OBJ_IS_SMALL_INT(obj)) {
        WRITE_INT8('S');
        WRITE_PTR(ref);
    } else if (MP_OBJ_IS_QSTR(obj)) {
        WRITE_INT8('Q');
        WRITE_PTR(ref);
    } else if (MP_OBJ_IS_OBJ(obj)) {
        WRITE_INT8('O');
        WRITE_PTR(ref);
    } else {
        WRITE_INT8('E');
        WRITE_PTR(ref);
    }
}

const mp_obj_type_t mp_type_persister = {
    { &mp_type_type },
    .name = MP_QSTR_Persister,
    .make_new = persister_make_new,
};

STATIC mp_obj_t mod_persist_test(mp_obj_t persister_obj, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(persister_obj, &mp_type_persister));
    
    mp_obj_persister_t *persister = persister_obj;
    WRITE_(MP_PERSIST_MAGIC "\n" "micropython persist v0.1\n");
    PACK_PTR(obj, PACK_ANY(obj));
    
    (void)persister_dump_microthread;
    (void)persister_dump_obj;
    (void)persister_dump_baseptr;
    (void)persister_dump_any;
    (void)persister_dump_code_state;
    (void)persister_dump_stack;
    (void)persister_dump_vm_state;
    (void)persister_dump_qstr;  
    (void)persister_dump_small_int;
    (void)persister_dump_mp_int_t;
    (void)persister_dump_mp_uint_t;

    mp_obj_t items[] = {
        mp_obj_new_bytes((const byte *)persister->sysbuf->buf, persister->sysbuf->len),
    };
    
    return mp_obj_new_tuple(2, items);
}

MP_DEFINE_CONST_FUN_OBJ_2(mod_persist_test_obj, mod_persist_test);


STATIC const mp_map_elem_t mp_module_persist_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_upersist) },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_dumps), (mp_obj_t)&mod_persist_dumps_obj },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_loads), (mp_obj_t)&mod_persist_loads_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Persister), (mp_obj_t)&mp_type_persister },
    { MP_OBJ_NEW_QSTR(MP_QSTR_SystemBuffer), (mp_obj_t)&mp_type_system_buf },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test), (mp_obj_t)&mod_persist_test_obj },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_test2), (mp_obj_t)&mod_persist_test2_obj },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_persist_globals, mp_module_persist_globals_table);

const mp_obj_module_t mp_module_persist = {
    .base = { &mp_type_module },
    .name = MP_QSTR_upersist,
    .globals = (mp_obj_dict_t*)&mp_module_persist_globals,
};
