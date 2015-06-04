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

public class PythonState extends PythonNativeState {
	public static final long DEFAULT_STACK_SIZE = 1024 * 32 * MEMORY_SCALE; // 32 KB
	public static final long DEFAULT_HEAP_SIZE = 1024 * 256 * MEMORY_SCALE; // 256 KB
	
	// TODO: replace to enum?
	public static final int JNUPY_EXECUTE_SINGAL = 1;
	public static final int JNUPY_EXECUTE_EXEC = 2;
	public static final int JNUPY_EXECUTE_EVAL = 3;
	
	// TODO: private?
	public HashMap<String, PythonObject> builtins;
	
	public static void main(String args[]) {
		System.gc(); // Remove incorrect reference.
		
		PythonState py = new PythonState();
		PythonObject print = py.builtins.get("print");
		PythonObject globals = py.builtins.get("globals");
		PythonObject getitem = py.pyEval("lambda x, y: x[y]");
		print.invoke(1, 2, 3);
		PythonObject importer = py.builtins.get("__import__");
		PythonObject microthread = importer.call("umicrothread");
		PythonObject MicroThread = microthread.getattr("MicroThread");
		// https://github.com/benelog/multiline ?
		String hello = "" +
			"import umicrothread\n" +
			"def hello():\n" +
			"	umicrothread.pause()";
		py.execute(hello);
		PythonObject func = MicroThread.call("hello", getitem.call(globals.call(), "hello"));
		System.out.println(func.getattr("resume").call());
		System.out.println(func.getattr("resume").call());
	}
	
	public PythonState() {
		this(DEFAULT_STACK_SIZE, DEFAULT_HEAP_SIZE);
	}

	public PythonState(long heap_size) {
		this(DEFAULT_STACK_SIZE, heap_size);
	}
	
	public PythonState(long stack_size, long heap_size) {
		super(stack_size, heap_size);
		
		builtins = new HashMap<String, PythonObject>();
		
		// jnupy is python-side module
		execute("import jnupy");
		
		/* TODO: push jnupy setting helper
		A. init internal usage function; access jnupy module only for
		    - getattr(..., name, None)
		    - setattr(..., name, value)
		B. get raw builtin module and function, class, etc.
		C. set custom __import__ function.
		*/
		
		// TODO: load stream and do load
		// execute("");
		
		PythonObject loader = pyEval("lambda x, y: setattr(jnupy, x, y)");
		loader.invoke("load_builtin", new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object... args) {
				if (args.length == 2 && args[1] instanceof PythonObject && args[0] instanceof String) {
					pythonState.builtins.put((String)args[0], (PythonObject)args[1]);
				}
				
				return null;
			}
		});
		
		execute("import builtins");
		execute("for name in dir(builtins): jnupy.load_builtin(name, getattr(builtins, name))"); // ...
	}
	
	public void execute(String code) {
		PythonObject func = jnupy_code_compile(code, JNUPY_EXECUTE_EXEC);
		func.invoke();
	}
	
	public Object eval(String code) {
		PythonObject func = jnupy_code_compile(code, JNUPY_EXECUTE_EVAL);
		return func.invoke();
	}
	
	public Object rawEval(String code) {
		PythonObject func = jnupy_code_compile(code, JNUPY_EXECUTE_EVAL);
		return func.rawInvoke();
	}
	
	public PythonObject pyEval(String code) {
		Object result = rawEval(code);
		if (result instanceof PythonObject) {
			return (PythonObject)result;
		}
		
		// TODO: change excpetion
		throw new RuntimeException("invaild python raw object return: " + result.toString());
	}
}
