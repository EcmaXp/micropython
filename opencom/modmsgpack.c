/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 sigsrv
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

// Module Name: umsgpack
// This module will control msgpack.

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <msgpack.h>

#include "py/nlr.h"
#include "py/obj.h"
#include "py/objint.h"
#include "py/objstr.h"
#include "py/objlist.h"
#include "py/objtuple.h"
#include "py/stackctrl.h"

#define MP_MSGPACK_EXT_TYPE_REPR 'r'

/*
TODO: ?

*/

/*
typedef struct _mp_obj_socket_t {
    mp_obj_base_t base;
    int fd;
} mp_obj_socket_t;
*/

// STATIC const mp_int_t MSGPACK_DEFAULT_RECURSE_LIMIT = 511;

STATIC const mp_obj_type_t usocket_type;


/******************************************************************************/
#define PACK_START if (0) {}
#define PACK(type, ...) msgpack_pack_##type(pk, ...)
#define PACK_CONST(const_value, pack_function) \
    else if (o == (const_value)) { \
        (pack_function)(pk); \
    }
#define PACK_IF(x) else if (x)
#define PACK_IF_TYPE(t) \
    else if (MP_OBJ_IS_TYPE(o, &t))
#define PACK_END else { \
    assert(0); \
}

#if mp_int_t == int32_t
#define PACK_INT(x) msgpack_pack_int32(pk, x)
#elif mp_int_t == int64_t
#define PACK_INT(x) msgpack_pack_int64(pk, x)
#endif
#define PACK_LIST_BY(x) _mod_msgpack_listpack(pk, o, &x)
#define PACK_OBJECT(x) _mod_msgpack_pack(pk, (x))
/******************************************************************************/

typedef void (*list_get_f)(mp_obj_t o, mp_uint_t *len, mp_obj_t **items);
STATIC void _mod_msgpack_pack(msgpack_packer *pk, mp_obj_t o);

STATIC void _mod_msgpack_listpack(msgpack_packer *pk, mp_obj_t o, list_get_f list_get) {
    mp_uint_t len = 0;
    mp_obj_t *items = NULL;

    list_get(o, &len, &items);

    msgpack_pack_array(pk, len);
    for (mp_uint_t i = 0; i < len; i++) {
        PACK_OBJECT(items[i]);
    }
}


