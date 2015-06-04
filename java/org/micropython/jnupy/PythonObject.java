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

public class PythonObject {
    PythonState pythonState;
    private long mpObject;

    PythonObject(PythonState pyState, long mpStateId, long objectId) {
        if (!pyState.checkState(mpStateId)) {
            // TODO: change exception
            throw new RuntimeException("invaild state");
        }
        
        pythonState = pyState;
        mpObject = objectId;
        pythonState.jnupy_ref_incr(this);
    }
    
    // TODO: move to PythonState?
    private PythonObject getHelper(String name) {
        return pythonState.builtins.get(name);
    }
    
    private PythonObject helper(String name, Object... args) {
        PythonObject func = getHelper(name);
        return func.call(args);
    }
    // END TODO
    
    public String toString() {
        PythonObject repr = pythonState.builtins.get("repr");
        Object result = repr.invoke(this);
        return "PythonObject[" + result.toString() + "]";
    }

    protected void finalize() throws Throwable {
        pythonState.jnupy_ref_derc(this);
    }
    
    private void checkState(PythonState pyState) {
        if (pythonState != pyState) {
            throw new RuntimeException("invaild state (not match)");
        }
    }
    
    public Object invoke(Object... args) {
        return pythonState.jnupy_func_call(true, this, args);
    }
    
    public Object rawInvoke(Object... args) {
        return pythonState.jnupy_func_call(false, this, args);
    }
    
    public PythonObject call(Object... args) {
        Object result = pythonState.jnupy_func_call(false, this, args);
		if (result instanceof PythonObject) {
			return (PythonObject)result;
		}
		
		// TODO: change excpetion
		throw new RuntimeException("invaild python raw object return: " + result.toString());
    }
    
    // TODO: getitem, getattr, etc impl in here? (by builtin)

    public PythonObject getattr(String name) {
        return helper("getattr", this, name);
    }

    public PythonObject setattr(String name, Object value) {
        return helper("setattr", this, name, value);
    }
    
    public PythonObject hasattr(String name) {
        return helper("hasattr", this, name);
    }
    
    public PythonObject delattr(String name) {
        return helper("delattr", this, name);
    }
    
    /*
    public Object unbox() {
        return pythonState.unboxValue(x);
    }
    */
    
    // getitem or setitem require helper function... (not builtin function.)
}
