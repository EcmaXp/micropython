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
import java.util.Scanner;
import java.util.NoSuchElementException;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.FileSystems;

public class PythonState extends PythonNativeState {
	public static final long DEFAULT_STACK_SIZE = 1024 * 32 * MEMORY_SCALE; // 32 KB
	public static final long DEFAULT_HEAP_SIZE = 1024 * 256 * MEMORY_SCALE; // 256 KB
	
	// TODO: private?
	HashMap<String, PythonObject> builtins;
	HashMap<String, PythonObject> helpers;
	
	public static void main(String args[]) {
		System.gc(); // Remove incorrect reference.
		PythonState py;
		
		try {
			py = new PythonState();
		} catch (PythonException e) {
			throw new RuntimeException("createing state are failed", e);
		}
		
		try {
			PythonObject importer = py.builtins.get("__import__");
			PythonModule modsys = (PythonModule)importer.call("sys");
			String rompath = System.getenv("MPOC_ROM");
			String libpath = System.getenv("MICROPYTHON_BATTERY");
	
			modsys.get("path").attr("append").call(rompath);
			modsys.get("path").attr("append").call(libpath);
			
			PythonObject argv = py.builtins.get("list").rawCall();
			argv.attr("append").call("<java>");
			for (String arg : args) {
				argv.attr("append").call(arg);
			}
			
			modsys.set("argv", py.builtins.get("tuple").rawCall(argv));
			
			PythonObject modbios = importer.call("bios");
			PythonObject result = modbios.attr("main").rawCall();
		} catch (PythonException e) {
			String name = e.getName();
			if (name.equals("SystemExit")) {
				System.exit(0);
			}
			
			e.printStackTrace();
		}
	}
	
	public PythonState() throws PythonException {
		this(DEFAULT_STACK_SIZE, DEFAULT_HEAP_SIZE);
	}

	public PythonState(long heap_size) throws PythonException {
		this(DEFAULT_STACK_SIZE, heap_size);
	}
	
	public PythonState(long stack_size, long heap_size) throws PythonException {
		super(stack_size, heap_size);

		builtins = new HashMap<String, PythonObject>();
		helpers = new HashMap<String, PythonObject>();
		
		setup();
	}
	
	public void setup() throws PythonException {
		PythonObject importer = pyEval("__import__");	
		PythonModule modjnupy = (PythonModule)importer.call("jnupy");
		PythonModule modbuiltins = (PythonModule)importer.call("builtins");

		JavaFunction load = new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object... args) {
				if (args[1] != null && args[1] instanceof PythonObject) {
					pythonState.builtins.put((String)args[0], (PythonObject)args[1]);
				}
				
				return null;
			}
		};

		String loader = "lambda x, m: [x(s, getattr(m,s)) for s in dir(m)]";
		pyEval(loader).rawCall(load, modbuiltins);
		
		execute("def __raise__(exc):\n\traise exc");
		helpers.put("raise", pyEval("__raise__"));
		execute("del __raise__");
		
		helpers.put("unbox", pyEval("lambda x: x"));
		
		modjnupy.set("input", new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object... args) throws PythonException {
				Scanner scan = new Scanner(System.in);
				try {
					return scan.nextLine();
				} catch (NoSuchElementException e) {
					return null;
				}
			}
		});
		
		modjnupy.set("readfile", new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object... args) throws PythonException {
				if (args.length == 0) {
					return null;
				}
				
				return readFile((String)args[0]);
			}
		});
		
		// TODO: move to module.xxx
		PythonModule modutime = newModule("utime");
		
		modutime.set("time", new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object... args) {
				if (args.length != 0) {
					throw new RuntimeException("invaild argument... ?");
				}
				
				return new Double(System.currentTimeMillis() / 1000);
			}
		});
		
		modutime.set("sleep", new JavaFunction() {
			@Override
			public Object invoke(PythonState pythonState, Object... args) {
				if (args.length != 1) {
					throw new RuntimeException("invaild argument... ?");
				}
				
				try {
					if (args[0] instanceof Float) {
						Float val = (Float)args[0];
						Thread.sleep(new Float(val * 1000).longValue());
					} else if (args[0] instanceof Double) {
						Double val = (Double)args[0];
						Thread.sleep(new Double(val * 1000).longValue());
					} else {
						throw new RuntimeException("invaild argument... ?");
					}
				} catch (InterruptedException e) {
					// TODO: exception
					throw new RuntimeException("?", e);
				}
				
				return null;
			}
		});
		
		// other thing should override also, example: map.
	}
	
	public void execute(String code) throws PythonException {
		PythonObject func = jnupy_code_compile(code, PythonParseInputKind.MP_PARSE_FILE_INPUT);
		func.invoke();
	}
	
	public Object eval(String code) throws PythonException {
		PythonObject func = jnupy_code_compile(code, PythonParseInputKind.MP_PARSE_EVAL_INPUT);
		return func.invoke();
	}
	
	public Object rawEval(String code) throws PythonException {
		PythonObject func = jnupy_code_compile(code, PythonParseInputKind.MP_PARSE_EVAL_INPUT);
		return func.rawInvoke();
	}
	
	public PythonObject pyEval(String code) throws PythonException {
		return PythonObject.fromObject(rawEval(code));
	}
	
	public PythonModule newModule(String name) throws PythonException {
		return new PythonModule(newRawModule(name));
	}
	
	public PythonObject newRawModule(String name) throws PythonException {
		return jnupy_module_new(name);
	}
	
	private File resolvePath(String path) {
		return Paths.get("").resolve(path).toFile();
	}
	
	public PythonImportStat readStat(String path) {
		File file = resolvePath(path);
		
		try {
			if (!file.exists()) {
				// ...
			} else if (file.isFile() && file.canRead()) {
				return PythonImportStat.MP_IMPORT_STAT_FILE;
			} else if (file.isDirectory()) {
				return PythonImportStat.MP_IMPORT_STAT_DIR;
			}
 		} catch (SecurityException e) {
			// ...
		}
		
		return PythonImportStat.MP_IMPORT_STAT_NO_EXIST; // not exists? (TODO: enum?)
	}
	
	public String readFile(String filename) {
		File file = resolvePath(filename);
		byte[] encoded;
		
		try {
			encoded = Files.readAllBytes(file.toPath());
		} catch (IOException e) {
			// TODO: exception...
			throw new RuntimeException("?", e);			
		} catch (SecurityException e) {
			// TODO: exception...
			throw new RuntimeException("?", e);
		}
		
		return new String(encoded, StandardCharsets.UTF_8);
	}
}
