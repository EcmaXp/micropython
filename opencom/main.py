#!/usr/bin/env micropython-jnupy
# -*- coding: utf-8 -*-
"""micropython with java

"""

__LICENSE__ = """
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
"""

# TODO: Makefile install 'micropython-jnupy'
import sys

try:
    import jnupy
except ImportError:
    sys.exit("This script should running with jnupy")

__path__ = __file__.rpartition("/")[0]
sys.path.append(__path__ + "/lib")

# TODO: site module?

if len(sys.argv) == 1:
    from code import interact
    import builtins
    builtins.exit = sys.exit
    interact(local=dict(__name__="__main__"))
    print()
    sys.exit()
elif sys.argv[1] == __file__:
    # What?
    pass
else:
    # ('<java>', '-X', 'emit=bytecode', 'basics/0prelim.py')
    # add jnupy to handle special argument (check unix/main.c)
    
    if sys.argv[1:3] == ["-X", 'emit=bytecode']:
        sys.argv = [sys.argv[0]] + sys.argv[3:]
    elif sys.argv[1].startswith("-"):
        print('argument {!r} is not supported'.format(sys.argv[1]))
        sys.exit(1)

    filename = sys.argv[1]
    basedir = filename.rpartition("/")[0]
    basedir = jnupy.abspath(basedir)
    sys.path.append(basedir)

    try:
        content = jnupy.readfile(filename)
    except Exception as e:
        print('reading file {!r} failed'.format(filename))
        print(e)
        exit(1)
    sys.argv = sys.argv[1:]
    local = dict(__name__="__main__")
    # TODO: do as runpy...
    #       - special exception for launch file?
    exec(content, local, local)
    sys.exit()
