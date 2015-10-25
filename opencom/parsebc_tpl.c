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

#include <assert.h>

#include "py/obj.h"
#include "py/bc0.h"
#include "parsebc.h"
#include "parsebc0.h"

/*<MECRO_RESULT>*/

void mp_parsebc(const byte *ip, mp_uint_t len, mp_parsebc_handler_t handler, void *handler_data) {
    const byte const *code_start = ip;
    while (ip < len + code_start) {
        mp_parsebc_opdata_t opdata = mp_parsebc_op(code_start, ip);
        ip = opdata.next_ip;
        handler(handler_data, &opdata);
    }
}

mp_parsebc_opdata_t mp_parsebc_op(const byte const *code_start, const byte *ip) {
    mp_parsebc_opdata_t opdata = {ip, ip, -1, false, false, false};

    /*<RESULT>*/
}

STATIC void mp_parsebc_count_sub(void *counter_ptr, const mp_parsebc_opdata_t *opdata);

mp_parsebc_opcounter_t *mp_parsebc_count(const byte *ip, mp_uint_t len) {
    mp_parsebc_opcounter_t *opcounter = m_new0(mp_parsebc_opcounter_t, 1);
    mp_parsebc(ip, len, &mp_parsebc_count_sub, opcounter);
    return opcounter;
}

STATIC void mp_parsebc_count_sub(void *counter_ptr, const mp_parsebc_opdata_t *opdata) {
    mp_parsebc_opcounter_t *counter = counter_ptr;
    counter->op_count++;

    if (opdata->is_ptr)
        counter->ptr_count++;
    if (opdata->is_num)
        counter->num_count++;
    if (opdata->is_unum)
        counter->unum_count++;
    if (opdata->is_qstr)
        counter->qstr_count++;
    if (opdata->has_extra)
        counter->extra_count++;
}
