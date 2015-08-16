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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opencom/modmicrothread.h"
#include "py/objfun.h"

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
#define WRITE_STR0(data) system_buf_write(sysbuf, (data), strlen((data)) + 1)
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

#define WRITE_TYPE(data) system_buf_write_int8(sysbuf, (data))
#define WRITE_SIZETYPE(data, size) ( \
    system_buf_write_int8(sysbuf, (data)), \
    system_buf_write_size(sysbuf, (size)), \
    (void)0)

#define WRITE_PTR(data) WRITE_UINT32(data)

#define PACKING \
    mp_obj_system_buf_t *sysbuf = persister->sysbuf; \
    if (modpersist_starting_dump(persister, obj))
        /* body */

// 3-way tagging

#define PACK(type, obj) persister_pack_##type(persister, (obj))
#define PACK_ANY(obj) PACK(any, (obj))
#define PACK_QSTR(qstr_val) PACK(qstr, MP_OBJ_NEW_QSTR(qstr_val))

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

    if (size == 0) {
        WRITE_INT8('0');
    } else if (size <= 0xFF) {
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

    mp_map_t *objmap;
    mp_obj_system_buf_t *sysbuf;

    mp_obj_t userfunc;
} mp_obj_persister_t;

const mp_obj_type_t mp_type_persister;

// 2 functions are used for persist anything.
STATIC void persister_pack_any(mp_obj_persister_t *persister, mp_obj_t o);
STATIC void persister_pack_qstr(mp_obj_persister_t *persister, mp_obj_t obj);

STATIC bool modpersist_starting_dump(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_map_elem_t *persist_reg = modpersist_mp_map_lookup(persister->objmap, obj);

    bool is_require_packing = (persist_reg->value == MP_OBJ_NULL);
    if (is_require_packing) {
        persist_reg->value = (mp_obj_t)(persister->sysbuf->len);
    } else {
        mp_obj_system_buf_t *sysbuf = persister->sysbuf;
        mp_uint_t ref = (mp_uint_t)persist_reg->value;
        if (ref <= 0xFFFF) {
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
    
    return is_require_packing;
}

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
    
    mp_obj_persister_t *persister = m_new_obj_with_finaliser(mp_obj_persister_t);
    persister->base.type = &mp_type_persister;
    persister->sysbuf = system_buf_new();
    persister->objmap = mp_map_new(16);
    persister->userfunc = user_persist_func;

    mp_obj_system_buf_t *sysbuf = persister->sysbuf;
    WRITE_STR(MP_PERSIST_MAGIC);

    return persister;
}

typedef struct _mp_persist_bytecode_t {
    mp_obj_fun_bc_t *fun_bc;

    const byte *code_info;
    mp_uint_t code_info_size;

    const byte *bytecode;
    mp_uint_t bytecode_size;

    const byte *bytecode_body;
    mp_uint_t bytecode_body_size;
    
    qstr block_name;
    qstr source_file;

    qstr *arg_names;
    mp_uint_t arg_names_len;

    uint n_state;
    uint n_exc_stack;

    uint *local_num;
    mp_uint_t local_num_len;

    byte *lineno_info;
    mp_uint_t lineno_info_len;
} mp_persist_bytecode_t;

STATIC mp_persist_bytecode_t *persister_parse_bytecode_from_fun_bc(mp_obj_t obj) {
    mp_obj_fun_bc_t *fun_bc = obj;
    const byte *ip = fun_bc->bytecode;
    mp_uint_t len = fun_bc->bytecode_size;
    
    // mp_bytecode_print(fun_bc, fun_bc->n_pos_args + fun_bc->n_kwonly_args, fun_bc->bytecode, fun_bc->bytecode_size);

    mp_persist_bytecode_t *bc = m_new_obj(mp_persist_bytecode_t);
    const byte *ci = ip;
    const byte *mp_showbc_code_start = ip;
    
    bc->fun_bc = fun_bc;
    bc->code_info = ci;
    bc->code_info_size = mp_decode_uint(&ci);
    ip += bc->code_info_size;
    
    bc->block_name = mp_decode_uint(&ci);
    bc->source_file = mp_decode_uint(&ci);
    
    bc->bytecode = ip;
    bc->bytecode_size = len - bc->code_info_size;
    
    // bytecode prelude: arg names (as qstr objects)
    mp_uint_t n_total_args = fun_bc->n_pos_args + fun_bc->n_kwonly_args;
    bc->arg_names = m_new0(qstr, n_total_args);
    bc->arg_names_len = n_total_args;
    for (mp_uint_t i = 0; i < n_total_args; i++) {
        bc->arg_names[i] = MP_OBJ_QSTR_VALUE(*(mp_obj_t*)ip);
        ip += sizeof(mp_obj_t);
    }

    // bytecode prelude: state size and exception stack size; 16 bit uints
    bc->n_state = mp_decode_uint(&ip);
    bc->n_exc_stack = mp_decode_uint(&ip);

    // bytecode prelude: initialise closed over variables
    {
        for (const byte* c = ip; *c++ != 255;) {
            bc->local_num_len++;
        }

        bc->local_num = m_new0(uint, bc->local_num_len);
        for (mp_uint_t i = 0; i < bc->local_num_len; i++) {
            bc->local_num[i] = *ip++;
        }
        
        // skip 255 marker
        ip++;
        len -= ip - mp_showbc_code_start;
    }

    // print out line number info
    {
        for (const byte* c = ci; *c;) {
            if ((ci[0] & 0x80) == 0) {
                bc->lineno_info_len += 1;
                c += 1;
            } else {
                bc->lineno_info_len += 2;                
                c += 2;
            }
        }
    
        bc->lineno_info = m_new0(byte, bc->lineno_info_len);
        const byte* c = ci;
        for (mp_uint_t i = 0; i < bc->lineno_info_len; i++) {
            bc->lineno_info[i] = *c++;
        }
    }
    
    bc->bytecode_body = ip;
    bc->bytecode_body_size = len;
    
    return bc;
}

STATIC void persister_pack_bytecode(mp_obj_persister_t *persister, mp_persist_bytecode_t *bc) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    // NOTE: mp_persist_bytecode_t is are part of fun_bc.

    WRITE_TYPE('E');
    WRITE_STR0("bytecode");
    WRITE_INT8('0'); // version: 0
    PACK_QSTR(bc->block_name);
    PACK_QSTR(bc->source_file);

    WRITE_SIZE(bc->arg_names_len);
    for (mp_uint_t i = 0; i < bc->arg_names_len; i++) {
        PACK_QSTR(bc->arg_names[i]);
    }

    WRITE_INT16(bc->n_state);
    WRITE_INT16(bc->n_exc_stack);

    WRITE_SIZE(bc->local_num_len);
    for (mp_uint_t i = 0; i < bc->local_num_len; i++) {
        // TODO: ptr?
        WRITE_PTR(bc->local_num[i]);
    }

    WRITE_SIZE(bc->lineno_info_len);
    for (mp_uint_t i = 0; i < bc->lineno_info_len; i++) {
        WRITE_INT8(bc->lineno_info[i]);
    }
    
    WRITE_SIZE(bc->bytecode_body_size);
    WRITE_STRN((const char *)bc->bytecode_body, bc->bytecode_body_size);
}

