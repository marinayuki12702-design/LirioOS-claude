/* Procesos y modo usuario de LirioOS.
 *
 *   - GDT propia con segmentos de kernel (anillo 0) y de usuario (anillo 3) y un TSS.
 *   - Paginación: el kernel se ve en la misma dirección en todos los espacios, y cada programa tiene
 *     su zona privada en 0x40000000-0x403FFFFF (nadie más la ve; el programa no puede tocar el kernel).
 *   - Las llamadas al sistema entran por int 0x80; las excepciones del procesador matan al programa
 *     si venían de modo usuario (y paran el kernel si fueron culpa suya).
 *   - Cargador de ELF: copia los segmentos del programa a páginas nuevas y salta al anillo 3.
 *
 * Esta primera versión ejecuta un programa a la vez hasta que termina (sin multitarea). */

#ifndef LIRIO_PROC_H
#define LIRIO_PROC_H

#include "elf.h"

#define USER_BASE          0x40000000u
#define USER_END           0x40400000u
#define USER_STACK_PAGES   16u
#define USER_STACK_BOTTOM  (USER_END - USER_STACK_PAGES * PAGE_SIZE)

/* Registros tal y como los dejan las rutinas de entrada de boot.asm */
struct exc_frame {
    u32 es, ds;
    u32 edi, esi, ebp, esp_d, ebx, edx, ecx, eax;
    u32 vec, err;
    u32 eip, cs, eflags, uesp, uss;                 /* uesp y uss solo si venía del anillo 3 */
};

static int  paging_ok;                              /* 1 = la paginación está activada */
u32 kernel_cr3;                                     /* lo usa user_return (boot.asm) */

struct proc {
    u32 pid;
    u32 pd, pt;                                     /* directorio y tabla de páginas del programa */
};
static struct proc *proc_cur;                       /* programa que se está ejecutando, o 0 */
static u32 next_pid = 1;
static u32 proc_last_pid;

static const char *proc_err;                        /* por qué no se pudo lanzar */
static int proc_faulted;                            /* el programa fue detenido por una excepción */
static u32 proc_fault_vec, proc_fault_addr, proc_fault_eip;

#ifdef LIRIO_HOST

/* En las pruebas en un PC normal no hay anillo 3: solo existe el lector de ELF */
static void gdt_init(void) { }
static void paging_init(void) { }
static u32 exc_table[32];
static void syscall_stub(void) { }
static int proc_exec(const char *name, const u8 *img, u32 len, int *code)
{
    (void)name; (void)img; (void)len; (void)code;
    proc_err = "sin modo usuario en este entorno";
    return 0;
}

#else

extern void gdt_flush(const void *p);
extern void tss_flush(void);
extern int  run_user(u32 entry, u32 esp, u32 cr3);
extern void user_return(int code) __attribute__((noreturn));
extern u32  exc_table[32];
extern void syscall_stub(void);

/* ---------- GDT y TSS ---------- */

struct gdt_entry {
    u16 limit_lo, base_lo;
    u8  base_mid, access, gran, base_hi;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static u32 tss[26];                                 /* TSS de 104 bytes; solo se usan esp0, ss0 y el mapa de E/S */
static u8 ring0_stack[16384] __attribute__((aligned(16)));

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 gran)
{
    gdt[i].limit_lo = (u16)(limit & 0xFFFF);
    gdt[i].base_lo = (u16)(base & 0xFFFF);
    gdt[i].base_mid = (u8)((base >> 16) & 0xFF);
    gdt[i].access = access;
    gdt[i].gran = (u8)(((limit >> 16) & 0x0F) | gran);
    gdt[i].base_hi = (u8)(base >> 24);
}

static void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC0);             /* 0x08: código del kernel */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);             /* 0x10: datos del kernel */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC0);             /* 0x18 (+3 = 0x1B): código de usuario */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC0);             /* 0x20 (+3 = 0x23): datos de usuario */
    gdt_set(5, (u32)tss, sizeof(tss) - 1, 0x89, 0x00);   /* 0x28: TSS */
    tss[1] = (u32)ring0_stack + sizeof(ring0_stack);        /* esp0 */
    tss[2] = 0x10;                                          /* ss0 */
    tss[25] = (u32)sizeof(tss) << 16;                       /* sin mapa de permisos de E/S */

    struct gdt_ptr p = { (u16)(sizeof(gdt) - 1), (u32)gdt };
    gdt_flush(&p);
    tss_flush();
}

