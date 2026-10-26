CC := clang
AR := ar
LD := ld.lld
TARGETFLAGS := -target x86_64-unknown-none-elf
ifeq ($(TOOLCHAIN),gnu)
CC := gcc
LD := ld
TARGETFLAGS :=
endif
CFLAGS := $(TARGETFLAGS) -std=c11 -O2 -g -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -mno-red-zone -ffunction-sections -fdata-sections -Wall -Wextra -nostdinc -Ilibc/include -I../include
LDFLAGS := -m elf_x86_64 -static --gc-sections -z noexecstack -T linker.ld
LIBCSRC := $(wildcard libc/src/*.c)
LIBCOBJ := $(patsubst libc/src/%.c,build/libc/%.o,$(LIBCSRC)) build/libc/start.o
COMMANDSRC := $(wildcard commands/*.c)
COMMANDS := $(patsubst commands/%.c,root/bin/%,$(COMMANDSRC))
NANOSRC := $(wildcard nano/tool/*.c) nano/lib/runtime.c nano/platform/aster.c
NANOOBJ := $(patsubst nano/%.c,build/nano/%.o,$(NANOSRC)) build/nano/platform/start.o
NANOBINS := root/bin/ncc root/bin/nas root/bin/nld
NANOHEADERS := $(patsubst libc/include/%,root/usr/include/%,$(shell find libc/include -type f -name '*.h'))
.PHONY: all clean
all: $(NANOBINS) root/usr/lib/crt0.o root/usr/lib/libc.a $(NANOHEADERS) root/usr/include/abi.h root/usr/include/stdarg.h root/sbin/init root/bin/sh $(COMMANDS) initrd.tar
build/libc/%.o: libc/src/%.c $(shell find libc/include -type f -name '*.h') ../include/abi.h
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
initrd.tar: $(NANOBINS) root/usr/lib/crt0.o root/usr/lib/libc.a $(NANOHEADERS) root/usr/include/abi.h root/usr/include/stdarg.h root/sbin/init root/bin/sh $(COMMANDS) $(shell find root -type f ! -path 'root/bin/*' ! -path 'root/sbin/*' 2>/dev/null)
	mkdir -p root/dev root/proc root/sys root/tmp root/root root/etc root/mnt
	chmod 755 root root/bin root/sbin root/etc root/dev root/proc root/sys root/mnt
	chmod 700 root/root
	chmod 644 root/etc/*
	chmod 755 root/bin/* root/sbin/*
	chmod 1777 root/tmp
	tar --format=ustar --owner=0 --group=0 -cf $@ -C root .
clean:
	rm -rf build root/bin root/sbin root/usr initrd.tar

build/nano/%.o: nano/%.c $(wildcard nano/include/*.h) ../include/abi.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -Inano/include -c $< -o $@
build/nano/platform/start.o: nano/platform/start.S
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
root/bin/ncc: $(NANOOBJ) linker.ld
	mkdir -p $(@D)
	$(LD) $(LDFLAGS) $(NANOOBJ) -o $@
	chmod 755 $@
root/bin/nas root/bin/nld: root/bin/ncc
	cp $< $@
root/usr/lib/crt0.o: build/libc/start.o
	mkdir -p $(@D)
	cp $< $@
build/nano-libc/%.o: libc/src/%.c $(shell find libc/include -type f -name '*.h') ../include/abi.h
	mkdir -p $(@D)
	$(CC) $(filter-out -ffunction-sections -fdata-sections,$(CFLAGS)) -c $< -o $@
build/nano-libc.a: $(patsubst libc/src/%.c,build/nano-libc/%.o,$(LIBCSRC))
	$(AR) rcs $@ $^
root/usr/lib/libc.a: build/nano-libc.a
	mkdir -p $(@D)
	cp $< $@
root/usr/include/abi.h: ../include/abi.h
	mkdir -p $(@D)
	cp $< $@
root/usr/include/stdarg.h root/usr/include/stddef.h: root/usr/include/%: nano/include/c99/%
	mkdir -p $(@D)
	cp $< $@
$(filter-out root/usr/include/stdarg.h root/usr/include/stddef.h,$(NANOHEADERS)): root/usr/include/%: libc/include/%
	mkdir -p $(@D)
	cp $< $@

GUISRC := $(wildcard apps/*.c)
GUIAPPS := $(patsubst apps/%.c,root/usr/apps/bin/%,$(GUISRC))
all: root/bin/asterwm $(GUIAPPS) root/usr/lib/libgui.a root/usr/include/gui.h
build/gui/%.o: gui/%.c gui/gui.h gui/font.h ../include/abi.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -Igui -c $< -o $@
build/libgui.a: build/gui/gui.o
	$(AR) rcs $@ $^
root/bin/asterwm: build/gui/asterwm.o build/libgui.a build/libc.a linker.ld
	mkdir -p $(@D)
	$(LD) $(LDFLAGS) build/libc/start.o $< build/libgui.a build/libc.a -o $@
	chmod 755 $@
root/usr/apps/bin/%: apps/%.c build/libgui.a build/libc.a gui/gui.h linker.ld
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -Igui -c $< -o $@.o
	$(LD) $(LDFLAGS) build/libc/start.o $@.o build/libgui.a build/libc.a -o $@
	rm -- $@.o
	chmod 755 $@
root/usr/lib/libgui.a: build/libgui.a
	mkdir -p $(@D)
	cp $< $@
root/usr/include/gui.h: gui/gui.h
	mkdir -p $(@D)
	cp $< $@
initrd.tar: root/bin/asterwm $(GUIAPPS) root/usr/lib/libgui.a root/usr/include/gui.h
