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

public class PythonState {
    static {
    	System.load(System.getenv("MICROPYTHON_LIB"));
		// NativeSupport.getInstance().getLoader().load();
		// MICROPYTHON_VERSION = mp_version();
	}
	
	public static void main(String args[]) {
		System.gc(); // Remove incorrect reference.
		
		PythonState py = new PythonState();
		py.mp_state_new();
		py.mp_test_jni_state();
		py.mp_test_jni();
		py.mp_jfunc_set("hello", new JavaFunction() {
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
		py.mp_code_exec("from jnupy import jfuncs, pyrefs; import jnupy");
		py.mp_code_exec("print(jfuncs)");
		py.mp_code_exec("x = jfuncs['hello']({3: 2}, object()); y = jfuncs['hello'](x); print('[', x, ']; [', y, ']')");
		py.mp_code_exec("print(pyrefs)");
		System.out.println(py.mp_code_eval("3 + 2"));
		System.gc();
		py.mp_code_exec("print(pyrefs)");	
		System.gc();
		System.out.println(py.mp_code_eval("3 + 2"));
		py.mp_code_exec("print(pyrefs)");
	}
	
	public static final int APIVERSION = 1;
	
	// store pointer, never access on java side!
	private long mpState;
	
	public PythonState() {
		System.out.println("PythonState are generated.");
		mp_state_new();
		System.out.println(mpState);
	}
	
	public void test() {
		System.out.println("hello from java");
		mp_test_jni();
		System.out.println("hello from java2");
	}
	
	public boolean checkState(long mpStateId) {
		return (mpState == mpStateId);
	}
	
	// TODO: public as private (until test done?)
	// TODO: build this first, and define in jnupy.c later
	// this is native function list
	
	public native synchronized void mp_test_jni();
	public native synchronized void mp_test_jni_state();
	public native synchronized void mp_test_jni_fail();
	public native synchronized boolean mp_state_new();
	public native synchronized boolean mp_state_free();
	public native synchronized boolean mp_state_exist();
	public native synchronized boolean mp_state_check();
	public native synchronized boolean mp_code_exec(String code);
	public native synchronized Object mp_code_eval(String code);
	public native synchronized boolean mp_jfunc_set(String name, JavaFunction jfunc);
    public native synchronized void mp_ref_incr(PythonObject pyobj);
    public native synchronized void mp_ref_derc(PythonObject pyobj);
}
