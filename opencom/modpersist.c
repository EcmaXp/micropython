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

/******************************************************************************/
// mp_map cloneed.
// TODO: replace mp_map_lookup with minimal version!

STATIC mp_map_elem_t *modpersist_mp_map_lookup(mp_map_t *map, mp_obj_t index);

STATIC void modpersist_mp_map_rehash(mp_map_t *map) {
    mp_uint_t old_alloc = map->alloc;
    mp_map_elem_t *old_table = map->table;
    map->alloc = (map->alloc * 2 + 1) | 1;
    map->used = 0;
    map->table = m_new0(mp_map_elem_t, map->alloc);
    for (mp_uint_t i = 0; i < old_alloc; i++) {
        if (old_table[i].key != MP_OBJ_NULL && old_table[i].key != MP_OBJ_SENTINEL) {
            modpersist_mp_map_lookup(map, old_table[i].key)->value = old_table[i].value;
        }
    }
    m_del(mp_map_elem_t, old_table, old_alloc);
}

STATIC mp_map_elem_t *modpersist_mp_map_lookup(mp_map_t *map, mp_obj_t index) {
    // mp_map_lookup_kind_t lookup_kind = MP_MAP_LOOKUP_ADD_IF_NOT_FOUND;
    
    if (map->alloc == 0) {
        modpersist_mp_map_rehash(map);
    }

    mp_uint_t hash = (mp_uint_t)index;
    mp_uint_t pos = hash % map->alloc;
    mp_uint_t start_pos = pos;
    mp_map_elem_t *avail_slot = NULL;
    for (;;) {
        mp_map_elem_t *slot = &map->table[pos];
        if (slot->key == MP_OBJ_NULL) {
            // found NULL slot, so index is not in table
            map->used += 1;
            if (avail_slot == NULL) {
                avail_slot = slot;
            }
            slot->key = index;
            slot->value = MP_OBJ_NULL;
            return slot;
        } else if (slot->key == MP_OBJ_SENTINEL) {
            if (avail_slot == NULL) {
                avail_slot = slot;
            }
        } else if (slot->key == index) {
            return slot;
        }

        pos = (pos + 1) % map->alloc;

        if (pos == start_pos) {
            if (avail_slot != NULL) {
                map->used++;
                avail_slot->key = index;
                avail_slot->value = MP_OBJ_NULL;
                return avail_slot;
            } else {
                modpersist_mp_map_rehash(map);
                start_pos = pos = hash % map->alloc;
            }
        }
    }
}

/******************************************************************************/

// TODO: WRITE_INTn and WRITE_UINTn will be platform depend thing!
#define WRITE_STR(data) system_buf_write(sysbuf, (data), strlen((data)))
#define WRITE_STRN(data, size) system_buf_write(sysbuf, (data), (size))

#define WRITE_SIZE(size) system_buf_write_size(sysbuf, (size))

#define WRITE_INT8(data) system_buf_write_int8(sysbuf, (data))
#define WRITE_UINT8(data) system_buf_write_int8(sysbuf, (data))

#define WRITE_INT16(data) system_buf_write_int(sysbuf, (data), (2))
#define WRITE_UINT16(data) system_buf_write_uint(sysbuf, (data), (2))

#define WRITE_INT32(data) system_buf_write_int(sysbuf, (data), (4))
#define WRITE_UINT32(data) system_buf_write_uint(sysbuf, (data), (4))

#define WRITE_INT64(data) system_buf_write_int(sysbuf, (data), (8))
#define WRITE_UINT64(data) system_buf_write_uint(sysbuf, (data), (8))

#define WRITE_PTR(data) WRITE_UINT32(data)

#define DUMP_START() \
    mp_uint_t _ref = sysbuf->len; \
    if (modpersist_starting_dump(persister, obj, &_ref)) {
        /* body */
        
#define DUMP_END() \
    } \
    return _ref;

#define PACK_START() \
    mp_uint_t _ref = 0; \
    if (modpersist_starting_pack(persister, obj, &_ref)) {
        /* body */

#define PACK_END() \
    } \
    return _ref;

// 3-way tagging

#define PACK(type, obj) persister_dump_##type(persister, (obj))
#define SUBPACK(type, obj) _ref = PACK(type, obj);
#define SUBPACKED(ref) _ref = (ref);
#define PACK_ANY(obj) PACK(any, (obj))
#define PACK_PTR(obj, ref) persister_dump_ptr(persister, (obj), (ref))

