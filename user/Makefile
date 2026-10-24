CC := clang
AR := ar
LD := ld.lld
CFLAGS := -target x86_64-unknown-none-elf -std=c11 -O2 -g -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -mno-red-zone -ffunction-sections -fdata-sections -Wall -Wextra -nostdinc -Ilibc/include -I../include
LDFLAGS := -m elf_x86_64 -static --gc-sections -z noexecstack -T linker.ld
LIBCSRC := $(wildcard libc/src/*.c)
LIBCOBJ := $(patsubst libc/src/%.c,build/libc/%.o,$(LIBCSRC)) build/libc/start.o
COMMANDSRC := $(wildcard commands/*.c)
COMMANDS := $(patsubst commands/%.c,root/bin/%,$(COMMANDSRC))
.PHONY: all clean
all: root/sbin/init root/bin/sh $(COMMANDS) initrd.tar
build/libc/%.o: libc/src/%.c $(wildcard libc/include/*.h libc/include/sys/*.h) ../include/abi.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
build/libc/start.o: libc/src/start.S
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
build/libc.a: $(LIBCOBJ)
	$(AR) rcs $@ $(LIBCOBJ)
root/sbin/init: init/main.c build/libc.a linker.ld
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@.o
	$(LD) $(LDFLAGS) build/libc/start.o $@.o build/libc.a -o $@
	rm -- $@.o
	chmod 755 $@
root/bin/sh: shell/main.c build/libc.a linker.ld
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@.o
	$(LD) $(LDFLAGS) build/libc/start.o $@.o build/libc.a -o $@
	rm -- $@.o
	chmod 755 $@
root/bin/%: commands/%.c build/libc.a linker.ld $(wildcard commands/*.h)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@.o
	$(LD) $(LDFLAGS) build/libc/start.o $@.o build/libc.a -o $@
	rm -- $@.o
	chmod 755 $@
initrd.tar: root/sbin/init root/bin/sh $(COMMANDS) $(shell find root -type f ! -path 'root/bin/*' ! -path 'root/sbin/*' 2>/dev/null)
	mkdir -p root/dev root/proc root/sys root/tmp root/root root/etc root/mnt
	chmod 755 root root/bin root/sbin root/etc root/dev root/proc root/sys root/mnt
	chmod 700 root/root
	chmod 644 root/etc/* root/root/*.txt
	chmod 755 root/bin/* root/sbin/*
	chmod 1777 root/tmp
	tar --format=ustar --owner=0 --group=0 -cf $@ -C root .
clean:
	rm -r build root/bin root/sbin initrd.tar