// https://github.com/msgpack/msgpack-python/blob/master/msgpack/_packer.pyx
STATIC void _mod_msgpack_pack(msgpack_packer *pk, mp_obj_t o) {
    MP_STACK_CHECK();
    
    PACK_START
    PACK_CONST(mp_const_none, msgpack_pack_nil)
    PACK_CONST(mp_const_true, msgpack_pack_true)
    PACK_CONST(mp_const_false, msgpack_pack_false)
    PACK_IF(MP_OBJ_IS_SMALL_INT(o)) {
        PACK_INT(MP_OBJ_SMALL_INT_VALUE(o));
    }
    PACK_IF(MP_OBJ_IS_INT(o)) {
        #if MICROPY_LONGINT_IMPL == MICROPY_LONGINT_IMPL_LONGLONG
            assert(sizeof(mp_longint_impl_t) == sizeof(int64_t));
            mp_obj_int_t *int_obj = MP_OBJ_CAST(o);
            msgpack_pack_int64(pk, int_obj->val);
        #else
            PACK_INT(mp_obj_int_get_truncated(o));
        #endif
    }
#if MICROPY_PY_BUILTINS_FLOAT
    PACK_IF_TYPE(mp_type_float) {
        #if MICROPY_FLOAT_IMPL == MICROPY_FLOAT_IMPL_DOUBLE
            msgpack_pack_double(pk, mp_obj_float_get(o));
        #else
            msgpack_pack_float(pk, mp_obj_float_get(o));
        #endif
    }
#endif
    PACK_IF(MP_OBJ_IS_STR_OR_BYTES(o)) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(o, &bufinfo, MP_BUFFER_READ);
        
        PACK_START
        PACK_IF(MP_OBJ_IS_STR(o)) {
            msgpack_pack_str(pk, bufinfo.len);
            msgpack_pack_str_body(pk, bufinfo.buf, bufinfo.len);
        }
        PACK_IF_TYPE(mp_type_bytes) {
            msgpack_pack_bin(pk, bufinfo.len);
            msgpack_pack_bin_body(pk, bufinfo.buf, bufinfo.len);            
        }
        PACK_END
    }
    PACK_IF_TYPE(mp_type_list) {
        PACK_LIST_BY(mp_obj_list_get);
    }
    PACK_IF_TYPE(mp_type_tuple) {
        PACK_LIST_BY(mp_obj_tuple_get);
    }
    PACK_IF_TYPE(mp_type_dict) {
        mp_obj_dict_t *dict = MP_OBJ_CAST(o);
        mp_uint_t max = dict->map.alloc;
        mp_map_t *map = &dict->map;
    
        msgpack_pack_map(pk, dict->map.used);
        for (mp_uint_t i = 0; i < max; i++) {
            if (MP_MAP_SLOT_IS_FILLED(map, i)) {
                mp_map_elem_t item = map->table[i];
                PACK_OBJECT(item.key);
                PACK_OBJECT(item.value);
            }
        }
    }
    PACK_IF(MP_OBJ_IS_OBJ(o)) {
        vstr_t vstr;
        mp_print_t print;
        vstr_init_print(&vstr, 16, &print);

        if (!vstr.had_error) {
            nlr_buf_t nlr;
            if (nlr_push(&nlr) == 0) {
                mp_obj_print_helper(&print, o, PRINT_REPR);
                nlr_pop();
            } else {
                vstr_clear(&vstr);
                nlr_raise(&nlr.ret_val);
            } 
        }
        
        if (vstr.had_error) {
            // TODO: handling error  
        }

        size_t len = vstr_len(&vstr);
        msgpack_pack_ext(pk, len, MP_MSGPACK_EXT_TYPE_REPR);
        msgpack_pack_ext_body(pk, vstr_str(&vstr), len);
        vstr_clear(&vstr);
    }
    PACK_END
}


/******************************************************************************/
#define UNPACK_START if (0) {}
#define UNPACK(t) \
    else if (mo.type == (t))
#define UNPACK_END else { \
    assert(0); \
}

#define UNPACK_OBJECT(x) _mod_msgpack_unpack((x))
/******************************************************************************/

// https://github.com/msgpack/msgpack-python/blob/master/msgpack/_unpacker.pyx
STATIC mp_obj_t _mod_msgpack_unpack(msgpack_object mo) {
    MP_STACK_CHECK();

    UNPACK_START
    UNPACK(MSGPACK_OBJECT_NIL) {
        return mp_const_none;
    }
    UNPACK(MSGPACK_OBJECT_BOOLEAN) {
        return mo.via.boolean? mp_const_true : mp_const_false;
    }
    UNPACK(MSGPACK_OBJECT_POSITIVE_INTEGER) {
        return mp_obj_new_int_from_ull(mo.via.u64);
    }
    UNPACK(MSGPACK_OBJECT_NEGATIVE_INTEGER) {
        return mp_obj_new_int_from_ll(mo.via.i64);
    }
    UNPACK(MSGPACK_OBJECT_FLOAT) {
        return mp_obj_new_float(mo.via.f64);
    }
    UNPACK(MSGPACK_OBJECT_STR) {
        return mp_obj_new_str(mo.via.str.ptr, (mp_uint_t)mo.via.str.size, false);
    }
    UNPACK(MSGPACK_OBJECT_ARRAY) {
        mp_uint_t size = mo.via.array.size;
        mp_obj_t *items = m_new(mp_obj_t, size);
        mp_obj_t *curitem = items;
        msgpack_object *cur = mo.via.array.ptr;
        
        for (mp_uint_t i = 0; i < size; i++) {
            *curitem++ = UNPACK_OBJECT(*cur++);
        }

        return mp_obj_new_list(size, items);
    }
    UNPACK(MSGPACK_OBJECT_MAP) {
        mp_uint_t size = mo.via.map.size;
        mp_obj_t dict = mp_obj_new_dict(size);
        msgpack_object_kv *cur = mo.via.map.ptr;
        
        for (mp_uint_t i = 0; i < size; i++, cur++) {
            mp_obj_dict_store(dict, UNPACK_OBJECT(cur->key), UNPACK_OBJECT(cur->val));
        }
        
        return dict;
    }
    UNPACK(MSGPACK_OBJECT_BIN) {
        return mp_obj_new_bytes((const byte *)mo.via.bin.ptr, (mp_uint_t)mo.via.bin.size);
    }
    UNPACK(MSGPACK_OBJECT_EXT) {
        // TODO: replace tuple as ext
        mp_obj_t *items = m_new(mp_obj_t, 2);
        items[0] = MP_OBJ_NEW_SMALL_INT(mo.via.ext.type);
        items[1] = mp_obj_new_str(mo.via.ext.ptr, (mp_uint_t)mo.via.ext.size, false);
        
        return mp_obj_new_tuple(2, items);
    }
    UNPACK_END
    
    // TODO: raise error.
    return mp_const_none;
}