// TODO: BASEPTR should accept ?
#define PACK_BASEPTR(baseptr, ptr, size) persister_dump_baseptr(persister, (baseptr), (char *)(ptr), size)
#define PACK_ERROR(message) (WRITE_STR("E:"), (WRITE_STR(message), WRITE_INT8(0)))

typedef struct _mp_obj_system_buf_t {
    // system malloc
    mp_obj_base_t base;

    byte *buf;
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
    bool is_overflow = false;
    mp_uint_t new_alloc = system_buf->alloc;
    while (new_alloc < system_buf->len + size) {
        new_alloc = sizeof(byte) * new_alloc * 3 + 1;    
        is_overflow = true;
    }

    if (is_overflow) {
        byte *new_buf = realloc(system_buf->buf, new_alloc);
        assert(new_buf != NULL);
        
        memset(new_buf + system_buf->len, 0, new_alloc - system_buf->len);
        system_buf->buf = new_buf;
        system_buf->alloc = new_alloc;
    }
    
    memcpy(system_buf->buf + system_buf->len, str, size);
    
    mp_uint_t last_len = system_buf->len;
    system_buf->len += size;
    
    return last_len;
}

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

#define PUI_SWAP(a, b) do { \
    char tmp = pui.c[a]; \
    pui.c[a] = pui.c[b]; \
    pui.c[b] = tmp; \
} while(0)

inline STATIC mp_uint_t system_buf_write_int8(mp_obj_system_buf_t *sysbuf, char num) {
    return system_buf_write(sysbuf, &num, 1);
}

STATIC mp_uint_t system_buf_write_int(mp_obj_system_buf_t *sysbuf, long long num, unsigned int size) {
    // TODO: should test this function working well
    persist_union_int_t pui;
    bool is_minus = num < 0;
    if (is_minus) {
        num = -num;
    }
    
    switch (size) {
        case 1:
            pui.int8 = num;
            break;
        case 2:
            pui.int16 = num;
            break;
        case 4:
            pui.int32 = num;
            break;
        case 8:
            pui.int64 = num;
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
    
    if (is_minus) {
        pui.c[0] |= 1 << (sizeof(char) - 1);
    }
    
    return system_buf_write(sysbuf, (char *)&pui.c, size);
}

STATIC mp_uint_t system_buf_write_uint(mp_obj_system_buf_t *sysbuf, unsigned long long num, unsigned int size) {
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
    
    return system_buf_write(sysbuf, (char *)&pui.c, size);
}

STATIC mp_int_t system_buf_write_size(mp_obj_system_buf_t *sysbuf, mp_uint_t size) {
    mp_uint_t start = sysbuf->len;

    if (size <= 0xFF) {
        WRITE_INT8('1');
        WRITE_UINT8(size);
    } else if (size <= 0xFFFF) {
        WRITE_INT8('2');
        WRITE_UINT16(size);
    } else if (size <= 0xFFFFFFFF) {
        WRITE_INT8('4');
        WRITE_UINT32(size);
    } else {
        WRITE_INT8('8');
        WRITE_UINT64(size);
    }
    
    return start;
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

typedef struct _mp_obj_persister_t {
    mp_obj_base_t base;

    #if MP_PERSIST_USE_REGMAP
    byte *regmap;
    mp_uint_t regmap_size;
    #endif

    mp_map_t *objmap;
    mp_obj_system_buf_t *sysbuf;

    mp_obj_t userfunc;
} mp_obj_persister_t;

const mp_obj_type_t mp_type_persister;

STATIC bool modpersist_starting_pack(mp_obj_persister_t *persister, mp_obj_t obj, mp_uint_t *ref) {
    mp_map_elem_t *persist_reg = modpersist_mp_map_lookup(persister->objmap, obj);

    bool require_packing = (persist_reg->value == MP_OBJ_NULL);
    if (!require_packing) {
        *ref = (mp_uint_t)persist_reg->value;
    }
    
    return require_packing;
}

STATIC bool modpersist_starting_dump(mp_obj_persister_t *persister, mp_obj_t obj, mp_uint_t *ref) {
    mp_map_elem_t *persist_reg = modpersist_mp_map_lookup(persister->objmap, obj);

    bool require_packing = (persist_reg->value == MP_OBJ_NULL);
    if (require_packing) {
        persist_reg->value = (mp_obj_t)(persister->sysbuf->len);
    }
    
    *ref = (mp_uint_t)persist_reg->value;
    return require_packing;
}

// not impl yet and using map.
#define MP_PERSIST_USE_REGMAP 0

#if mp_int_t == int32_t
#define MP_PERSIST_MAP_ROW_SHIFT 3
#elif mp_int_t == int64_t
#define MP_PERSIST_MAP_ROW_SHIFT 4
#endif

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

    mp_obj_t user_persist_func = MP_OBJ_NULL;
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    if (1 <= n_args) {
        user_persist_func = args[0];
    }
    
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
    persister->userfunc = user_persist_func;

    return persister;
}

// 2 functions are used for persist anything.
STATIC mp_uint_t persister_dump_any(mp_obj_persister_t *persister, mp_obj_t o);
STATIC void persister_dump_ptr(mp_obj_persister_t *persister, mp_obj_t obj, mp_uint_t ref);

STATIC void persister_dump_mp_int_t(mp_obj_persister_t *persister, mp_int_t num) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    (void)sysbuf;
}

