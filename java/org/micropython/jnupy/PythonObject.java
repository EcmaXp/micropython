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
    long refObject;

    PythonObject(PythonState pyState, long mpStateId, long refId) {
        if (pyState == null || !pyState.checkState(mpStateId)) {
            throw new IllegalStateException("Python state is invaild.");
        }

        pythonState = pyState;
        refObject = refId;
        refIncr();
    }

    PythonObject(PythonState pyState, long refId) {
        pyState.check();

        pythonState = pyState;
        refObject = refId;
        refIncr();
    }

    // TODO: make close method
    // public void close() { }

    protected void finalize() throws Throwable {
        refDerc();
    }

    private void refIncr() {
        pythonState.jnupy_ref_incr(refObject);
    }

    private void refDerc() {
        pythonState.jnupy_ref_derc(refObject);
    }

    public static PythonObject fromObject(Object obj) {
        if (obj == null) {
            throw new NullPointerException();
        } else if (obj instanceof PythonObject) {
			return (PythonObject)obj;
		}

		// TODO: change excpetion
		throw new RuntimeException("invaild python raw object: " + obj.toString());
	}

    public Object toJavaObject() throws PythonException {
        return pythonState.jnupy_obj_py2j(this);
    }

    // TODO: move to PythonState?
    private PythonObject getHelper(String name) {
        return pythonState.builtins.get(name);
    }

    private PythonObject helper(String name, Object... args) throws PythonException {
        PythonObject func = getHelper(name);
        return func.rawCall(args);
    }

    private Object rawhelper(String name, Object... args) throws PythonException {
        PythonObject func = getHelper(name);
        return func.invoke(args);
    }
    // END TODO

    public String toString() {
        try {
            PythonObject repr = pythonState.builtins.get("repr");
            Object result = repr.invoke(this);
            return this.getClass().getName() + "[" + result.toString() + "]";
        } catch (Exception e) {
            return super.toString();
        }
    }

    private void checkState(PythonState pyState) {
        if (pythonState != pyState) {
            throw new RuntimeException("invaild state (not match)");
        }
    }

    public Object invoke(Object... args) throws PythonException {
        return pythonState.jnupy_func_call(true, this, args);
    }

    public PythonObject call(Object... args) throws PythonException {
        Object result = pythonState.jnupy_func_call(true, this, args);
        if (result == null) {
            return null;
        }

        return PythonObject.fromObject(result);
    }

    public PythonObject rawCall(Object... args) throws PythonException {
        return PythonObject.fromObject(pythonState.jnupy_func_call(false, this, args));
    }

    // TODO: getitem, getattr, etc impl in here? (by builtin)

    public PythonObject attr(String name) throws PythonException {
        return getattr(name);
    }

    public void attr(String name, Object value) throws PythonException {
        setattr(name, value);
    }

    public PythonObject getattr(String name) throws PythonException {
        return helper("getattr", this, name);
    }

    public PythonObject getattr(String name, Object defvalue) throws PythonException {
        return helper("getattr", this, name, defvalue);
    }

    public void setattr(String name, Object value) throws PythonException {
        rawhelper("setattr", this, name, value);
    }

    public PythonObject hasattr(String name) throws PythonException {
        return helper("hasattr", this, name);
    }

    public void delattr(String name) throws PythonException {
        rawhelper("delattr", this, name);
    }

    public Object unbox() throws PythonException {
        return pythonState.helpers.get("unbox").invoke(this);
    }

    // getitem or setitem require helper function... (not builtin function.)
}
