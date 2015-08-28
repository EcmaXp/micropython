# Enable/disable modules and 3rd-party libs to be included in interpreter
# And custom settings?

# Build 32-bit binaries on a 64-bit host
MICROPY_FORCE_32BIT = 0

# Subset of persist module
MICROPY_PY_PERSIST = 1

# Subset of microthread module
MICROPY_PY_MICROTHREAD = 1

# Allow multi state in one process
MICROPY_MULTI_STATE_CONTEXT = 1

# Allow override assert for include as library (for stable process)
# TODO: overrideing assert will raise unknown segfault.
MICROPY_OVERRIDE_ASSERT_FAIL = 0