/* ---------- Paginación ---------- */

static u32 kernel_pd[1024] __attribute__((aligned(4096)));

static void zero_page(u32 phys)
{
    volatile u32 *w = (volatile u32 *)phys;
    for (u32 i = 0; i < PAGE_SIZE / 4; i++) w[i] = 0;
}

static void paging_init(void)
{
    u32 a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(d & (1u << 3))) return;                   /* sin páginas de 4 MiB (PSE): sin modo usuario */

    /* Todo el espacio de 4 GiB se ve igual que la memoria física (solo el kernel puede tocarlo),
     * salvo 0x40000000-0x7FFFFFFF, que queda libre para los programas. */
    for (u32 i = 0; i < 1024; i++) kernel_pd[i] = (i << 22) | 0x83;
    for (u32 i = 256; i < 512; i++) kernel_pd[i] = 0;
    kernel_cr3 = (u32)kernel_pd;
    u32 t;
    __asm__ volatile ("mov %%cr4, %0\n\tor $0x10, %0\n\tmov %0, %%cr4\n\t"
                      "mov %1, %%cr3\n\t"
                      "mov %%cr0, %0\n\tor $0x80000000, %0\n\tmov %0, %%cr0"
                      : "=&r"(t) : "r"(kernel_cr3) : "memory");
    paging_ok = 1;
}

/* Memoria física de la página de usuario 'va' del programa, o 0 si no existe */
static u32 vm_phys(const struct proc *p, u32 va)
{
    if (va < USER_BASE || va >= USER_END) return 0;
    u32 e = ((volatile u32 *)p->pt)[(va - USER_BASE) >> 12];
    return (e & 1) ? (e & ~0xFFFu) : 0;
}

/* Se asegura de que la página que contiene 'va' exista (nueva y a cero si hace falta) */
static int vm_map(struct proc *p, u32 va)
{
    if (vm_phys(p, va)) return 1;
    u32 pg = pmm_alloc_page();
    if (!pg) return 0;
    zero_page(pg);
    ((u32 *)p->pt)[(va - USER_BASE) >> 12] = pg | 7;       /* presente, escritura, accesible en modo usuario */
    return 1;
}

static void vm_free(struct proc *p)
{
    if (p->pt) {
        for (u32 i = 0; i < 1024; i++) {
            u32 e = ((u32 *)p->pt)[i];
            if (e & 1) pmm_free_page(e & ~0xFFFu);
        }
        pmm_free_page(p->pt);
    }
    if (p->pd) pmm_free_page(p->pd);
    p->pd = p->pt = 0;
}

/* ¿Está todo [ptr, ptr+len) dentro de memoria del programa? */
static int user_range_ok(u32 ptr, u32 len)
{
    if (!proc_cur || ptr < USER_BASE || ptr >= USER_END || len > USER_END - ptr) return 0;
    for (u32 a = ptr & ~0xFFFu; a < ptr + len; a += PAGE_SIZE)
        if (!vm_phys(proc_cur, a)) return 0;
    return 1;
}

/* ---------- Excepciones y llamadas al sistema ---------- */

/* La llama exc_common (boot.asm) cuando el procesador detecta un fallo */
void exception_handler(struct exc_frame *f)
{
    u32 cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    if ((f->cs & 3) == 3 && proc_cur) {
        proc_faulted = 1;
        proc_fault_vec = f->vec;
        proc_fault_addr = cr2;
        proc_fault_eip = f->eip;
        user_return(-1);
    }
    con_print("\n*** ERROR DEL KERNEL: excepción ", ATTR_PROMPT);
    put_num(f->vec, ATTR_PROMPT);
    con_print(" en eip ", ATTR_PROMPT);
    put_hex32(f->eip, ATTR_PROMPT);
    con_print(" (cr2 ", ATTR_PROMPT);
    put_hex32(cr2, ATTR_PROMPT);
    con_print(")\nEl sistema se ha detenido.\n", ATTR_PROMPT);
    for (;;) __asm__ volatile ("cli; hlt");
}

/* int 0x80: eax = número de llamada, ebx y ecx = argumentos; el resultado vuelve en eax.
 *   1 salir(código)   2 escribir(texto, largo)   3 ticks()   4 dormir(ticks)   5 pid() */
