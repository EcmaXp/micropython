# Enable/disable modules and 3rd-party libs to be included in interpreter
# And custom settings?

# Build 32-bit binaries on a 64-bit host
MICROPY_FORCE_32BIT = 0

# Subset of CPython time module
MICROPY_PY_TIME = 1

# Subset of CPython socket module
MICROPY_PY_SOCKET = 1

# Subset of msgpack module
MICROPY_PY_MSGPACK = 1

# Subset of persist module
MICROPY_PY_PERSIST = 1

# Allow pause Virtual Machine
MICROPY_ALLOW_PAUSE_VM = 1

# Allow control cpu
MICROPY_LIMIT_CPU = 1

# Allow multi state in one process
MICROPY_MULTI_STATE_CONTEXT = 1

# Allow override assert for include as library (for stable process)
MICROPY_OVERRIDE_ASSERT_FAIL = 1
