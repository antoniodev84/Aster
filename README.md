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

Boot mounts the first supported ext2 SATA filesystem as `/`. Without one, it uses a volatile RAM root and reports that choice. The initrd populates missing files and refreshes `/bin`, `/sbin`, `/usr/include` and `/usr/lib` on every boot. Other existing files persist. `/sbin/init` starts and supervises the interactive shell. Commands and logs are in English.

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

There are 48 separate command ELF64 executables, plus separate `sh` and `init` executables:

```
cat chmod chown clear cp echo edit env false grep head hexdump help
id kill ln ls mapwrite mkdir printf ps pwd readlink rm rmdir seq
sigwatch sleep sort spin stat su sync tail tee test time touch
true uname wc whoami yes ping ifconfig udhcp httpd wget
```

`edit FILE` is a full-screen editor with scrolling, cursor movement, insertion/deletion, Ctrl-S save and Ctrl-Q quit. `sh` supports scripts, quoting, variables/positional arguments, command substitution, globbing, redirections, pipelines, logical lists, background jobs, job control, subshells, if/elif/else, for/while/until, traps and shell builtins. Each external command is independently loaded by the ELF loader.

## Networking

Original PCI drivers support Intel 82540EM/82545EM e1000 devices (8086:100e and 8086:100f) and Realtek RTL8139 (10ec:8139). DMA descriptor rings, bounded receive polling, link detection and transmission run on the boot CPU. Up to four Ethernet interfaces are supported. The timer services reception, TCP retransmissions, DNS and DHCP even while applications are blocked.

The network stack is original code in `kernel/src/net`, with no imported network implementation. It provides Ethernet ARP, IPv4 routing, fragmentation/reassembly, ICMP echo and error handling, UDP checksums and datagram queues, and TCP active/passive opens, sequence tracking, receive reordering, sliding windows, congestion control, retransmission timers, zero-window probes, half-close and TIME-WAIT. DNS resolves A records and CNAME chains, checks question and transaction identities, caches TTLs and falls back to TCP for truncated replies. DHCP implements discovery, requesting, address-conflict probes, leases, renewal, rebinding, release and expiry. The timer services the stack while applications are blocked.