STATIC void persister_pack_code_state(mp_obj_persister_t *persister, mp_obj_t obj) {

/*
typedef struct _mp_code_state {
    mp_obj_t fun;
    const byte *code_info;
    const byte *ip;
    mp_obj_t *sp;
    // bit 0 is saved currently_in_except_block value
    mp_exc_stack_t *exc_sp;
    mp_obj_dict_t *old_globals;
    #if MICROPY_STACKLESS
    struct _mp_code_state *prev;
    #endif
    #if MICROPY_KEEP_LAST_CODE_STATE
    struct _mp_code_state *current;
    #endif
    mp_uint_t n_state;
    // Variable-length
    mp_obj_t state[0];
    // Variable-length, never accessed by name, only as (void*)(state + n_state)
    //mp_exc_stack_t exc_state[0];
} mp_code_state;
*/
}

STATIC void persister_pack_fun_bc(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_fun_bc));
    
    mp_obj_fun_bc_t *fun_bc = obj;
    mp_persist_bytecode_t *bc = persister_parse_bytecode_from_fun_bc(fun_bc);

    // function dump
    PACKING {
        WRITE_TYPE('F');
        
        // PACK_PTR(fun_bc->globals, globals_ref);
        PACK_ANY(fun_bc->globals);
        
        WRITE_INT8(fun_bc->n_pos_args);
        WRITE_INT8(fun_bc->n_kwonly_args);
        WRITE_INT8(fun_bc->n_def_args);
        WRITE_INT8(fun_bc->has_def_kw_args || fun_bc->takes_var_args << 2 || fun_bc->takes_kw_args << 3);
        WRITE_INT32(0);
        
        persister_pack_bytecode(persister, bc);
    }
}

