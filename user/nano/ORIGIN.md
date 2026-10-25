Source: https://github.com/antoniodev84/Nano-Toolchain
Commit: c5372c30d2bb7e09a262eda52cb5d273566e2e1c

The runtime, C frontend, assembler and linker are from Nano-Toolchain.
The Aster port uses the native syscall ABI and installs ncc, nas and nld.
The compiler searches /usr/include and links /usr/lib/crt0.o and /usr/lib/libc.a from Aster.
