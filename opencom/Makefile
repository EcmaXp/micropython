include ../py/mkenv.mk
-include mpconfigport.mk

# define main target
PROG = libmicropython.so
PROGBIN = micropython
PROGJAR = micropython.jar

# check java environ
ifeq ($(JAVA_HOME),)
$(error Require setup JAVA_HOME)
endif

# setup java environ
BUILD_JAVA = $(BUILD)/java

JAVA = $(JAVA_HOME)/bin/java
JAVAC = $(JAVA_HOME)/bin/javac
JAVAP = $(JAVA_HOME)/bin/javap
JAR = $(JAVA_HOME)/bin/jar
JAVACFLAGS =

# test directory
TESTDIR = \
	opencom \
	basics \
	micropython \
	float \
	import \
	io \
	misc \
	unicode \
	extmod \
	cmdline \
	unix

TESTFLAGS = -d $(TESTDIR)

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# OS name, for simple autoconfig
UNAME_S := $(shell uname -s)

# include py core make definitions
include ../py/py.mk

INC =  -I.
INC +=  -I..
INC += -I$(BUILD)

# jni include files
INC += -I$(JAVA_HOME)/include
# TODO: replace this by arch...
INC += -I$(JAVA_HOME)/include/linux

# compiler settings
CWARN = -Wall -Werror
CWARN += -Wpointer-arith -Wuninitialized
CFLAGS = $(INC) $(CWARN) -ansi -std=gnu99 -DUNIX $(CFLAGS_MOD) $(COPT) $(CFLAGS_EXTRA)

# Debugging/Optimization
ifdef DEBUG
CFLAGS += -g
JAVACFLAGS += -g
COPT = -O0
else
COPT = -Os -DNDEBUG
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

# jnupy settings
$(BUILD)/jnupy.o: jnupy.c
	$(Q)$(PYTHON) jnupy_ap.py jnupy.c
	$(call compile_c)

CFLAGS_EXTRA += -fPIC

LDFLAGS_EXTRA += -shared
LDFLAGS_EXTRA += -z noexecstack

ifeq ($(MICROPY_FORCE_32BIT),1)
# Note: you may need to install i386 versions of dependency packages,
# starting with linux-libc-dev:i386
CFLAGS += -m32
LDFLAGS += -m32
endif

ifeq ($(MICROPY_OVERRIDE_ASSERT_FAIL),1)
CFLAGS_MOD += -DMICROPY_OVERRIDE_ASSERT_FAIL=1
INC +=  -I../lib/assert
endif

ifeq ($(MICROPY_MULTI_STATE_CONTEXT),1)
CFLAGS_MOD += -DMICROPY_MULTI_STATE_CONTEXT=1
CFLAGS_MOD += -DMICROPY_NLR_SETJMP=1
endif

ifeq ($(MICROPY_PY_PERSIST),1)
CFLAGS_MOD += -DMICROPY_PY_PERSIST=1
SRC_MOD += modpersist.c
endif

ifeq ($(MICROPY_PY_MICROTHREAD),1)
SRC_MOD += modmicrothread.c
endif

# source files
SRC_C = \
	jnupy.c \
	gccollect.c \
	$(SRC_MOD)

OBJ = $(PY_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

include ../py/mkrules.mk

all: $(PROG) $(PROGJAR) vaild-jni

clean: clean-jar
clean-jar:
	$(RM) -f $(PROGJAR)
.PHONY: clean

$(BUILD_JAVA):
	$(MKDIR) -p $(BUILD_JAVA)

# TODO: wildcard discard or change to other thing...
$(PROGJAR): $(BUILD_JAVA) ../java/org/micropython/jnupy/*
	$(JAVAC) $(JAVACFLAGS) ../java/org/micropython/jnupy/*.java -d $(BUILD_JAVA)
	$(JAR) -cvmf ../java/MANIFEST-jnupy $(PROGJAR) -C $(BUILD_JAVA) . || exit 1
	
print-jni: $(PROGJAR)
	$(JAVAP) -cp $(PROGJAR) -s org.micropython.jnupy.PythonNativeState

vaild-jni: $(PROGJAR)
	$(Q)MICROPYTHON_LIB=$(abspath $(PROG)) $(JAVA) -cp $(PROGJAR) org.micropython.jnupy.PythonNativeState

test: $(PROG) $(PROGJAR) ../tests/run-tests
	$(eval DIRNAME=$(notdir $(CURDIR)))
	cd ../tests && MICROPY_MICROPYTHON=../$(DIRNAME)/$(PROGBIN) ./run-tests $(TESTFLAGS)

.PHONY: test