STATIC void persister_pack_microthread(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_microthread));

    mp_obj_microthread_t *thread = obj;
    
    // microthread dump
    PACKING {
        WRITE_TYPE('E');
        WRITE_STR0("microthread");
        WRITE_INT8('0'); // version: 0
    
        PACK_ANY(thread->name);
        PACK_ANY(thread->fun);
        PACK_ANY(thread->last_result);
        PACK_ANY(thread->prev_thread);
        PACK(code_state, thread->code_state);
        
        // status dump
        {
            WRITE_TYPE('E');
            WRITE_STR0("microthread_status");
            WRITE_INT8('0'); // version: 0
        }
    
        // context dump
        {
            WRITE_TYPE('E');
            WRITE_STR0("microthread_context");
            WRITE_INT8('0'); // version: 0
        }
    }
}

STATIC void persister_pack_stack(mp_obj_persister_t *persister) {

}

STATIC void persister_pack_vm_state(mp_obj_persister_t *persister) {

}

STATIC void persister_pack_str(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_STR(obj));
    PACKING {
        mp_uint_t len = 0;
        const char* str = mp_obj_str_get_data(obj, &len);
        
        WRITE_TYPE('s');
        WRITE_SIZE(len);
        WRITE_STRN(str, len);
    }
}

STATIC void persister_pack_bytes(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_bytes));
    
    PACKING {
        mp_buffer_info_t objbuf;
        mp_get_buffer_raise(obj, &objbuf, MP_BUFFER_READ);
        
        WRITE_SIZETYPE('b', objbuf.len);
        WRITE_STRN(objbuf.buf, objbuf.len);
    }
}

STATIC void persister_pack_qstr(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_QSTR(obj));

    PACKING {
        mp_uint_t len = 0;
        const byte* str = qstr_data(MP_OBJ_QSTR_VALUE(obj), &len);
        
        WRITE_SIZETYPE('q', len);
        WRITE_STRN((const char*)str, len);
    }
}

STATIC void persister_pack_small_int(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_SMALL_INT(obj));

    mp_int_t value = MP_OBJ_SMALL_INT_VALUE(obj);
    persist_union_int_t pui = {.int32 = value};

    if (pui.int32 == value) {
        mp_obj_system_buf_t *sysbuf = persister->sysbuf;
        WRITE_TYPE('S');
        WRITE_PTR(MP_OBJ_SMALL_INT_VALUE(obj));
    } else {
        PACKING {
            WRITE_TYPE('i');
            WRITE_INT8('8');
            WRITE_INT64(value);
        }
    }
}

STATIC void persister_pack_int(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_int));

    PACKING {
        // TODO: how to persist large int?
        mp_int_t value = mp_obj_int_get_truncated(obj);
        
        // XXX only works with small int...
        WRITE_TYPE('i');
        WRITE_INT8('8');
        WRITE_INT64(value);
    }
}

STATIC void persister_pack_tuple(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_tuple));
   
    PACKING {
        mp_obj_t iter = mp_getiter(obj);
        mp_int_t size = mp_obj_get_int(mp_obj_len(obj));
     
        WRITE_SIZETYPE('t', size);
        for (mp_int_t i = 0; i < size; i++){
            mp_obj_t cur = mp_iternext(iter);
            PACK_ANY(cur);
        }
    }
}

STATIC void persister_pack_list(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_list));

    PACKING {
        mp_obj_t iter = mp_getiter(obj);
        mp_int_t size = mp_obj_get_int(mp_obj_len(obj));
        
        WRITE_SIZETYPE('l', size);
        for (mp_int_t i = 0; i < size; i++){
            mp_obj_t cur = mp_iternext(iter);
            PACK_ANY(cur);
        }
    }
}

STATIC void persister_pack_dict(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_list));

    PACKING {
        mp_obj_t iter = mp_getiter(mp_call_function_0(mp_load_attr(obj, MP_QSTR_items)));
        mp_int_t size = mp_obj_get_int(mp_obj_len(obj));
        
        WRITE_SIZETYPE('d', size);
        for (mp_int_t i = 0; i < size; i++){
            mp_obj_t cur = mp_iternext(iter);
            mp_uint_t csize = 0;
    
            mp_obj_t *items = NULL;
            mp_obj_tuple_get(cur, &csize, &items);
            assert(csize == 2);
            
            PACK_ANY(items[0]);
            PACK_ANY(items[1]);
        }
    }
}

