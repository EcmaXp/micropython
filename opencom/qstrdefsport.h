/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

// qstrs specific to this port

Q(Test)

Q(fileno)
Q(makefile)

Q(FileIO)
Q(flush)

Q(_os)
Q(stat)
Q(system)
Q(unlink)

Q(input)
Q(utime)
Q(time)
Q(clock)
Q(sleep)

Q(socket)
Q(sockaddr_in)
Q(htons)
Q(inet_aton)
Q(gethostbyname)
Q(getaddrinfo)
Q(usocket)
Q(connect)
Q(bind)
Q(listen)
Q(accept)
Q(recv)
Q(setsockopt)
Q(setblocking)

Q(AF_UNIX)
Q(AF_INET)
Q(AF_INET6)
Q(SOCK_STREAM)
Q(SOCK_DGRAM)
Q(SOCK_RAW)

Q(MSG_DONTROUTE)
Q(MSG_DONTWAIT)

Q(SOL_SOCKET)
Q(SO_BROADCAST)
Q(SO_ERROR)
Q(SO_KEEPALIVE)
Q(SO_LINGER)
Q(SO_REUSEADDR)

// mpoc
Q(mpoc)
Q(test)
Q(test1)
Q(test2)
Q(test3)
Q(test4)
Q(get_state_ident)
Q(get_loaded_modules)
Q(builtin_modules)

// microthread
Q(microthread)
Q(umicrothread)
Q(MicroThread)
Q(_MicroThread)
Q(_init)
Q(current_thread)
Q(resume)
Q(prev_thread)
// return kind
Q(normal)
Q(yield)
Q(exception)
Q(limit)
Q(pause)
Q(force_pause)
Q(running)
Q(stop)
// limit type
Q(limit_soft)
Q(limit_hard)

Q(STATUS_NORMAL)
Q(STATUS_YIELD)
Q(STATUS_EXCEPTION)
Q(STATUS_LIMIT)
Q(STATUS_PAUSE)
Q(STATUS_FORCE_PAUSE)
Q(STATUS_STOP)
Q(STATUS_RUNNING)
Q(LIMIT_SOFT)
Q(LIMIT_HARD)

#if MICROPY_LIMIT_CPU
Q(cpu_hard_limit)
Q(cpu_soft_limit)
Q(cpu_safe_limit)
Q(cpu_current_executed)
#endif

// msgpack
Q(msgpack)
Q(umsgpack)
Q(dumps)
Q(loads)
Q(test)
Q(test1)
Q(test2)

// persist
Q(persist)
Q(upersist)
Q(Persister)
Q(UnPersister)
Q(dumps)
Q(loads)
Q(test)
Q(test1)
Q(test2)

// jnupy
Q(jnupy)
Q(JFunction)
Q(last_jfunc)
