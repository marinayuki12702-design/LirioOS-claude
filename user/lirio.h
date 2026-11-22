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

static inline int sys(int n, u32 a, u32 b, u32 c)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static inline int sys5(int n, u32 a, u32 b, u32 c, u32 d, u32 e)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return r;
}

static inline void salir(int codigo)  { syscall3(1, (u32)codigo, 0); for (;;) { } }
static inline int  escribir(const char *s, u32 n) { return syscall3(2, (u32)s, n); }
static inline u32  ticks(void)        { return (u32)syscall3(3, 0, 0); }
static inline void dormir(u32 t)      { syscall3(4, t, 0); }
static inline int  mi_pid(void)       { return syscall3(5, 0, 0); }

/* Espera una tecla y devuelve su código (Latin-1; '\n' = Enter, '\b' = borrar). Esc detiene el programa. */
static inline int  leer_tecla(void) { return sys(6, 0, 0, 0); }
/* Lee un archivo (ruta como en el terminal, p. ej. "/disco/notas.txt"); devuelve los bytes leídos o -1 */
static inline int  leer_archivo(const char *ruta, void *buf, u32 max) { return sys(7, (u32)ruta, (u32)buf, max); }
/* Crea o reemplaza un archivo; devuelve 0 o -1 */
static inline int  escribir_archivo(const char *ruta, const void *datos, u32 n) { return sys(8, (u32)ruta, (u32)datos, n); }
/* Pide 'n' bytes más de memoria (a cero); devuelve la dirección o -1 */
static inline void *pedir_memoria(u32 n) { int r = sys(9, n, 0, 0); return r == -1 ? (void *)0 : (void *)r; }

/* ---- Ventanas gráficas ----
 * Cada programa puede abrir ventanas con su propia superficie (colores 0xRRGGBB). Se dibuja con
 * ventana_rect / ventana_texto y se enseña con ventana_mostrar. Teclas y ratón llegan como eventos. */
struct evento { u32 tipo; u32 a, b, c; };     /* tipo 1 = tecla (a = código); tipo 2 = ratón (a = x, b = y, c = botón pulsado) */

static inline int ventana_crear(int ancho, int alto, const char *titulo) { return sys(10, (u32)ancho, (u32)alto, (u32)titulo); }
static inline void ventana_rect(int v, int x, int y, int w, int h, u32 color)
{ sys5(11, (u32)v, ((u32)x << 16) | ((u32)y & 0xFFFF), ((u32)w << 16) | ((u32)h & 0xFFFF), color, 0); }
static inline void ventana_texto(int v, int x, int y, const char *t, u32 color, int escala)
{ sys5(12, (u32)v, ((u32)x << 16) | ((u32)y & 0xFFFF), (u32)t, color, (u32)escala); }
static inline void ventana_mostrar(int v) { sys(13, (u32)v, 0, 0); }
/* Devuelve 1 si hay evento (en *e), 0 si no hay (sin esperar), -1 si la ventana ya no existe */
static inline int ventana_evento(int v, struct evento *e, int esperar) { return sys5(14, (u32)v, (u32)e, (u32)esperar, 0, 0); }

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
