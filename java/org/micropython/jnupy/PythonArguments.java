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

import java.util.HashMap;

public class PythonArguments {
    /* TODO
    // - assert length
    // - get args by slice?
    // - get kwargs as dict
    */
    
    private PythonObject[] args;
    private HashMap<String, PythonObject> kwargs; // can be null!
    public final int length;
    
    public PythonArguments(PythonObject[] args) {
        this(args, null);
    }
    
    public PythonArguments(PythonObject[] args, HashMap<String, PythonObject> kwargs) {
        this.args = args;
        this.kwargs = kwargs; // can be null!
        this.length = args.length;
    }
    
    public PythonObject rawGet(int index) {
        return this.args[index];
    }
    
    public PythonObject rawGet(String key) {
        if (this.kwargs != null) {
            PythonObject result = this.kwargs.get(key);
            if (result != null) {
                return result;
            }
        }
        
        // TODO: change exception... (to what?)
        throw new IndexOutOfBoundsException();
    }
    
    private Object convertObject(PythonObject obj) throws PythonException {
        return obj.toJavaObject();
    }
    
    public Object get(int index) throws PythonException {
        return convertObject(rawGet(index));
    }
    
    public Object get(String key) throws PythonException {
        return convertObject(rawGet(key));
    }
}
