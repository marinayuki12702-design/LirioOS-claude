/* Acceso a los puertos de entrada/salida del procesador (x86).
 * Para las pruebas en un PC normal se compila con -DLIRIO_HOST y las pruebas aportan host_inb/host_outb. */

#ifndef LIRIO_IO_H
#define LIRIO_IO_H

#ifdef LIRIO_HOST

static unsigned char host_inb(unsigned short port);
static void host_outb(unsigned short port, unsigned char val);
static inline u8 inb(u16 port) { return host_inb(port); }
static inline void outb(u16 port, u8 val) { host_outb(port, val); }

#else

static inline u8 inb(u16 port)
{
    u8 r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

#endif

/* Lee/escribe 'count' palabras de 16 bits desde/hacia un puerto (lo usa el disco) */
static inline void insw(u16 port, void *addr, u32 count)
{
    __asm__ volatile ("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(u16 port, const void *addr, u32 count)
{
    __asm__ volatile ("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

#endif
