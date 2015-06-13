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

public abstract class JavaFunction {
    abstract public Object invoke(PythonState pythonState, PythonArguments args) throws PythonException;
    public void checkInvoke(PythonState pythonState, int n_args, int n_kw) throws PythonException {
        // ...
    };
    
    public static abstract class JavaFun extends JavaFunction {
        static public final int inf = -1;
        boolean takes_kw;
        int n_args_min;
        int n_args_max;
        
        public JavaFun(boolean takes_kw, int n_args_min, int n_args_max) {
            assert(n_args_min <= n_args_max);
            assert(n_args_min >= 0);
            assert(n_args_max >= 0 || n_args_max == inf);

            this.takes_kw = takes_kw;
            this.n_args_min = n_args_min;
            this.n_args_max = n_args_max;
        }
        
        @Override
        public void checkInvoke(PythonState pythonState, int n_args, int n_kw) {
            boolean vaild = (takes_kw || n_kw == 0) &&
                (n_args_min <= n_args) &&
                (n_args <= n_args_max || n_args_max == inf);
                
            if (!vaild) {
                pythonState.jnupy_func_arg_check_num(
                    n_args, n_kw,
                    n_args_min, (n_args_max == inf? n_args: n_args_max),
                    takes_kw);
            }
        }
    }

    public static abstract class JavaFun0 extends JavaFun {
        public JavaFun0() {
            super(false, 0, 0);
        }
    }
    
    public static abstract class JavaFun1 extends JavaFun {
        public JavaFun1() {
            super(false, 1, 1);
        }
    }
    
    public static abstract class JavaFun2 extends JavaFun {
        public JavaFun2() {
            super(false, 2, 2);
        }
    }
    
    public static abstract class JavaFun3 extends JavaFun {
        public JavaFun3() {
            super(false, 3, 3);
        }
    }
    
    public static abstract class JavaFunVar extends JavaFun {
        public JavaFunVar(int n_args_min) {
            super(false, n_args_min, inf);
        }
    }
    
    public static abstract class JavaFunVarBetween extends JavaFun {
        public JavaFunVarBetween(int n_args_min, int n_args_max) {
            super(false, n_args_min, n_args_max);
        }
    }
    
    public static abstract class JavaFunKW extends JavaFun {
        public JavaFunKW(int n_args_min) {
            super(true, n_args_min, inf);
        }
    }
}
