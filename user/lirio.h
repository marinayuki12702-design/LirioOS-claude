/* Biblioteca mínima para programas de LirioOS (sin libc): llamadas al sistema por int 0x80.
 *   eax = número de llamada, ebx/ecx = argumentos, resultado en eax. */
#ifndef LIRIO_USER_H
#define LIRIO_USER_H

typedef unsigned int u32;

static inline int syscall3(int n, u32 a, u32 b)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b) : "memory");
    return r;
}

static inline void salir(int codigo)  { syscall3(1, (u32)codigo, 0); for (;;) { } }
static inline int  escribir(const char *s, u32 n) { return syscall3(2, (u32)s, n); }
static inline u32  ticks(void)        { return (u32)syscall3(3, 0, 0); }
static inline void dormir(u32 t)      { syscall3(4, t, 0); }
static inline int  mi_pid(void)       { return syscall3(5, 0, 0); }

static inline u32 largo(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static inline void texto(const char *s) { escribir(s, largo(s)); }
static inline void numero(u32 v)
{
    char b[12];
    int i = 11;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    texto(b + i);
}

#endif
