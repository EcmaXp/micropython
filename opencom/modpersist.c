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
#include "opencom/copybc.h"
#include "py/objfun.h"
#include "py/objclosure.h"
#include "py/objboundmeth.h"
#include "py/compile.h"
#include "py/scope.h"
#include "py/parse.h"
#include "py/emit.h"
#include "py/emitbc.h"

#if !MICROPY_ENABLE_GC
#error Persist module require gc. (really?)
#endif

#if !MICROPY_OBJ_BC_HAVE_RAW_CODE
#error Persist module MICROPY_OBJ_BC_HAVE_RAW_CODE.
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
#define PACK_BASEPTR(baseptr, ptr, size) persister_dump_baseptr(persister, (baseptr), (void *)(ptr), size)
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

STATIC MP_DEFINE_CONST_DICT(system_buf_locals_dict, system_buf_locals_dict_table);

const mp_obj_type_t mp_type_system_buf = {
    { &mp_type_type },
    .name = MP_QSTR_SystemBuffer,
    .buffer_p = { .get_buffer = system_buf_get_buffer },
    .locals_dict = (mp_obj_t)&system_buf_locals_dict,
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

typedef struct _mp_persist_raw_code_t {
    mp_raw_code_t *raw_code;

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
} mp_persist_raw_code_t;

// TODO: build auto parser for mp_bytecode_print?

const byte *mp_persist_copy_bytecode(const byte *ip) {
    // mp_bytecode_print_str
    
    
    // parse result in mp_bytecode_print

    return ip;
}

STATIC void mp_persist_debug_opcode(void *start_bc, const mp_copybc_opdata_t *opdata) {
    printf("c[" INT_FMT ":" INT_FMT "] => OP %d", opdata->ip - (byte *)start_bc, opdata->next_ip - (byte *)start_bc, (int)opdata->op);
    if (opdata->is_ptr)
        printf(" ptr=%p", opdata->data.u_ptr);
    if (opdata->is_num)
        printf(" num=" INT_FMT, opdata->data.u_num);
    if (opdata->is_unum)
        printf(" unum=" UINT_FMT, opdata->data.u_unum);
    if (opdata->is_qstr)
        printf(" qstr=%s", qstr_str(opdata->data.u_qstr));
    if (opdata->has_extra)
        printf(" extra=" UINT_FMT, opdata->extra);
    printf("\n");
}

STATIC mp_persist_raw_code_t *persister_parse_raw_code(mp_raw_code_t *raw_code) {
    if (raw_code->kind != MP_CODE_BYTECODE) {
        mp_not_implemented("raw_code->kind != MP_CODE_BYTECODE");
    }

    const byte *ip = raw_code->data.u_byte.code;
    mp_uint_t len = raw_code->data.u_byte.len;

    // how to debugging?
    // mp_bytecode_print(fun_bc, fun_bc->n_pos_args + fun_bc->n_kwonly_args, fun_bc->bytecode, fun_bc->bytecode_size);

    mp_persist_raw_code_t *prc = m_new_obj(mp_persist_raw_code_t);
    const byte *ci = ip;
    const byte *mp_showbc_code_start = ip;
    
    prc->raw_code = raw_code;
    prc->code_info = ci;
    prc->code_info_size = mp_decode_uint(&ci);
    ip += prc->code_info_size;
    
    prc->block_name = mp_decode_uint(&ci);
    prc->source_file = mp_decode_uint(&ci);
    
    prc->bytecode = ip;
    prc->bytecode_size = len - prc->code_info_size;
    
    // bytecode prelude: arg names (as qstr objects)
    mp_uint_t n_total_args = raw_code->n_pos_args + raw_code->n_kwonly_args;
    prc->arg_names = m_new0(qstr, n_total_args);
    prc->arg_names_len = n_total_args;
    for (mp_uint_t i = 0; i < n_total_args; i++) {
        prc->arg_names[i] = MP_OBJ_QSTR_VALUE(*(mp_obj_t*)ip);
        ip += sizeof(mp_obj_t);
    }

    // bytecode prelude: state size and exception stack size; 16 bit uints
    prc->n_state = mp_decode_uint(&ip);
    prc->n_exc_stack = mp_decode_uint(&ip);

    // bytecode prelude: initialise closed over variables
    {
        for (const byte* c = ip; *c++ != 255;) {
            prc->local_num_len++;
        }

        prc->local_num = m_new0(uint, prc->local_num_len);
        for (mp_uint_t i = 0; i < prc->local_num_len; i++) {
            prc->local_num[i] = *ip++;
        }
        
        // skip 255 marker
        ip++;
        len -= ip - mp_showbc_code_start;
    }

    // print out line number info
    {
        for (const byte* c = ci; *c;) {
            if ((ci[0] & 0x80) == 0) {
                prc->lineno_info_len += 1;
                c += 1;
            } else {
                prc->lineno_info_len += 2;                
                c += 2;
            }
        }
    
        prc->lineno_info = m_new0(byte, prc->lineno_info_len);
        const byte* c = ci;
        for (mp_uint_t i = 0; i < prc->lineno_info_len; i++) {
            prc->lineno_info[i] = *c++;
        }
    }
    
    prc->bytecode_body = ip;
    prc->bytecode_body_size = len;
    
    mp_copybc_copy(ip, len, &mp_persist_debug_opcode, (void *)ip);
    
    mp_bytecode_print2(ip, len);
    (void)mp_persist_copy_bytecode;
    
    // TODO: parse bytecode and find all rawcode and parse again... HELL?
    //       NO WAY: cycle raw_code? just store in dict...?
    //       OMG! many qst and raw_code, etc thing in bytecode body!
    
    return prc;
}

STATIC void persister_pack_raw_code(mp_obj_persister_t *persister, mp_persist_raw_code_t *prc) {
    mp_obj_system_buf_t *sysbuf = persister->sysbuf;

    WRITE_TYPE('X');
    WRITE_STR0("raw_code");
    WRITE_INT8('0'); // version: 0
    
    
    WRITE_INT8(prc->raw_code->kind); // 3 bit
    WRITE_INT8(prc->raw_code->scope_flags); // 7 bit
    WRITE_INT16(prc->raw_code->n_pos_args); // 11 bit
    WRITE_INT16(prc->raw_code->n_kwonly_args); // 11 bit
    
    PACK_QSTR(prc->block_name);
    PACK_QSTR(prc->source_file);

    WRITE_SIZE(prc->arg_names_len);
    for (mp_uint_t i = 0; i < prc->arg_names_len; i++) {
        PACK_QSTR(prc->arg_names[i]);
    }

    WRITE_INT16(prc->n_state);
    WRITE_INT16(prc->n_exc_stack);

    WRITE_SIZE(prc->local_num_len);
    for (mp_uint_t i = 0; i < prc->local_num_len; i++) {
        // TODO: ptr?
        WRITE_PTR(prc->local_num[i]);
    }

    WRITE_SIZE(prc->lineno_info_len);
    for (mp_uint_t i = 0; i < prc->lineno_info_len; i++) {
        WRITE_INT8(prc->lineno_info[i]);
    }
    
    WRITE_SIZE(prc->bytecode_body_size);
    WRITE_STRN((const char *)prc->bytecode_body, prc->bytecode_body_size);
}

STATIC void persister_pack_code_state(mp_obj_persister_t *persister, mp_obj_t obj) {

    mp_code_state *code_state = obj;
    mp_obj_t fun_obj = code_state->fun;

    assert(MP_OBJ_IS_TYPE(fun_obj, &mp_type_fun_bc));
    mp_obj_fun_bc_t *fun_bc = fun_obj;
    (void)fun_bc;

    if (0) {
        // because persister_parse_raw_code is heavy calc.
        assert(persister_parse_raw_code(fun_bc->raw_code)->code_info == code_state->code_info);
    }
    
    PACKING {
        WRITE_TYPE('X');
        WRITE_STR0("code_state");
        WRITE_INT8('0'); // version: 0
        
        PACK_ANY(code_state->fun);
        PACK_ANY(code_state->old_globals);
        
        #if MICROPY_STACKLESS
        PACK(code_state, code_state->prev);
        #else
        PACK_ANY(NULL);
        #endif

        #if MICROPY_KEEP_LAST_CODE_STATE
        PACK(code_state, code_state->current);
        #else
        PACK_ANY(NULL);
        #endif

        
        
        // GET_ORIGINAL_DUMP();

        // sp
        // ip
        // code_info
        
        
    }
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

STATIC mp_obj_fun_bc_t *persister_find_root_fun_bc(mp_obj_t obj) {
    mp_obj_t last_obj = NULL;
    while (last_obj != obj) {
        // escape from cycle reference...
        last_obj = obj;

        if (!MP_OBJ_IS_OBJ(obj)) {
            // TODO: should raise error?
            break;
        } else if (MP_OBJ_IS_TYPE(obj, &mp_type_fun_bc)) {
            return obj;
        } else if (MP_OBJ_IS_TYPE(obj, &mp_type_closure)) {
            mp_obj_closure_t *o = obj;
            obj = o->fun;
        } else if (MP_OBJ_IS_TYPE(obj, &mp_type_bound_meth)) {
            mp_obj_bound_meth_t *o = obj;
            obj = o->meth;
        } else {
            // TODO: should raise error?
            break;
        }
    }
    
    return NULL;
}

STATIC void persister_pack_fun_bc(mp_obj_persister_t *persister, mp_obj_t obj) {
    (void)persister_pack_raw_code;
    
    // TODO: chaining function also. (method, closure, etc.)
    //       do not use persister_find_root_fun_bc!
    obj = persister_find_root_fun_bc(obj);
    if (obj == NULL) {
        mp_obj_system_buf_t *sysbuf = persister->sysbuf;
        PACK_ERROR("invaild function (not fun_bc)");
        return;
    }
    
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_fun_bc));
    mp_obj_fun_bc_t *fun_bc = obj;
    
    mp_persist_raw_code_t *prc = persister_parse_raw_code(fun_bc->raw_code);

    // function dump
    PACKING {
        WRITE_TYPE('F');
        
        // PACK_PTR(fun_bc->globals, globals_ref);
        PACK_ANY(fun_bc->globals);
        
        WRITE_INT8(fun_bc->n_pos_args);
        WRITE_INT8(fun_bc->n_kwonly_args);
        WRITE_INT8(fun_bc->n_def_args);
        WRITE_INT8(fun_bc->has_def_kw_args || fun_bc->takes_var_args << 2 || fun_bc->takes_kw_args << 3);

        WRITE_SIZETYPE('t', fun_bc->n_def_args);
        for (mp_uint_t i = 0; i < fun_bc->n_def_args; i++) {
            PACK_ANY(fun_bc->extra_args[i]);            
        }
        
        // TODO: parse bytecode and find all mp_raw_code
        /*
            in mp_emit_bc_make_function function
            - [!] write MP_BC_MAKE_FUNCTION op with scope->raw_code
        
            so bytecode contains another raw_codes...
            mean require parse all bytecode and getting raw_code and again..
        
            omg hell...
        */
        
        persister_pack_raw_code(persister, prc);
    }
}

