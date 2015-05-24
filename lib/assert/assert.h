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
#ifndef __MICROPY_INCLUDED_LIB_ASSERT_H__
#define __MICROPY_INCLUDED_LIB_ASSERT_H__

#include "py/mpconfig.h"

#if !MICROPY_OVERRIDE_ASSERT_FAIL
#error This library (assert.h) should include by MICROPY_OVERRIDE_ASSERT_FAIL
#endif

#define __MP_ASSERT_NEVER_EXECUTE(x) (void)0
// http://stackoverflow.com/questions/10593492/catching-assert-with-side-effects

NORETURN void mp_assert_fail(const char *__assertion, const char *__file,
                             unsigned int __line, const char *__function);

#if NDEBUG
#define assert(expr) __MP_ASSERT_NEVER_EXECUTE(expr)
#else
#define assert(expr) ((expr)? (void)0 : \
    mp_assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

#endif //__MICROPY_INCLUDED_LIB_ASSERT_H__
