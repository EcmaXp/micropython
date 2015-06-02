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

public class PythonNativeState {
	// TODO: PythonNativeState is correct name?
	
    static {
    	System.load(System.getenv("MICROPYTHON_LIB"));
		// NativeSupport.getInstance().getLoader().load();
		// MICROPYTHON_VERSION = jnupy_version();
	}
	
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
	
	private static final int APIVERSION = 1;
	
	// store pointer, never access on java side!
	private long mpState;

	public PythonNativeState(long stack_size, long heap_size) {
		if (!jnupy_state_new(stack_size, heap_size)) {
			throw new IllegalStateException("Generate python state is failed.");
		}
	}

	// Internal usage only in java-side
	public boolean checkState(long mpStateId) {
		return (mpState == mpStateId);
	}
	
	public final synchronized boolean isOpen() {
		return jnupy_state_check();
	}
	
	public synchronized void close() {
		if (isOpen()) {
			jnupy_state_free();
		}
	}
	
	void check() {
		if (!isOpen()) {
			throw new IllegalStateException("Python state is closed.");
		}
	}
	
	// native function list: jnupy.c
	native synchronized boolean jnupy_state_new(long stack_size, long heap_size);
	native synchronized boolean jnupy_state_check();
	native synchronized void jnupy_state_free();
    native synchronized long jnupy_ref_incr(PythonObject pyobj);
    native synchronized void jnupy_ref_derc(PythonObject pyobj);
	native synchronized Object jnupy_execute(boolean convertResult, int flag, String code);
    native synchronized Object jnupy_func_call(boolean convertResult, PythonObject func, Object ...args);
}