STATIC void persister_dump_mp_uint_t(mp_obj_persister_t *persister, mp_uint_t num) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    (void)sysbuf;
}

STATIC mp_uint_t persister_dump_microthread(mp_obj_persister_t *persister, mp_obj_t thread_obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_TYPE(thread_obj, &mp_type_microthread));

    mp_obj_microthread_t *thread = thread_obj;
    
    // PACK(array, 1);
    // PACK_PTR();
    // PACK_BASEPTR();
    // PACK(int64, 32);

    (void)thread;
    (void)sysbuf;
    
    return 0;
}

STATIC mp_uint_t persister_dump_code_state(mp_obj_persister_t *persister, mp_code_state *code_state) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;
    (void)sysbuf;
    return 0;
}

STATIC mp_uint_t persister_dump_stack(mp_obj_persister_t *persister) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;
    (void)sysbuf;
    return 0;
}

STATIC mp_uint_t persister_dump_vm_state(mp_obj_persister_t *persister) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;
    (void)sysbuf;
    return 0;
}

STATIC mp_uint_t persister_dump_str(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_STR(obj));
    DUMP_START();
    
    mp_uint_t len = 0;
    const char* str = mp_obj_str_get_data(obj, &len);
    
    WRITE_INT8('s');
    WRITE_SIZE(len);
    WRITE_STRN(str, len);
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_bytes(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_TYPE(obj, &mp_type_bytes));
    DUMP_START();
    
    mp_buffer_info_t objbuf;
    mp_get_buffer_raise(obj, &objbuf, MP_BUFFER_READ);
    
    WRITE_INT8('b');
    WRITE_SIZE(objbuf.len);
    WRITE_STRN(objbuf.buf, objbuf.len);
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_qstr(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_QSTR(obj));
    DUMP_START();
    
    mp_uint_t len = 0;
    const byte* str = qstr_data(MP_OBJ_QSTR_VALUE(obj), &len);
    
    WRITE_INT8('q');
    WRITE_SIZE(len);
    WRITE_STRN((const char*)str, len);
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_small_int(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_SMALL_INT(obj));

    mp_int_t value = MP_OBJ_SMALL_INT_VALUE(obj);
    persist_union_int_t pui = {.int32 = value};

    if (pui.int32 != value) {
        DUMP_START();
        
        WRITE_INT8('i');
        WRITE_INT8('8');
        WRITE_INT64(value);
        
        DUMP_END();
    } else {
        return 0;
    }
}