The shared ABI and libc expose `socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `shutdown`, `getsockname`, `getpeername`, `setsockopt` and `getsockopt`. Socket descriptors also support `read`, `write`, `close`, `dup`, `fork`, nonblocking flags and close-on-exec. Supported socket options are reuse-address, broadcast, pending error and receive/send timeouts. Raw ICMP sockets and network configuration require root. IPv4 conversion, `getaddrinfo`, `gethostbyname`, `netresolve`, `netconfig` and `netdhcp` are available in libc.

QEMU starts with an e1000 device by default. Use `make run-bios TOOLCHAIN=llvm NET_DEVICE=rtl8139` to select RTL8139. `NET_FLAGS` overrides the complete network command line. For inbound HTTP from the host:

```
make run-bios TOOLCHAIN=llvm NET_FLAGS='-netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-:8080 -device e1000,netdev=net0'
```

Inside Aster, discover the interface name, request a lease, then use the network:

```
ifconfig
udhcp -i en1 -t 30
ping -c 4 10.0.2.2
ping -c 1 example.com
wget -O /tmp/page http://HOST/PATH
mkdir /tmp/www
echo hello > /tmp/www/index.html
httpd -p 8080 -h /tmp/www &
```

Interface names come from `ifconfig`; Ethernet interfaces are named `en1` through `en4`. The stack also routes 127.0.0.0/8 through an internal loopback path. The gateway and addresses above are QEMU user-network defaults. Internet ICMP forwarding depends on the host. Static configuration uses `ifconfig en1 inet ADDRESS netmask MASK gateway ADDRESS dns ADDRESS default`. `udhcp -i en1 -r` releases a lease. The kernel keeps leases renewed after `udhcp` exits.

`ping` accepts `-c COUNT`, `-W TIMEOUT_SECONDS` and `-s PAYLOAD_BYTES`, checks ICMP checksums and reports loss and round-trip times. `wget` supports HTTP, numeric ports, DNS names, redirects, content-length, chunked transfer coding and connection-delimited responses; `-O -` writes the body to stdout. It reports truncated or malformed responses as errors. `httpd` forks per connection, serves GET and HEAD, limits headers and client timeouts, and confines file lookup with directory descriptors and no-follow opens. IPv6, HTTPS/TLS and complete Linux/POSIX socket compatibility are outside the implemented interface.

## Current boundaries

Only x86_64 and the boot CPU are supported. Executables are static ET_EXEC ELF64; there is no dynamic linker. The kernel is not preempted inside syscall handlers. The console and editor use ASCII, and libc uses the C locale without threads, wide-character formatting or long-double printf formatting. The shell is not a complete POSIX shell: functions, arithmetic expansion and here-documents are not implemented. Utilities provide their documented local options rather than full GNU compatibility.

Ext2 supports raw filesystems and MBR Linux partitions with 1/2/4 KiB blocks. It rejects unsupported features; there is no journal, extents or modification of indexed directories. Directory rename is unsupported. Storage completion is polled with bounded timeouts. Firmware/hardware support has been exercised in QEMU, not certified on physical machines.

Validation uses direct builds and manual VM interaction; no test suite or test scripts are added.

The original network stack was compiled with Clang/LLD and exercised in QEMU with both e1000 and RTL8139. Manual checks covered DHCP acquisition/release, DNS A/CNAME and TCP fallback, ICMP echo, fragmented 8192-byte UDP echo and socket timeouts, and 409600-byte HTTP transfers with matching SHA-256 hashes. Sixteen concurrent HTTP requests succeeded on each driver. A direct Ethernet peer exercised TCP SYN/data retransmission, receive reordering across sequence-number wrap, small/zero windows and persist probes, simultaneous open and orderly close. Short DHCP leases exercised renewal, rebinding and rediscovery after expiry. IPv4 checks exercised reordered/duplicate fragments and rejection of overlaps, invalid final extents and bad checksums. No automated test suite is included. Physical hardware and external Internet ICMP delivery remain unverified.

## Nano C toolchain

The userland includes `ncc`, `nas` and `nld`, built from Nano-Toolchain
commit `c5372c30d2bb7e09a262eda52cb5d273566e2e1c`.
Sources and native syscall glue are in `user/nano`.
No additional third-party library is linked into the userland.

`ncc` searches `/usr/include` and automatically links Aster's own
`/usr/lib/crt0.o` and `/usr/lib/libc.a`. The installed libc uses fewer ELF
sections to remain within the kernel's program-header limit.

Inside Aster:

```
ncc /root/hello.c -o /tmp/hello
/tmp/hello
ncc -c /root/hello.c -o /tmp/hello.o
nld -o /tmp/hello /tmp/hello.o /usr/lib/crt0.o /usr/lib/libc.a
```

Nano implements a subset of C99. Floating-point types remain unsupported;
floating-point declarations are hidden from its view of the libc headers.
The installed stdarg and stddef headers come from Nano. `_Noreturn` and GCC
format attributes are ignored by this compiler. Multiple constructor arrays
are rejected by the native linker.

For a host with GCC and GNU ld, the userland can also be built with:

```
make -C user TOOLCHAIN=gnu
```

Full-system builds retain Aster's original external boot dependencies
(Limine, protocol headers and freestanding headers/runtime). They require
host image-building tools and QEMU to boot. No replacements for these
components were added by the Nano integration.

The integrated image was built with Clang/LLD 19 and booted in QEMU 10
with BIOS firmware, AHCI/ext2 and 512 MiB RAM. Manual shell interaction
compiled `/root/hello.c` with `ncc`, ran the resulting executable, and
repeated compilation and linking separately with `ncc -c` and `nld`.
Both executables printed `Hello from Nano on Aster` inside the guest.
The ELF loader accepts empty load segments emitted by GNU ld.