STATIC bool persister_pack_user(mp_obj_persister_t *persister, mp_obj_t obj) {
    PACKING {
        mp_buffer_info_t retbuf;
        
        // TODO: persister's user function can access PACK_ANY for internal usage?
        //       (but pre-dump stage are removed so can't use.)
        
        // TODO: handle error!
        mp_obj_t result = mp_call_function_1(persister->userfunc, obj);
    
        mp_get_buffer_raise(result, &retbuf, MP_BUFFER_READ);
        
        WRITE_SIZETYPE('U', retbuf.len);
        WRITE_STRN(retbuf.buf, retbuf.len);

        return (retbuf.len != 0);
    } else {
        return true;
    }

}

STATIC void persister_pack_common_obj(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    void *start = MP_STATE_MEM(gc_pool_start);
    void *end = MP_STATE_MEM(gc_pool_end);
    
    if (0) {
    } else if (MP_OBJ_IS_STR(obj)) {
        PACK(str, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_bytes)) {
        PACK(bytes, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_int)) {
        PACK(int, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_tuple)) {
        PACK(tuple, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_list)) {
        PACK(list, obj);
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_dict)) {
        PACK(dict, obj);
    // } else if (MP_OBJ_IS_FUN(obj)) {
    //    PACK_ERROR("failed to persi");
    } else if (MP_OBJ_IS_TYPE(obj, &mp_type_fun_bc)) {
        PACK(fun_bc, obj);
    } else {
        bool is_success = PACK(user, obj);
        if (!is_success) {
            if (start <= obj && obj < end) {
                PACK_ERROR("failed dynamic persist");
            } else {
                PACK_ERROR("failed builtin persist");
            }
        }
    }
}

STATIC void persister_pack_obj(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    if (0) {
    // C tagging is const pointer.
    } else if (obj == mp_const_none) {
        WRITE_TYPE('C');
        WRITE_INT8('N');
    } else if (obj == mp_const_true) {
        WRITE_TYPE('C');
        WRITE_INT8('T');
    } else if (obj == mp_const_false) {
        WRITE_TYPE('C');
        WRITE_INT8('F');
    } else {
        // common object
        PACK(common_obj, obj);
    }
}

STATIC void persister_pack_baseptr(mp_obj_t baseptr, char *ptr, mp_uint_t size) {
    mp_uint_t offset = (mp_uint_t)baseptr - (mp_uint_t)ptr;
    assert(offset < 0 || size <= offset);

    (void)offset;
}

STATIC void persister_pack_any(mp_obj_persister_t *persister, mp_obj_t obj) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    if (0) {
    // C tagging is const pointer.
    } else if (obj == MP_OBJ_NULL) {
        WRITE_TYPE('C');
        WRITE_INT8('0');
    } else if (obj == MP_OBJ_STOP_ITERATION) {
        WRITE_TYPE('C');
        WRITE_INT8('4');
    } else if (obj == MP_OBJ_SENTINEL) {
        WRITE_TYPE('C');
        WRITE_INT8('8');
    #if MICROPY_ALLOW_PAUSE_VM
    } else if (obj == MP_OBJ_PAUSE_VM) {
        WRITE_TYPE('C');
        WRITE_INT8('P');
    #endif
    } else if (MP_OBJ_IS_SMALL_INT(obj)) {
        PACK(small_int, obj);
    } else if (MP_OBJ_IS_QSTR(obj)) {
        PACK(qstr, obj);
    } else if (MP_OBJ_IS_OBJ(obj)) {
        PACK(obj, obj);
    } else {
        assert(! "unknown type");
        PACK_ERROR("unknown type");
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

    PACK_ANY(obj);
    
    (void)persister_pack_microthread;
    (void)persister_pack_baseptr;
    (void)persister_pack_code_state;
    (void)persister_pack_stack;
    (void)persister_pack_vm_state;

    return mp_obj_new_bytes(persister->sysbuf->buf, persister->sysbuf->len);
}

MP_DEFINE_CONST_FUN_OBJ_2(mod_persist_test_obj, mod_persist_test);

STATIC mp_obj_t mod_persist_dumps(mp_obj_t obj) {
    mp_obj_t persister_obj = persister_make_new((mp_obj_t)&mp_type_persister, 0, 0, NULL);
    mp_obj_persister_t *persister = persister_obj;

    PACK_ANY(obj);

    return mp_obj_new_bytes(persister->sysbuf->buf, persister->sysbuf->len);
}

MP_DEFINE_CONST_FUN_OBJ_1(mod_persist_dumps_obj, mod_persist_dumps);

STATIC const mp_map_elem_t mp_module_persist_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_upersist) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_dumps), (mp_obj_t)&mod_persist_dumps_obj },
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
