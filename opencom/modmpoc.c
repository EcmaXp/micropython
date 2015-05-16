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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "py/nlr.h"
#include "py/runtime.h"
#include "py/objtuple.h"

STATIC mp_obj_t mod_mpoc_test(mp_obj_t asdf) {
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_mpoc_test_obj, mod_mpoc_test);

STATIC mp_obj_t mod_mpoc_get_loaded_modules(void) {
    mp_obj_dict_t *self = m_new_obj(mp_obj_dict_t);
    self->base.type = &mp_type_dict;
    self->map = MP_STATE_VM(mp_loaded_modules_map);
    
    mp_obj_t copy_fun = mp_load_attr(self, MP_QSTR_copy);
    return mp_call_function_0(copy_fun);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_mpoc_get_loaded_modules_obj, mod_mpoc_get_loaded_modules);

STATIC const mp_map_elem_t mp_module_mpoc_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_mpoc) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_test), (mp_obj_t)&mod_mpoc_test_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_builtin_modules), (mp_obj_t)&mp_builtin_module_dict },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_loaded_modules), (mp_obj_t)&mod_mpoc_get_loaded_modules_obj },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_mpoc_globals, mp_module_mpoc_globals_table);

const mp_obj_module_t mp_module_mpoc = {
    .base = { &mp_type_module },
    .name = MP_QSTR_mpoc,
    .globals = (mp_obj_dict_t*)&mp_module_mpoc_globals,
};
