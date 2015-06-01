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
	
	// TODO: private?
	HashMap<String, PythonFunction> builtin_functions;
	
	public static void main(String args[]) {
		System.gc(); // Remove incorrect reference.
		
		PythonState py = new PythonState();

		PythonObject global = py.pyEval("globals()");
		PythonFunction set = (PythonNativeFunction)py.jnupy_code_eval(true, "lambda x, y, z: x.__setitem__(y, z)");
		System.out.println(global);
		set.invoke(global, "hello", new JavaFunction() {
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
		
		PythonNativeFunction print = (PythonNativeFunction)py.jnupy_code_eval(true, "print");
		print.invoke(1, 2, 3);
		System.out.println(print.toString());
		
		py.jnupy_code_eval(false, "hello(1, 2, 3)");
	}
	
	public PythonState() {
		this(DEFAULT_STACK_SIZE, DEFAULT_HEAP_SIZE);
	}

	public PythonState(long heap_size) {
		this(DEFAULT_STACK_SIZE, heap_size);
	}
	
	public PythonState(long stack_size, long heap_size) {
		super(stack_size, heap_size);
		
		// jnupy is python-side module
		execute("import jnupy");
		
		builtin_functions = new HashMap<String, PythonFunction>();
	
		PythonFunction loader = (PythonFunction)eval("lambda x: setattr(jnupy, 'loader', x)");
		loader.invoke(new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object... args) {
				if (args[1] instanceof PythonFunction && args[0] instanceof String) {
					pythonState.builtin_functions.put((String)args[0], (PythonFunction)args[1]);
				}
				
				return null;
			}
		});
		execute("import builtins");
		execute("for name in dir(builtins): x=getattr(builtins, name); (jnupy.loader(name, x) if (callable(x) and type(x) != type) else None)"); // ...
	}
	
	public void execute(String code) {
		jnupy_code_exec(code);
	}
	
	public Object eval(String code) {
		return jnupy_code_eval(true, code);
	}
	
	public Object rawEval(String code) {
		return jnupy_code_eval(false, code);
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