STATIC mp_uint_t persister_dump_int(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_TYPE(obj, &mp_type_int));

    // TODO: how to persist large int?
    mp_int_t value = mp_obj_int_get_truncated(obj);
    
    DUMP_START();
    
    // XXX only works with small int...
    WRITE_INT8('i');
    WRITE_INT8('8');
    WRITE_INT64(value);
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_tuple(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_TYPE(obj, &mp_type_tuple));
    
    mp_obj_t iter = mp_getiter(obj);
    mp_int_t size = mp_obj_get_int(mp_obj_len(obj));
    mp_uint_t *ref = m_new0(mp_uint_t, size);

    for (mp_int_t i = 0; i < size; i++){
        mp_obj_t cur = mp_iternext(iter);
        *(ref + i) = PACK_ANY(cur);
    }
    
    DUMP_START();
    
    WRITE_INT8('t');
    WRITE_SIZE(size);
    
    iter = mp_getiter(obj);
    for (mp_int_t i = 0; i < size; i++){
        mp_obj_t cur = mp_iternext(iter);
        PACK_PTR(cur, *(ref + i));
    }
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_list(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_TYPE(obj, &mp_type_list));

    mp_obj_t iter = mp_getiter(obj);
    mp_int_t size = mp_obj_get_int(mp_obj_len(obj));
    mp_uint_t *ref = m_new0(mp_uint_t, size);

    for (mp_int_t i = 0; i < size; i++){
        mp_obj_t cur = mp_iternext(iter);
        *(ref + i) = PACK_ANY(cur);
    }
    
    DUMP_START();
    
    WRITE_INT8('l');
    WRITE_SIZE(size);
    
    iter = mp_getiter(obj);
    for (mp_int_t i = 0; i < size; i++){
        mp_obj_t cur = mp_iternext(iter);
        PACK_PTR(cur, *(ref + i));
    }
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_dict(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    assert(MP_OBJ_IS_TYPE(obj, &mp_type_list));

    mp_obj_t iter = mp_getiter(mp_call_function_0(mp_load_attr(obj, MP_QSTR_items)));
    mp_int_t size = mp_obj_get_int(mp_obj_len(obj));
    mp_uint_t *ref_key = m_new0(mp_uint_t, size);
    mp_uint_t *ref_value = m_new0(mp_uint_t, size);

    for (mp_int_t i = 0; i < size; i++){
        mp_obj_t cur = mp_iternext(iter);
        mp_uint_t csize = 0;

        mp_obj_t *items = NULL;
        mp_obj_tuple_get(cur, &csize, &items);
        assert(csize == 2);
        
        *(ref_key + i) = PACK_ANY(items[0]);
        *(ref_value + i) = PACK_ANY(items[1]);
    }
    
    DUMP_START();
    
    WRITE_INT8('d');
    WRITE_SIZE(size);
    
    iter = mp_getiter(mp_call_function_0(mp_load_attr(obj, MP_QSTR_items)));
    for (mp_int_t i = 0; i < size; i++){
        mp_obj_t cur = mp_iternext(iter);
        mp_uint_t csize = 0;

        mp_obj_t *items = NULL;
        mp_obj_tuple_get(cur, &csize, &items);
        assert(csize == 2);
        
        PACK_PTR(items[0], *(ref_key + i));
        PACK_PTR(items[1], *(ref_value + i));
    }
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_user(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    DUMP_START();
    
    mp_buffer_info_t retbuf;
    
    // TODO: persister's user function can access PACK_PTR and PACK_ANY for internal usage?
    mp_obj_t result = mp_call_function_1(persister->userfunc, obj);

    mp_get_buffer_raise(result, &retbuf, MP_BUFFER_READ);
    
    WRITE_INT8('U');
    WRITE_SIZE(retbuf.len);
    WRITE_STRN(retbuf.buf, retbuf.len);
    
    DUMP_END();
}

STATIC mp_uint_t persister_dump_common_obj(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    void *start = MP_STATE_MEM(gc_pool_start);
    void *end = MP_STATE_MEM(gc_pool_end);
    
    PACK_START();
    
    if (0) {
    } else if (MP_OBJ_IS_STR(obj)) {
        SUBPACK(str, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_bytes)) {
        SUBPACK(bytes, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_int)) {
        SUBPACK(int, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_tuple)) {
        SUBPACK(tuple, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_list)) {
        SUBPACK(list, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_dict)) {
        SUBPACK(dict, obj);
    // } else if (MP_OBJ_IS_FUN(obj)) {
    //    PACK_ERROR("failed to persi");
    } else {
        mp_uint_t ref = PACK(user, obj);
        if (ref != 0) {
            SUBPACKED(ref);
        } else if (start <= obj && obj < end) {
            PACK_ERROR("failed dynamic persist");
        } else {
            PACK_ERROR("failed builtin persist");
        }
    }
    
    PACK_END();
}

STATIC mp_uint_t persister_dump_obj(mp_obj_persister_t *persister, mp_obj_t obj) {
    if (!(
        obj == MP_OBJ_NULL ||
        obj == MP_OBJ_STOP_ITERATION ||
        obj == MP_OBJ_SENTINEL ||
    #if MICROPY_ALLOW_PAUSE_VM
        obj == MP_OBJ_PAUSE_VM ||
    #endif
        obj == mp_const_none || 
        obj == mp_const_true || 
        obj == mp_const_false ||
    false)) {
        // common object
        return PACK(common_obj, obj);
    }
    
    // this is const object
    return 0;
}

STATIC void persister_dump_baseptr(mp_obj_t baseptr, char *ptr, mp_uint_t size) {
    mp_uint_t offset = (mp_uint_t)baseptr - (mp_uint_t)ptr;
    assert(offset < 0 || size <= offset);

    (void)offset;
}

STATIC mp_uint_t persister_dump_any(mp_obj_persister_t *persister, mp_obj_t o) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    if (MP_OBJ_IS_SMALL_INT(o)) {
        return PACK(small_int, o);
    } else if (MP_OBJ_IS_QSTR(o)) {
        return PACK(qstr, o);
    } else if (MP_OBJ_IS_OBJ(o)) {
        return PACK(obj, o);
    } else {
        return PACK_ERROR("unknown type");
    }
}

