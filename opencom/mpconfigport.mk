# Enable/disable modules and 3rd-party libs to be included in interpreter

# Build 32-bit binaries on a 64-bit host
MICROPY_FORCE_32BIT = 0

# Subset of CPython time module
MICROPY_PY_TIME = 1

# Subset of CPython socket module
MICROPY_PY_SOCKET = 1

# Allow pause Virtual Machine
MICROPY_ALLOW_PAUSE_VM = 1
