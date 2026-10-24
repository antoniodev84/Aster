# Aster

An original x86_64 operating system built on the Limine C template. The kernel, drivers, filesystem implementations, libc, shell, editor and utilities added here are authored in this repository. Limine, its protocol headers, the template build infrastructure and firmware remain external boot dependencies; no external kernel, libc, shell or editor implementation is ported.

## Build and run

Requirements: GNU make, Clang/LLVM with LLD, ar, a host C compiler, git, curl, tar, xorriso, mke2fs and QEMU. The template can fetch its boot dependencies. Optional HDD boot images also require sgdisk and mtools.

```
make TOOLCHAIN=llvm
make run TOOLCHAIN=llvm
make run-bios TOOLCHAIN=llvm
```

The build produces `template-x86_64.iso`, `kernel/bin-x86_64/kernel`, user ELF64 executables, `user/initrd.tar`, and a writable `rootfs.img`. The default VM uses q35 with a SATA disk on AHCI, 512 MiB RAM, a framebuffer and serial console. The first root image is created as ext2 with 256-byte inodes; an existing image is preserved. Override `DISK_IMAGE`, `DISK_SIZE` and `MKE2FS` as needed. `make clean` preserves the root disk.

Boot mounts the first supported ext2 SATA filesystem as `/`. Without one, it uses a volatile RAM root and reports that choice. The initrd populates missing files and refreshes `/bin` and `/sbin` on every boot. Other existing files persist. `/sbin/init` starts and supervises the interactive shell. Commands and logs are in English.

## Kernel

The HAL separates architecture interfaces from the x86_64 backend. The backend provides GDT, TSS with exception stacks, IDT, ACPI discovery, local APIC, I/O APIC, real hardware interrupt routing, a calibrated periodic timer, RTC/TSC clocks, PS/2 keyboard/mouse and PCI enumeration. `kprintf` writes to both serial and framebuffer; the framebuffer console has an original font and ANSI cursor/color support.

Physical memory uses page allocation and reference counts. Four-level virtual memory separates user address spaces, validates user pointers, supports anonymous/private/shared file mappings and enforces writable/executable protections. Fork copies private pages and retains shared pages. Shared file mappings write dirty pages back to storage.

Processes run static ELF64 executables in ring 3. The timer preempts user code, with per-process kernel stacks, TSS stack updates, CR3 switches and x87/SSE state preservation. Native SYSCALL/SYSRET handles system calls. The scheduler provides blocking I/O, pipes, sleep, wait/reaping, process groups and signals with user handlers, masks, stop/continue, saved user contexts and signal return. Kernel execution is serialized on the boot CPU.

The original AHCI driver performs SATA identification, LBA48 DMA transfers and cache flushes. The ext2 driver reads and writes files, directories, metadata, hard links and symbolic links, including indirect block allocation and truncation. Root credentials, permissions, ownership, sticky directories and set-ID execution are enforced. Open unlinked files remain usable until their final reference closes.

The VFS mounts real device nodes under `/dev`, process information under `/proc`, and PCI information under `/sys`. The terminal supports canonical/raw input, foreground process groups, terminal signals and full-screen applications. Initrd loading uses a validated USTAR reader.

## Userspace

`include/abi.h` defines the shared ABI. Implemented interfaces include:

```
write read open close getdents64 mkdir rmdir link unlink yield brk
mmap munmap stat fstat fork exec wait4 signal kill chmod chown
chdir fchdir dup dup2 pipe lstat readlink symlink access ioctl
getpid getcwd getuid geteuid setuid seteuid exit
```

The libc also exports `mumap` as an alias for `munmap`, plus supporting interfaces for seeking, truncation, syncing, descriptors, clocks, process groups, signal masks and memory protection. Unsupported syscall numbers return `ENOSYS`.

The original static libc includes startup/exit, syscall wrappers, buffered stdio, integer and double formatting, scanning, string/memory routines, numeric conversions, environment handling, sorting, directory access and an mmap-backed allocator with splitting, coalescing and page release. It targets the available system calls; it is not a claim of complete POSIX or Linux ABI compatibility.

There are 43 separate command ELF64 executables, plus separate `sh` and `init` executables:

```
cat chmod chown clear cp echo edit env false grep head hexdump help
id kill ln ls mapwrite mkdir printf ps pwd readlink rm rmdir seq
sigwatch sleep sort spin stat su sync tail tee test time touch
true uname wc whoami yes
```

`edit FILE` is a full-screen editor with scrolling, cursor movement, insertion/deletion, Ctrl-S save and Ctrl-Q quit. `sh` supports scripts, quoting, variables/positional arguments, command substitution, globbing, redirections, pipelines, logical lists, background jobs, job control, subshells, if/elif/else, for/while/until, traps and shell builtins. Each external command is independently loaded by the ELF loader.

## Current boundaries

Only x86_64 and the boot CPU are supported. Executables are static ET_EXEC ELF64; there is no dynamic linker. The kernel is not preempted inside syscall handlers. The console and editor use ASCII, and libc uses the C locale without threads, sockets, wide-character formatting or long-double printf formatting. The shell is not a complete POSIX shell: functions, arithmetic expansion and here-documents are not implemented. Utilities provide their documented local options rather than full GNU compatibility.

Ext2 supports raw filesystems and MBR Linux partitions with 1/2/4 KiB blocks. It rejects unsupported features; there is no journal, extents or modification of indexed directories. Directory rename is unsupported. Storage completion is polled with bounded timeouts. Firmware/hardware support has been exercised in QEMU, not certified on physical machines.

Validation uses direct builds and manual VM interaction; no test suite or test scripts are added.