STATIC void persister_dump_ptr(mp_obj_persister_t *persister, mp_obj_t obj, mp_uint_t ref) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    if (0) {
    } else if (MP_OBJ_IS_SMALL_INT(obj) && ref == 0) {
        WRITE_INT8('S');
        WRITE_PTR(MP_OBJ_SMALL_INT_VALUE(obj));
    } else {
        if (0) {
        } else if (obj == MP_OBJ_NULL) {
            WRITE_INT8('X');
            WRITE_INT8('N');
        } else if (obj == MP_OBJ_STOP_ITERATION) {
            WRITE_INT8('X');
            WRITE_INT8('I');
        } else if (obj == MP_OBJ_SENTINEL) {
            WRITE_INT8('X');
            WRITE_INT8('S');
        #if MICROPY_ALLOW_PAUSE_VM
        } else if (obj == MP_OBJ_PAUSE_VM) {
            WRITE_INT8('X');
            WRITE_INT8('P');
        #endif
        // C tagging is const pointer.
        } else if (obj == mp_const_none) {
            WRITE_INT8('C');
            WRITE_INT8('N');
        } else if (obj == mp_const_true) {
            WRITE_INT8('C');
            WRITE_INT8('T');
        } else if (obj == mp_const_false) {
            WRITE_INT8('C');
            WRITE_INT8('F');
        } else {
            if (ref == 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_RuntimeError, "unknown refernce detected"));
            } else if (ref <= 0xFFFF) {
                WRITE_INT8('O');
                WRITE_UINT16(ref);
            } else if (ref <= 0XFFFFFFFF) {
                WRITE_INT8('Q');
                WRITE_UINT32(ref);
            } else {
                // XXX WOW?
                nlr_raise(mp_obj_new_exception_msg(&mp_type_RuntimeError, "too large refernce index"));
            }
        }
    }
}

STATIC void persister_dump_main(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    mp_uint_t ref = PACK_ANY(obj);
    WRITE_INT8('M');
    PACK_PTR(obj, ref);
}

const mp_obj_type_t mp_type_persister = {
    { &mp_type_type },
    .name = MP_QSTR_Persister,
    .make_new = persister_make_new,
};

STATIC mp_obj_t mod_persist_test(mp_obj_t persister_obj, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(persister_obj, &mp_type_persister));
    
    mp_obj_persister_t *persister = persister_obj;
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    WRITE_STR(MP_PERSIST_MAGIC "\n" "micropython persist v0.1\n");
    PACK(main, obj);
    
    (void)persister_dump_microthread;
    (void)persister_dump_baseptr;
    (void)persister_dump_code_state;
    (void)persister_dump_stack;
    (void)persister_dump_vm_state;
    (void)persister_dump_mp_int_t;
    (void)persister_dump_mp_uint_t;

    return mp_obj_new_bytes(persister->sysbuf->buf, persister->sysbuf->len);
}

MP_DEFINE_CONST_FUN_OBJ_2(mod_persist_test_obj, mod_persist_test);


STATIC const mp_map_elem_t mp_module_persist_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_upersist) },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_dumps), (mp_obj_t)&mod_persist_dumps_obj },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_loads), (mp_obj_t)&mod_persist_loads_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test2), (mp_obj_t)&mp_identity_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Persister), (mp_obj_t)&mp_type_persister },
    { MP_OBJ_NEW_QSTR(MP_QSTR_SystemBuffer), (mp_obj_t)&mp_type_system_buf },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test), (mp_obj_t)&mod_persist_test_obj },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_persist_globals, mp_module_persist_globals_table);

const mp_obj_module_t mp_module_persist = {
    .base = { &mp_type_module },
    .name = MP_QSTR_upersist,
    .globals = (mp_obj_dict_t*)&mp_module_persist_globals,
};
