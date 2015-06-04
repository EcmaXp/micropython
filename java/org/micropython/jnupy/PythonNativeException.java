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
import java.util.regex.Pattern;

public class PythonNativeException extends PythonException {
    // only jnupy.c can throw this exception.
    // TODO: have PythonObject?
    
    private String name;
    private String detail;
    
	private PythonNativeException(String msg) {
	    super(msg);
	    parseMessage(msg);
	}

	private PythonNativeException(String msg, Throwable cause) {
		super(msg, cause);
	    parseMessage(msg);
	}
	
	private void parseMessage(String msg) {
	    String[] splited = msg.split(Pattern.quote(": "), 2);
	    if (splited.length == 1) {
	        detail = splited[0];
	    } else if (splited.length == 2) {
	        name = splited[0];
	        detail = splited[1];
	    }
	}

	@Override	
	public String getName() {
	    return name;
	}
	
	@Override
	public String getDetail() {
	    return detail;
	}
}
