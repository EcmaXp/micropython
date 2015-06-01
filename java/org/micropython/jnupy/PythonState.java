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
import java.io.ByteArrayInputStream;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;

public class PythonState {
	public static final int MEMORY_SCALE;
	static {
		String model = System.getProperty("sun.arch.data.model");
		switch (model) {
			case "32":
				MEMORY_SCALE = 1;
				break;
			case "64":
				MEMORY_SCALE = 2;
				break;
			default:
				MEMORY_SCALE = 1;
		}
	}
	
	public static final long DEFAULT_STACK_SIZE = 1024 * 32 * MEMORY_SCALE; // 32 KB
	public static final long DEFAULT_HEAP_SIZE = 1024 * 256 * MEMORY_SCALE; // 256 KB
	
    static {
    	System.load(System.getenv("MICROPYTHON_LIB"));
		// NativeSupport.getInstance().getLoader().load();
		// MICROPYTHON_VERSION = jnupy_version();
	}
	
	public static void main(String args[]) {
		System.gc(); // Remove incorrect reference.
		
		PythonState py = new PythonState();
		System.out.println(new Object());
		py.jnupy_test_jni_state();
		py.jnupy_test_jni();
		py.jnupy_jfunc_set("hello", new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object ... args) {
			    for (Object o : args) {
			    	if (o instanceof Object[]) {
			    		Object[] array = (Object[])o;
			    		System.out.println("start");
			    		for (int i=0; i < array.length; i++) {
						    System.out.println(array[i]);
						}
			    		System.out.println("end");
			    	}
			        System.out.println(o);
			    }
				return args[0];
			}
		});
		System.out.println("helloX");
		py.jnupy_jobj_set("hello", new Object());
		System.out.println("helloY");
		System.out.println(py.jnupy_code_exec("x=jnupy.jfuncs['hello']; print(repr(jnupy.jfuncs['hello']), x); x(1, 2, 3.4, None)"));
		System.out.println("helloZ");
		py.jnupy_code_eval("print(repr(jnupy.jrefs['hello']))");
		py.jnupy_code_exec("print(jnupy.pyrefs)");
		PythonNativeFunction print = (PythonNativeFunction)py.jnupy_code_eval("print");
		print.invoke(1, 2, 3);
		System.out.println(print.toString());
	}
	
	public static final int APIVERSION = 1;
	
	// store pointer, never access on java side!
	private long mpState;
	
	public PythonState() {
		this(DEFAULT_STACK_SIZE, DEFAULT_HEAP_SIZE);
	}

	public PythonState(long heap_size) {
		this(DEFAULT_STACK_SIZE, heap_size);
	}

	public PythonState(long stack_size, long heap_size) {
		if (!jnupy_state_new(stack_size, heap_size)) {
			throw new IllegalStateException("Generate python state is failed.");
		}
		
		// jnupy is python-side module
		jnupy_code_exec("import jnupy");
	}

	public boolean checkState(long mpStateId) {
		return (mpState == mpStateId);
	}
	
	public final synchronized boolean isOpen() {
		return jnupy_state_check();
	}
	
	public synchronized void close() {
		if (jnupy_state_check()) {
			jnupy_state_free();
		}
	}
	
	private void check() {
		if (!jnupy_state_check()) {
			throw new IllegalStateException("Python state is closed");
		}
	}
	
	// TODO: public as private
	// native function list: jnupy.c
	
	public native synchronized void jnupy_test_jni();
	public native synchronized void jnupy_test_jni_state();
	public native synchronized void jnupy_test_jni_fail();
	public native synchronized boolean jnupy_state_new(long stack_size, long heap_size);
	public native synchronized void jnupy_state_free();
	public native synchronized boolean jnupy_state_exist();
	public native synchronized boolean jnupy_state_check();
	public native synchronized boolean jnupy_code_exec(String code);
	public native synchronized Object jnupy_code_eval(String code);
	public native synchronized boolean jnupy_jfunc_set(String name, JavaFunction jfunc);
    public native synchronized boolean jnupy_jobj_set(String name, Object jobj);
    public native synchronized String jnupy_obj_repr(PythonNativeObject pyobj);
    public native synchronized long jnupy_ref_incr(PythonNativeObject pyobj);
    public native synchronized void jnupy_ref_derc(PythonNativeObject pyobj);
    public native synchronized boolean jnupy_func_vaild(String name);
    public native synchronized Object jnupy_func_call(Boolean convertResult, PythonNativeObject func, Object ...args);

    // How to persist? no idea...
}