STATIC mp_obj_t mod_msgpack_pack(mp_obj_t object){
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);

    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
    
    _mod_msgpack_pack(&pk, object);
    
    mp_obj_t buffer_obj = mp_obj_new_bytes((const byte *)sbuf.data, sbuf.size);
    
    msgpack_sbuffer_destroy(&sbuf);
    
    return buffer_obj;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_msgpack_pack_obj, mod_msgpack_pack);

STATIC mp_obj_t mod_msgpack_unpack(mp_obj_t buffer_obj){
    if (!MP_OBJ_IS_TYPE(buffer_obj, &mp_type_bytes)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "it is not bytes"));
    }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buffer_obj, &bufinfo, MP_BUFFER_READ);
    
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_sbuffer_write(&sbuf, bufinfo.buf, bufinfo.len);
    
    msgpack_zone mempool;
    msgpack_zone_init(&mempool, 2048);

    msgpack_object deserialized;
    msgpack_unpack(sbuf.data, sbuf.size, NULL, &mempool, &deserialized);

    mp_obj_t object = _mod_msgpack_unpack(deserialized);

    msgpack_zone_destroy(&mempool);
    msgpack_sbuffer_destroy(&sbuf);
    
    return object;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_msgpack_unpack_obj, mod_msgpack_unpack);

STATIC mp_obj_t mod_msgpack_test2(mp_obj_t asdf) {
    /* msgpack::sbuffer is a simple buffer implementation. */
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);

    /* serialize values into the buffer using msgpack_sbuffer_write callback function. */
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_array(&pk, 64);
    msgpack_pack_int(&pk, 1);
    msgpack_pack_true(&pk);
    msgpack_pack_str(&pk, 7);
    msgpack_pack_str_body(&pk, "example", 7);
    

    /* deserialize the buffer into msgpack_object instance. */
    /* deserialized object is valid during the msgpack_zone instance alive. */
    msgpack_zone mempool;
    msgpack_zone_init(&mempool, 2048);

    msgpack_object deserialized;
    msgpack_unpack(sbuf.data, sbuf.size, NULL, &mempool, &deserialized);

    /* print the deserialized object. */
    msgpack_object_print(stdout, deserialized);
    puts("");

    msgpack_zone_destroy(&mempool);
    msgpack_sbuffer_destroy(&sbuf);

    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_msgpack_test2_obj, mod_msgpack_test2);

STATIC const mp_map_elem_t mp_module_msgpack_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_umsgpack) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_pack), (mp_obj_t)&mod_msgpack_pack_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_unpack), (mp_obj_t)&mod_msgpack_unpack_obj },
//    { MP_OBJ_NEW_QSTR(MP_QSTR_test), (mp_obj_t)&mod_msgpack_test_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test2), (mp_obj_t)&mod_msgpack_test2_obj },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_msgpack_globals, mp_module_msgpack_globals_table);

const mp_obj_module_t mp_module_msgpack = {
    .base = { &mp_type_module },
    .name = MP_QSTR_umsgpack,
    .globals = (mp_obj_dict_t*)&mp_module_msgpack_globals,
};
