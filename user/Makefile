CC = gcc
CFLAGS = -m32 -ffreestanding -fno-pic -fno-pie -fno-builtin -fno-stack-protector -O2 -Wall -Wextra -nostdlib -static
LDFLAGS = -Wl,-T,user.ld -Wl,-m,elf_i386 -Wl,--build-id=none -Wl,-z,noexecstack

all: hola.elf falla.elf

%.elf: %.c lirio.h user.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f *.elf
