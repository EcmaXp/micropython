include ../py/mkenv.mk
-include mpconfigport.mk

# define main target
PROG = micropython

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# OS name, for simple autoconfig
UNAME_S := $(shell uname -s)

# include py core make definitions
include ../py/py.mk

INC =  -I.
INC +=  -I..
INC += -I$(BUILD)

# compiler settings
CWARN = -Wall -Werror
CWARN += -Wpointer-arith -Wuninitialized
CFLAGS = $(INC) $(CWARN) -ansi -std=gnu99 -DUNIX $(CFLAGS_MOD) $(COPT) $(CFLAGS_EXTRA)

# Debugging/Optimization
ifdef DEBUG
CFLAGS += -g
COPT = -O0
else
COPT = -Os #-DNDEBUG
endif

# msgpack (used for communicate with OpenComputers)
# question: just static link with msgpack? (maybe no)
# TODO: check env (MICROPY_PY_MSGAPCK?)
LDFLAGS_MOD += -lmsgpack

ifeq ($(MICROPY_OVERRIDE_ASSERT_FAIL),1)
CFLAGS_MOD += -DMICROPY_OVERRIDE_ASSERT_FAIL=1
INC +=  -I../lib/assert
endif

ifeq ($(MICROPY_BUILD_JNI_LIBRARY),1)

ifeq ($(JAVA_HOME),)
$(error Require setup JAVA_HOME...)
endif

PROG = libmicropython.so
SRC_MOD += jnupy.c

INC += -I$(JAVA_HOME)/include
# TODO: replace this by arch...
INC += -I$(JAVA_HOME)/include/linux

CFLAGS_MOD += -DMICROPY_BUILD_JAVA_NATIVE_INTERFACE=1
CFLAGS_EXTRA += -fPIC

LDFLAGS_EXTRA += -shared
LDFLAGS_EXTRA += -z noexecstack
else
SRC_MOD += main.c
endif

# On OSX, 'gcc' is a symlink to clang unless a real gcc is installed.
# The unix port of micropython on OSX must be compiled with clang,
# while cross-compile ports require gcc, so we test here for OSX and 
# if necessary override the value of 'CC' set in py/mkenv.mk
ifeq ($(UNAME_S),Darwin)
CC = clang
# Use clang syntax for map file
LDFLAGS_ARCH = -Wl,-map,$@.map
else
# Use gcc syntax for map file
LDFLAGS_ARCH = -Wl,-Map=$@.map,--cref
endif
LDFLAGS = $(LDFLAGS_MOD) $(LDFLAGS_ARCH) -lm $(LDFLAGS_EXTRA)

ifeq ($(MICROPY_FORCE_32BIT),1)
# Note: you may need to install i386 versions of dependency packages,
# starting with linux-libc-dev:i386
CFLAGS += -m32
LDFLAGS += -m32
endif

ifeq ($(MICROPY_MULTI_STATE_CONTEXT),1)
CFLAGS_MOD += -DMICROPY_MULTI_STATE_CONTEXT=1
CFLAGS_MOD += -DMICROPY_NLR_SETJMP=1
endif

ifeq ($(MICROPY_PY_TIME),1)
CFLAGS_MOD += -DMICROPY_PY_TIME=1
SRC_MOD += modtime.c
endif
ifeq ($(MICROPY_PY_SOCKET),1)
CFLAGS_MOD += -DMICROPY_PY_SOCKET=1
SRC_MOD += modsocket.c
endif
ifeq ($(MICROPY_PY_MSGPACK),1)
CFLAGS_MOD += -DMICROPY_PY_MSGPACK=1
SRC_MOD += modmsgpack.c
endif
ifeq ($(MICROPY_PY_PERSIST),1)
CFLAGS_MOD += -DMICROPY_PY_PERSIST=1
SRC_MOD += modpersist.c
endif
ifeq ($(MICROPY_ALLOW_PAUSE_VM),1)
CFLAGS_MOD += -DMICROPY_ALLOW_PAUSE_VM=1
SRC_MOD += modmicrothread.c
endif
ifeq ($(MICROPY_LIMIT_CPU),1)
CFLAGS_MOD += -DMICROPY_LIMIT_CPU=1
endif

# source files
SRC_C = \
	file.c \
	gccollect.c \
	modos.c \
	modmpoc.c \
	$(SRC_MOD)

OBJ = $(PY_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o)) 

include ../py/mkrules.mk

.PHONY: test

test: $(PROG) ../tests/run-tests
	$(eval DIRNAME=$(notdir $(CURDIR)))
	cd ../tests && MICROPY_MICROPYTHON=../$(DIRNAME)/$(PROG) ./run-tests

# install micropython in /usr/local/bin
TARGET = micropython
PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin
PIPSRC = ../tools/pip-micropython
PIPTARGET = pip-micropython

jnilib:
	$(Q)$(CC) $(COPT) -o $@ $(OBJ) $(LIB) $(LDFLAGS)

install: micropython
	install -D $(TARGET) $(BINDIR)/$(TARGET)
	install -D $(PIPSRC) $(BINDIR)/$(PIPTARGET)

# uninstall micropython
uninstall:
	-rm $(BINDIR)/$(TARGET)
	-rm $(BINDIR)/$(PIPTARGET)

# build an interpreter for coverage testing and do the testing
coverage:
	$(MAKE) COPT="-O0" CFLAGS_EXTRA='-fprofile-arcs -ftest-coverage -Wdouble-promotion -Wformat -Wmissing-declarations -Wmissing-prototypes -Wold-style-definition -Wpointer-arith -Wshadow -Wsign-compare -Wuninitialized -Wunused-parameter' LDFLAGS_EXTRA='-fprofile-arcs -ftest-coverage' BUILD=build-coverage PROG=micropython_coverage

coverage_test: coverage
	$(eval DIRNAME=$(notdir $(CURDIR)))
	cd ../tests && MICROPY_MICROPYTHON=../$(DIRNAME)/micropython_coverage ./run-tests
	gcov -o build-coverage/py ../py/*.c
