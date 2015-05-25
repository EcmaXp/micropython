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

public class PythonState {
    static {
    	System.load(System.getenv("MICROPYTHON_LIB"));
		// NativeSupport.getInstance().getLoader().load();
		// MICROPYTHON_VERSION = mp_version();
	}
	
	public static void main(String args[]) {
		PythonState py = new PythonState();
		py.mp_state_new();
		py.mp_test_jni_state();		
		py.mp_test_jni();
		py.mp_state_new();
		py.mp_state_new();
		py.mp_code_exec("print(3)");
	}
	
	public static final int APIVERSION = 1;
	
	public PythonState() {
		System.out.println("PythonState are generated.");
	}
	
	public void test() {
		System.out.println("hello from java");
		mp_test_jni();
		System.out.println("hello from java2");
	}
	
	// TODO: public as private (until test done?)
	// TODO: build this first, and define in jnupy.c later
	// this is native function list
	
	public native void mp_test_jni();
	public native void mp_test_jni_state();
	public native void mp_test_jni_fail();
	public native boolean mp_state_new();
	public native boolean mp_state_free();
	public native boolean mp_state_exist();
	public native boolean mp_state_check();
	public native boolean mp_code_exec(String nope);
}
