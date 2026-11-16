CC      = gcc
AS      = nasm
LD      = ld
CFLAGS  = -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -nostdlib
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

all: lirioos-kernel.bin

boot.o: boot.asm
	$(AS) -f elf32 boot.asm -o boot.o

kernel.o: kernel.c gui.h font.h fs.h images.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

lirioos-kernel.bin: boot.o kernel.o linker.ld gui.h font.h
	$(LD) $(LDFLAGS) boot.o kernel.o -o lirioos-kernel.bin

# Genera una ISO arrancable (necesita grub-mkrescue y xorriso)
iso: lirioos-kernel.bin
	mkdir -p iso/boot/grub
	cp lirioos-kernel.bin iso/boot/lirioos-kernel.bin
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o LirioOS-kernel.iso iso

# Prueba rápida si tienes QEMU instalado
run: iso
	qemu-system-i386 -cdrom LirioOS-kernel.iso

clean:
	rm -rf *.o *.bin iso *.iso