STATIC void persister_pack_microthread(mp_obj_persister_t *persister, mp_obj_t obj) {
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_microthread));

    mp_obj_microthread_t *thread = obj;
    
    // microthread dump
    PACKING {
        WRITE_TYPE('X');
        WRITE_STR0("microthread");
        WRITE_INT8('0'); // version: 0
    
        PACK_ANY(thread->name);
        PACK_ANY(thread->fun);
        PACK_ANY(thread->last_result);
        PACK_ANY(thread->prev_thread);
        PACK(code_state, thread->code_state);
        
        // status dump
        {
            WRITE_TYPE('X');
            WRITE_STR0("microthread_status");
            WRITE_INT8('0'); // version: 0
        }
    
        // context dump
        {
            WRITE_TYPE('X');
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
    assert(MP_OBJ_IS_TYPE(obj, &mp_type_dict));

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

STATIC void system_buf_write_mp_emit_uint(mp_obj_system_buf_t *sysbuf, mp_uint_t val) {
    // emitbc.c: emit_write_code_info_uint function
    
    byte buf[BYTES_FOR_INT];
    byte *p = buf + sizeof(buf);
    
    do {
        *--p = val & 0x7f;
        val >>= 7;
    } while (val != 0);
    
    mp_uint_t len = buf + sizeof(buf) - p;
    byte cbuf[len];
    byte *c = &cbuf[0];
    
    while (p != buf + sizeof(buf) - 1) {
        *c++ = *p++ | 0x80;
    }
    *c = *p;
    
    system_buf_write(sysbuf, (char *)&cbuf, len);
}

STATIC void system_buf_write_mp_emit_qstr(mp_obj_system_buf_t *sysbuf, qstr val) {
    system_buf_write_mp_emit_uint(sysbuf, (mp_uint_t)val);   
}

typedef _emitbc_t emitbc_t;
STATIC mp_obj_t mod_persist_function(mp_uint_t n_args, const mp_obj_t *args) {
    if (n_args != 14) {
        return mp_const_none;    
    }
    
    mp_uint_t arg_idx = 0; 
    mp_obj_t global_dict = args[arg_idx++];
    mp_uint_t n_pos_args = mp_obj_get_int(args[arg_idx++]);
    mp_uint_t n_kwonly_args = mp_obj_get_int(args[arg_idx++]);
    mp_uint_t n_def_args = mp_obj_get_int(args[arg_idx++]);
    mp_uint_t flags = mp_obj_get_int(args[arg_idx++]);

    mp_obj_t *extra_args = NULL;
    {
        mp_obj_t extra_args_obj = args[arg_idx++];
        mp_uint_t len = 0;
        mp_obj_t *items = NULL;
        
        mp_obj_tuple_get(extra_args_obj, &len, &items);
        extra_args = m_new(mp_obj_t, len);
        assert(n_def_args == len);

        for (mp_uint_t i = 0; i < len; i++) {
            extra_args[i] = items[i];
        }
    }

    qstr block_name = mp_obj_str_get_qstr(args[arg_idx++]);
    qstr source_file = mp_obj_str_get_qstr(args[arg_idx++]);
    
    qstr *arg_names = NULL;
    mp_uint_t arg_names_len = 0;
    {
        mp_obj_t arg_names_obj = args[arg_idx++];
        mp_obj_t *items = NULL;
        
        mp_obj_list_get(arg_names_obj, &arg_names_len, &items);
        arg_names = m_new(qstr, arg_names_len);

        for (mp_uint_t i = 0; i < arg_names_len; i++) {
            arg_names[i] = mp_obj_str_get_qstr(items[i]);
        }
    }
    
    mp_uint_t n_state = mp_obj_get_int(args[arg_idx++]);
    mp_uint_t n_exc_stack = mp_obj_get_int(args[arg_idx++]);
    
    mp_uint_t *local_nums = NULL;
    mp_uint_t local_nums_len = 0;
    {
        mp_obj_t local_nums_obj = args[arg_idx++];
        mp_obj_t *items = NULL;
        
        mp_obj_list_get(local_nums_obj, &local_nums_len, &items);
        local_nums = m_new(mp_uint_t, local_nums_len);

        for (mp_uint_t i = 0; i < local_nums_len; i++) {
            local_nums[i] = mp_obj_get_int(items[i]);
        }
    }
    
    mp_buffer_info_t lineno_info_buf;
    {
        mp_obj_t lineno_info_obj = args[arg_idx++];
        mp_get_buffer_raise(lineno_info_obj, &lineno_info_buf, MP_BUFFER_READ);
    }
    
    mp_buffer_info_t body_buf;
    {
        mp_obj_t lineno_info_obj = args[arg_idx++];
        mp_get_buffer_raise(lineno_info_obj, &body_buf, MP_BUFFER_READ);
    }
    
    (void)global_dict;
    (void)flags;
    (void)local_nums;
    (void)lineno_info_buf;
    (void)body_buf;

    // check compile.c's mp_compile function

    emitbc_t *bc_emit = emit_bc_new();
    scope_t *fun_scope = scope_new(SCOPE_MODULE, MP_PARSE_NODE_NULL, source_file, 0);

    // write the name and source file of this function
    fun_scope->simple_name = block_name;

    // bytecode prelude: argument names
    fun_scope->num_pos_args = n_pos_args;
    fun_scope->num_kwonly_args = n_kwonly_args;

    // TODO: vaild id_info and local_num...
    assert(n_pos_args + n_kwonly_args <= arg_names_len);
    for (mp_uint_t i = 0; i < n_pos_args + n_kwonly_args; i++) {
        // qstr qst = MP_QSTR__star_;
        bool is_added = false;
        id_info_t *id_info = scope_find_or_add_id(fun_scope, arg_names[i], &is_added);
        id_info->flags = ID_FLAG_IS_PARAM;
        id_info->local_num = i;
        assert(is_added == true);
    }
    
    // bytecode prelude: local state size and exception stack size
    fun_scope->num_locals = local_nums_len;
    fun_scope->stack_size = n_state - local_nums_len;
    fun_scope->exc_stack_size = n_exc_stack;
    
    // not working yet.
    
    mp_emit_bc_start_pass(bc_emit, MP_PASS_SCOPE, fun_scope);
    mp_emit_bc_start_pass(bc_emit, MP_PASS_SCOPE, fun_scope);
    
    mp_emit_bc_start_pass(bc_emit, MP_PASS_STACK_SIZE, fun_scope);
    mp_emit_bc_start_pass(bc_emit, MP_PASS_STACK_SIZE, fun_scope);

    mp_emit_bc_start_pass(bc_emit, MP_PASS_CODE_SIZE, fun_scope);
    printf("bc emit off: %d %d\n", (int)bc_emit->code_info_offset, (int)bc_emit->bytecode_offset);
    /* do something */
    mp_emit_bc_start_pass(bc_emit, MP_PASS_CODE_SIZE, fun_scope);

    mp_emit_bc_start_pass(bc_emit, MP_PASS_EMIT, fun_scope);
    /* do something */
    mp_emit_bc_start_pass(bc_emit, MP_PASS_EMIT, fun_scope);
    
    mp_obj_system_buf_t *ci_sysbuf = system_buf_new();
    mp_obj_system_buf_t *code_sysbuf = system_buf_new();
    
    system_buf_write_mp_emit_qstr(ci_sysbuf, block_name);
    system_buf_write_mp_emit_qstr(ci_sysbuf, source_file);
    
    system_buf_write_mp_emit_uint(code_sysbuf, 0xbeef);
    system_buf_write(code_sysbuf, (char *)ci_sysbuf->buf, ci_sysbuf->len);
    system_buf_write_mp_emit_uint(code_sysbuf, 0xefbe);
    system_buf_write(code_sysbuf, "hello", strlen("hello"));
    system_buf_write(code_sysbuf, (char *)bc_emit->code_base, bc_emit->code_info_size + bc_emit->bytecode_size);
    
    assert(arg_idx == 14);
    return mp_obj_new_bytes(code_sysbuf->buf, code_sysbuf->len);
}

MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_persist_function_obj, 14, 14, mod_persist_function);

STATIC const mp_map_elem_t mp_module_persist_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_upersist) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_dumps), (mp_obj_t)&mod_persist_dumps_obj },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_loads), (mp_obj_t)&mod_persist_loads_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test2), (mp_obj_t)&mp_identity_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Persister), (mp_obj_t)&mp_type_persister },
    { MP_OBJ_NEW_QSTR(MP_QSTR_function), (mp_obj_t)&mod_persist_function_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_SystemBuffer), (mp_obj_t)&mp_type_system_buf },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test), (mp_obj_t)&mod_persist_test_obj },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_persist_globals, mp_module_persist_globals_table);

const mp_obj_module_t mp_module_persist = {
    .base = { &mp_type_module },
    .name = MP_QSTR_upersist,
    .globals = (mp_obj_dict_t*)&mp_module_persist_globals,
};
