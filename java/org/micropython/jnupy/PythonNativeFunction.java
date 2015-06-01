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

package org.micropython.jnupy;

// TODO: remove PythonNativeFunction?

public class PythonNativeFunction extends PythonNativeObject implements PythonFunction {
    PythonNativeFunction(PythonState pyState, long mpStateId, long objectId) {
        super(pyState, mpStateId, objectId);
    }
    
    private void checkState(PythonState pyState) {
        if (pythonState != pyState) {
            throw new RuntimeException("invaild state (not match)");
        }
    }
    
    @Override
    public Object invoke(PythonState pyState, Object... args) {
        checkState(pyState);
        return this.invoke(args);
    }
    
    @Override
    public Object invoke(Object... args) {
        return pythonState.mp_func_call(true, this, args);
    }
    
    @Override
    public Object rawInvoke(PythonState pyState, Object... args) {
        checkState(pyState);
        return this.rawInvoke(args);
    }
    
    @Override
    public Object rawInvoke(Object... args) {
        return pythonState.mp_func_call(false, this, args);
    }
    
    
}
