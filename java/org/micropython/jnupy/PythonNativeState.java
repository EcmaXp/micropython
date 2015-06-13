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
import java.io.IOException;
import java.util.HashMap;

public class PythonNativeState {
	// TODO: PythonNativeState is correct name?
	public static final String MICROPYTHON_VERSION; // 1.4.1?
	
    static {
    	NativeSupport.getInstance().getLoader().load();
		MICROPYTHON_VERSION = jnupy_mp_version();
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
	
	public static void main(String args[]) {
		PythonNativeState py = new PythonNativeState(PythonState.DEFAULT_STACK_SIZE, PythonState.DEFAULT_HEAP_SIZE);
		py.close();
	}
	
	// store pointer, never access on java side!
	private long mpState;

	public PythonNativeState(long stack_size, long heap_size) {
		if (!jnupy_state_new(stack_size, heap_size)) {
			throw new IllegalStateException("Generate python state is failed.");
		}
	}

	public final synchronized boolean isOpen() {
		return jnupy_state_check();
	}

	// Internal usage only in java-side
	boolean checkState(long mpStateId) {
		return (mpState == mpStateId) && isOpen();
	}
	
	synchronized void close() {
		if (isOpen()) {
			jnupy_state_free();
		}
	}
	
	void check() {
		if (!isOpen()) {
			throw new IllegalStateException("Python state is closed.");
		}
	}
	
	// Override on subclass.
	public void print(byte[] buf) {
		try {
			System.out.write(buf);	
		} catch (IOException e) {
			// what the...
			// TODO: change exception type, message.
			throw new RuntimeException("unexcepted error", e);
		}
	}
	
	public PythonImportStat readStat(String path) {
		return PythonImportStat.MP_IMPORT_STAT_NO_EXIST;
	}
	
	public String readFile(String filename) {
		return "";
	}
	
	// TODO: add new exception method? (name and detail and PythonObject exc?)
	
	// native function list: jnupy.c
	native static String jnupy_mp_version();
	native synchronized boolean jnupy_state_new(long stack_size, long heap_size);
	native synchronized boolean jnupy_state_check();
	native synchronized void jnupy_state_free();
    native synchronized void jnupy_ref_incr(long refobj);
    native synchronized void jnupy_ref_derc(long refobj);
	native synchronized PythonObject jnupy_code_compile(String code, PythonParseInputKind kind) throws PythonException;
    native synchronized Object jnupy_func_call(boolean convertResult, PythonObject func, Object ...args) throws PythonException;
    native synchronized PythonObject jnupy_module_new(String name) throws PythonException;
    native synchronized Object jnupy_obj_py2j(PythonObject obj) throws PythonException;
    native synchronized void jnupy_func_arg_check_num(int n_args, int n_kw, int n_args_min, int n_args_max, boolean takes_kw);
}