void syscall_handler(struct exc_frame *f)
{
    switch (f->eax) {
    case 1:
        user_return((int)f->ebx);
    case 2: {
        u32 p = f->ebx, n = f->ecx;
        char buf[256];
        if (n > 255) n = 255;
        if (n && !user_range_ok(p, n)) { f->eax = (u32)-1; break; }
        for (u32 i = 0; i < n; i++) buf[i] = ((const char *)p)[i];
        buf[n] = 0;
        con_print(buf, ATTR_NORMAL);
        f->eax = n;
        break;
    }
    case 3:
        f->eax = ticks;
        break;
    case 4: {
        u32 t0 = ticks, n = f->ebx;
        if (n > 6000) n = 6000;
        irq_on();
        while (ticks - t0 < n) __asm__ volatile ("hlt");
        irq_off();
        f->eax = 0;
        break;
    }
    case 5:
        f->eax = proc_cur ? proc_cur->pid : 0;
        break;
    default:
        f->eax = (u32)-1;
    }
}

/* ---------- Lanzar un programa ---------- */

/* Carga el ELF 'img' en un espacio nuevo y lo ejecuta hasta que termine o falle.
 * Devuelve 1 si llegó a ejecutarse (código de salida en *code; mira proc_faulted), o 0 si no pudo
 * (el motivo queda en proc_err). */
static int proc_exec(const char *name, const u8 *img, u32 len, int *code)
{
    struct proc p;
    struct elf_seg seg[ELF_MAX_SEGS];
    u32 entry, n;
    (void)name;
    p.pid = 0; p.pd = 0; p.pt = 0;
    proc_faulted = 0;

    if (!paging_ok) { proc_err = "este procesador no tiene paginación de 4 MiB"; return 0; }
    int e = elf_parse(img, len, USER_BASE, USER_STACK_BOTTOM, &entry, seg, ELF_MAX_SEGS, &n);
    if (e) { proc_err = elf_err_text(e); return 0; }

    p.pd = pmm_alloc_page();
    p.pt = pmm_alloc_page();
    if (!p.pd || !p.pt) { vm_free(&p); proc_err = "no queda memoria"; return 0; }
    zero_page(p.pd);
    zero_page(p.pt);
    for (u32 i = 0; i < 1024; i++)                  /* el kernel se ve igual en todos los espacios */
        if (i < 256 || i >= 512) ((u32 *)p.pd)[i] = kernel_pd[i];
    ((u32 *)p.pd)[256] = p.pt | 7;                  /* 0x40000000-0x403FFFFF: zona del programa */

    for (u32 s = 0; s < n; s++) {
        for (u32 a = seg[s].vaddr & ~0xFFFu; a < seg[s].vaddr + seg[s].memsz; a += PAGE_SIZE)
            if (!vm_map(&p, a)) { vm_free(&p); proc_err = "no queda memoria"; return 0; }
        for (u32 k = 0; k < seg[s].filesz; k++) {
            u32 va = seg[s].vaddr + k;
            ((u8 *)(vm_phys(&p, va) + (va & 0xFFF)))[0] = img[seg[s].off + k];
        }
    }
    for (u32 a = USER_STACK_BOTTOM; a < USER_END; a += PAGE_SIZE)
        if (!vm_map(&p, a)) { vm_free(&p); proc_err = "no queda memoria"; return 0; }

    p.pid = next_pid++;
    proc_last_pid = p.pid;
    proc_cur = &p;
    *code = run_user(entry, USER_END - 16, p.pd);
    proc_cur = 0;
    vm_free(&p);
    return 1;
}

#endif /* LIRIO_HOST */

/* Explica por qué se detuvo el programa */
static void proc_fault_print(void)
{
    switch (proc_fault_vec) {
    case 0:  con_print("división entre cero", ATTR_NORMAL); break;
    case 6:  con_print("instrucción no válida", ATTR_NORMAL); break;
    case 13: con_print("violación de protección", ATTR_NORMAL); break;
    case 14:
        con_print("acceso no permitido a la memoria (dirección ", ATTR_NORMAL);
        put_hex32(proc_fault_addr, ATTR_NORMAL);
        con_print(")", ATTR_NORMAL);
        break;
    default:
        con_print("excepción ", ATTR_NORMAL);
        put_num(proc_fault_vec, ATTR_NORMAL);
    }
    con_print(" en eip ", ATTR_NORMAL);
    put_hex32(proc_fault_eip, ATTR_NORMAL);
}

#endif
