/* Procesos y modo usuario de LirioOS.
 *
 *   - GDT propia con segmentos de kernel (anillo 0) y de usuario (anillo 3) y un TSS.
 *   - Paginación: el kernel se ve en la misma dirección en todos los espacios, y cada programa tiene
 *     su zona privada en 0x40000000-0x403FFFFF (nadie más la ve; el programa no puede tocar el kernel).
 *   - Las llamadas al sistema entran por int 0x80; las excepciones del procesador matan al programa
 *     si venían de modo usuario (y paran el kernel si fueron culpa suya).
 *   - Cargador de ELF: copia los segmentos del programa a páginas nuevas y salta al anillo 3.
 *
 *   - Multitarea: varios programas a la vez. El temporizador (100 veces por segundo) reparte el procesador
 *     entre ellos por turnos (round robin); el propio kernel con su escritorio es el proceso 0.
 *     Mientras un proceso está dentro del kernel (llamada al sistema) no se le quita el procesador, salvo
 *     cuando se duerme: así los datos del kernel nunca se corrompen a medias. */

#ifndef LIRIO_PROC_H
#define LIRIO_PROC_H

#include "elf.h"

typedef short s16;

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

#define MAX_TASKS 8
#define KSTACK_PAGES 2u

enum { T_FREE = 0, T_READY, T_SLEEP, T_ZOMBIE };

struct task {
    u32 state;
    u32 pid;
    u32 esp;                                        /* esp de la pila de kernel mientras no se ejecuta */
    u32 kstack;                                     /* base de su pila de kernel (2 páginas) */
    u32 pd, pt;                                     /* directorio y tabla de páginas del programa */
    u32 wake;                                       /* si duerme: tic en que despierta */
    int code;                                       /* código de salida */
    int faulted;                                    /* lo detuvo una excepción */
    int killed;                                     /* lo detuvo el usuario con 'matar' */
    u32 fvec, faddr, feip;
    u32 start;                                      /* tic en que empezó */
    u32 brk;                                        /* fin de su montón (sbrk) */
    u32 keyq[16], kq_h, kq_n;                       /* teclas pulsadas que aún no ha leído */
    char name[16];
};
static struct task tasks[MAX_TASKS];                /* tasks[0] es el kernel (escritorio y terminal) */
static u32 cur_t;                                   /* índice del proceso que se ejecuta */
static struct task *proc_cur;                       /* el proceso de usuario actual, o 0 si es el kernel */
static u32 next_pid = 1;
static u32 kbd_focus;                               /* programa que recibe el teclado (0 = el terminal) */

static const char *proc_err;                        /* por qué no se pudo lanzar */

#ifdef LIRIO_HOST

/* En las pruebas en un PC normal no hay anillo 3: solo existe el lector de ELF */
static void gdt_init(void) { }
static void paging_init(void) { }
static u32 exc_table[32];
static void syscall_stub(void) { }
void timer_irq(void) { ticks++; outb(0x20, 0x20); }
static u32 proc_spawn(const char *name, const u8 *img, u32 len)
{
    (void)name; (void)img; (void)len;
    proc_err = "sin modo usuario en este entorno";
    return 0;
}
static int proc_key(u32 c) { (void)c; return 0; }
static void sched_idle(void) { }
static void sched_reap(void) { }
static int proc_kill(u32 pid) { (void)pid; return 0; }
static void proc_list(void) { }

#else

extern void gdt_flush(const void *p);
extern void tss_flush(void);
extern void switch_to(u32 *save_esp, u32 new_esp, u32 new_cr3);
extern void task_start(void);
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
    tasks[0].state = T_READY;
    tasks[0].name[0] = 'k'; tasks[0].name[1] = 0;
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
static u32 vm_phys(const struct task *p, u32 va)
{
    if (va < USER_BASE || va >= USER_END) return 0;
    u32 e = ((volatile u32 *)p->pt)[(va - USER_BASE) >> 12];
    return (e & 1) ? (e & ~0xFFFu) : 0;
}

/* Se asegura de que la página que contiene 'va' exista (nueva y a cero si hace falta) */
static int vm_map(struct task *p, u32 va)
{
    if (vm_phys(p, va)) return 1;
    u32 pg = pmm_alloc_page();
    if (!pg) return 0;
    zero_page(pg);
    ((u32 *)p->pt)[(va - USER_BASE) >> 12] = pg | 7;       /* presente, escritura, accesible en modo usuario */
    return 1;
}

static void vm_free(struct task *p)
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

static void proc_fault_print(const struct task *t);

/* ---------- Planificador ---------- */

static void sched_switch_to(u32 next)
{
    u32 prev = cur_t;
    cur_t = next;
    proc_cur = next ? &tasks[next] : 0;
    if (next) tss[1] = tasks[next].kstack + KSTACK_PAGES * PAGE_SIZE;     /* pila que usará al volver del anillo 3 */
    switch_to(&tasks[prev].esp, tasks[next].esp, next ? tasks[next].pd : kernel_cr3);
}

/* Elige el siguiente proceso listo (por turnos) y cambia a él. Siempre con las interrupciones desactivadas. */
static void schedule(void)
{
    for (u32 i = 1; i <= MAX_TASKS; i++) {
        u32 n = (cur_t + i) % MAX_TASKS;
        struct task *t = &tasks[n];
        if (t->state == T_SLEEP && (s32)(ticks - t->wake) >= 0) t->state = T_READY;
        if (t->state == T_READY && n != cur_t) { sched_switch_to(n); return; }
    }
}

/* La llama irq0_stub (boot.asm) 100 veces por segundo */
struct irq_frame { u32 es, ds, edi, esi, ebp, esp_d, ebx, edx, ecx, eax, eip, cs, eflags; };

void timer_irq(struct irq_frame *f)
{
    ticks++;
    outb(0x20, 0x20);                               /* fin de interrupción para el PIC */
    if ((f->cs & 3) == 3) schedule();               /* solo se quita el procesador a quien está en modo usuario */
}

/* Para el kernel (proceso 0), cuando no tiene nada que hacer: si hay programas listos les deja su turno;
 * si no, duerme hasta la siguiente interrupción. */
static void sched_idle(void)
{
    int ready = 0;
    for (u32 i = 1; i < MAX_TASKS; i++)
        if (tasks[i].state == T_READY || (tasks[i].state == T_SLEEP && (s32)(ticks - tasks[i].wake) >= 0)) ready = 1;
    if (!ready) { __asm__ volatile ("hlt"); return; }
    irq_off();
    schedule();
    irq_on();
}

static void task_free(struct task *t)
{
    vm_free(t);
    if (t->kstack) { for (u32 i = 0; i < KSTACK_PAGES; i++) pmm_free_page(t->kstack + i * PAGE_SIZE); }
    t->kstack = 0;
    t->state = T_FREE;
}

/* Recoge los procesos terminados: libera su memoria y avisa en el terminal. Solo la llama el kernel (proceso 0). */
static void sched_reap(void)
{
    int changed = 0;
    for (int w = 0; w < UW_MAX; w++)                /* ventanas cerradas con el botón: se detiene a su programa */
        if (uw[w].used && uw[w].want_close) {
            struct task *o = &tasks[uw[w].owner];
            if (o->state == T_READY || o->state == T_SLEEP) { o->killed = 1; o->state = T_ZOMBIE; if (kbd_focus == uw[w].owner) kbd_focus = 0; }
            uw_destroy(w);
            changed = 1;
        }
    for (u32 i = 1; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state != T_ZOMBIE) continue;
        for (int w = 0; w < UW_MAX; w++)
            if (uw[w].used && uw[w].owner == i) { uw_destroy(w); changed = 1; }
        if (changed) { uw_refresh(); changed = 0; }
        con_print("[pid ", ATTR_SOFT); put_num(t->pid, ATTR_SOFT);
        if (t->killed) {
            con_print("] detenido por el usuario\n", ATTR_SOFT);
        } else if (t->faulted) {
            con_print("] detenido por el sistema: ", ATTR_SOFT);
            proc_fault_print(t);
            con_putc('\n', ATTR_NORMAL);
        } else {
            con_print("] terminó con código ", ATTR_SOFT); put_num((u32)t->code, ATTR_SOFT);
            con_putc('\n', ATTR_NORMAL);
        }
        task_free(t);
    }
    if (changed) uw_refresh();
}

/* Quita un proceso del medio: 'matar'. Nunca es el que se está ejecutando (lo llama el kernel). */
static int proc_kill(u32 pid)
{
    for (u32 i = 1; i < MAX_TASKS; i++)
        if (tasks[i].state != T_FREE && tasks[i].state != T_ZOMBIE && tasks[i].pid == pid) {
            tasks[i].killed = 1;
            irq_off();
            tasks[i].state = T_ZOMBIE;
            if (kbd_focus == i) kbd_focus = 0;
            irq_on();
            return 1;
        }
    return 0;
}

/* El terminal le pasa cada tecla; devuelve 1 si un programa se la queda. Esc detiene al programa. */
static int proc_key(u32 c)
{
    int top = top_visible();
    if (top >= 2) {                                 /* la ventana de un programa está delante: las teclas son suyas */
        uw_event(top - 2, 1, c, 0, 0);
        return 1;
    }
    if (!kbd_focus) return 0;
    struct task *t = &tasks[kbd_focus];
    if (t->state != T_READY && t->state != T_SLEEP) { kbd_focus = 0; return 0; }
    if (c == 27) { proc_kill(t->pid); return 1; }
    if (c < 0x100 && t->kq_n < 16) { t->keyq[(t->kq_h + t->kq_n) % 16] = c; t->kq_n++; }
    return 1;
}

static void proc_list(void)
{
    int any = 0;
    con_print("  PID  estado     tiempo  programa\n", ATTR_SOFT);
    for (u32 i = 1; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state == T_FREE) continue;
        any = 1;
        con_print("  ", ATTR_NORMAL);
        if (t->pid < 10) con_putc(' ', ATTR_NORMAL);
        if (t->pid < 100) con_putc(' ', ATTR_NORMAL);
        put_num(t->pid, ATTR_NORMAL);
        con_print(t->state == T_READY ? "  activo     " : t->state == T_SLEEP ? "  dormido    " : "  terminando ", ATTR_NORMAL);
        put_num((ticks - t->start) / 100, ATTR_NORMAL);
        con_print(" s  ", ATTR_NORMAL);
        con_print(t->name, ATTR_NORMAL);
        con_putc('\n', ATTR_NORMAL);
    }
    if (!any) con_print("  (no hay programas en ejecución)\n", ATTR_SOFT);
}

/* El proceso actual deja de existir (salió o falló). No vuelve nunca. */
static void task_die(void)
{
    proc_cur->state = T_ZOMBIE;
    if (kbd_focus == cur_t) kbd_focus = 0;
    schedule();
    for (;;) __asm__ volatile ("hlt");
}

/* ---------- Excepciones y llamadas al sistema ---------- */

/* La llama exc_common (boot.asm) cuando el procesador detecta un fallo */
void exception_handler(struct exc_frame *f)
{
    u32 cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    if ((f->cs & 3) == 3 && proc_cur) {
        proc_cur->faulted = 1;
        proc_cur->fvec = f->vec;
        proc_cur->faddr = cr2;
        proc_cur->feip = f->eip;
        task_die();
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

/* Copia un texto terminado en 0 desde memoria del programa (hasta max-1 bytes); 0 si no es válido */
static int user_cstr(u32 p, char *out, u32 max)
{
    for (u32 i = 0; i + 1 < max; i++) {
        if (!user_range_ok(p + i, 1)) return 0;
        out[i] = ((const char *)p)[i];
        if (!out[i]) return 1;
    }
    return 0;
}

#define FILE_MAX 65536u

static int sys_leer_archivo(u32 upath, u32 ubuf, u32 max)
{
    char name[PATH_MAX], full[PATH_MAX];
    if (max > FILE_MAX || !user_cstr(upath, name, sizeof(name)) || !path_resolve(name, full)) return -1;
    if (max && !user_range_ok(ubuf, max)) return -1;
    u32 len = 0;
    if (is_disk(full)) {
        u8 *tmp = (u8 *)kmalloc(max ? max : 1);
        if (!tmp) return -1;
        if (!fat_read_file(disk_rel(full), tmp, max, &len)) { kfree(tmp); return -1; }
        for (u32 i = 0; i < len; i++) ((u8 *)ubuf)[i] = tmp[i];
        kfree(tmp);
    } else {
        const struct ramfs_file *r = fs_file(full);
        if (!r || r->img) return -1;
        len = slen(r->data);
        if (len > max) len = max;
        for (u32 i = 0; i < len; i++) ((u8 *)ubuf)[i] = (u8)r->data[i];
    }
    return (int)len;
}

static int sys_escribir_archivo(u32 upath, u32 udata, u32 len)
{
    char name[PATH_MAX], full[PATH_MAX];
    if (len > FILE_MAX || !user_cstr(upath, name, sizeof(name)) || !path_resolve(name, full)) return -1;
    if (len && !user_range_ok(udata, len)) return -1;
    u8 *tmp = (u8 *)kmalloc(len + 1);
    if (!tmp) return -1;
    for (u32 i = 0; i < len; i++) tmp[i] = ((const u8 *)udata)[i];
    tmp[len] = 0;
    int ok;
    if (is_disk(full)) {
        ok = fat_ready && fat_write_file(disk_rel(full), tmp, len);
        disk_forget();
    } else {
        ok = fs_write(full, (const char *)tmp);
    }
    kfree(tmp);
    return ok ? 0 : -1;
}

/* ¿Es 'id' una ventana abierta por el programa actual? */
static int uw_mine(u32 id)
{
    return id < UW_MAX && uw[id].used && uw[id].owner == cur_t;
}

/* int 0x80: eax = número de llamada, ebx y ecx = argumentos; el resultado vuelve en eax.
 *   1 salir(código)   2 escribir(texto, largo)   3 ticks()   4 dormir(ticks)   5 pid()
 *   10 ventana_crear  11 ventana_rect  12 ventana_texto  13 ventana_mostrar  14 ventana_evento (ventanas gráficas)
 *   6 leer_tecla()   7 leer_archivo(ruta, buf, max)   8 escribir_archivo(ruta, datos, largo)   9 sbrk(bytes) */
void syscall_handler(struct exc_frame *f)
{
    switch (f->eax) {
    case 1:
        proc_cur->code = (int)f->ebx;
        task_die();
        break;
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
        u32 n = f->ebx;
        if (n > 6000) n = 6000;
        proc_cur->wake = ticks + n;
        proc_cur->state = T_SLEEP;
        schedule();                                 /* vuelve cuando se cumple el tiempo */
        f->eax = 0;
        break;
    }
    case 5:
        f->eax = proc_cur->pid;
        break;
    case 6: {                                       /* leer_tecla: espera a que se pulse una */
        kbd_focus = cur_t;
        while (proc_cur->kq_n == 0) {
            proc_cur->wake = ticks + 1;
            proc_cur->state = T_SLEEP;
            schedule();
        }
        f->eax = proc_cur->keyq[proc_cur->kq_h];
        proc_cur->kq_h = (proc_cur->kq_h + 1) % 16;
        proc_cur->kq_n--;
        break;
    }
    case 7: f->eax = (u32)sys_leer_archivo(f->ebx, f->ecx, f->edx); break;
    case 8: f->eax = (u32)sys_escribir_archivo(f->ebx, f->ecx, f->edx); break;
    case 9: {                                       /* sbrk: amplía el montón del programa */
        u32 old = proc_cur->brk, nw = old + f->ebx;
        if (nw < old || nw > USER_STACK_BOTTOM) { f->eax = (u32)-1; break; }
        for (u32 a = old & ~0xFFFu; a < nw; a += PAGE_SIZE)
            if (!vm_map(proc_cur, a)) { f->eax = (u32)-1; goto done; }
        proc_cur->brk = nw;
        f->eax = old;
        break;
    }
    case 10: {                                      /* ventana_crear(ancho, alto, titulo) */
        char title[28];
        if (!user_cstr(f->edx, title, sizeof(title))) { f->eax = (u32)-1; break; }
        f->eax = (u32)uw_create((s32)f->ebx, (s32)f->ecx, title, cur_t);
        if ((int)f->eax >= 0) uw_refresh();
        break;
    }
    case 11:                                        /* ventana_rect(id, x<<16|y, ancho<<16|alto, color) */
        if (!uw_mine(f->ebx)) { f->eax = (u32)-1; break; }
        uw_rect((int)f->ebx, (s16)(f->ecx >> 16), (s16)(f->ecx & 0xFFFF), (s16)(f->edx >> 16), (s16)(f->edx & 0xFFFF), f->esi);
        f->eax = 0;
        break;
    case 12: {                                      /* ventana_texto(id, x<<16|y, texto, color, escala) */
        char txt[128];
        if (!uw_mine(f->ebx) || !user_cstr(f->edx, txt, sizeof(txt))) { f->eax = (u32)-1; break; }
        uw_text((int)f->ebx, (s16)(f->ecx >> 16), (s16)(f->ecx & 0xFFFF), txt, f->esi, f->edi == 2 ? 2 : 1);
        f->eax = 0;
        break;
    }
    case 13:                                        /* ventana_mostrar(id): vuelve a dibujar el escritorio */
        if (!uw_mine(f->ebx)) { f->eax = (u32)-1; break; }
        uw_refresh();
        f->eax = 0;
        break;
    case 14: {                                      /* ventana_evento(id, buf[4], esperar) */
        int id = (int)f->ebx;
        if (!uw_mine(f->ebx) || !user_range_ok(f->ecx, 16)) { f->eax = (u32)-1; break; }
        for (;;) {
            if (uw_pop(id, (u32 *)f->ecx)) { f->eax = 1; break; }
            if (!f->edx) { f->eax = 0; break; }
            proc_cur->wake = ticks + 1;
            proc_cur->state = T_SLEEP;
            schedule();
            if (!uw_mine((u32)id)) { f->eax = (u32)-1; break; }
        }
        break;
    }
    default:
        f->eax = (u32)-1;
    }
done:;
}

/* ---------- Lanzar un programa ---------- */

/* Carga el ELF 'img' en un espacio nuevo y lo deja listo para ejecutarse en segundo plano.
 * Devuelve su pid, o 0 si no pudo (el motivo queda en proc_err). */
static u32 proc_spawn(const char *name, const u8 *img, u32 len)
{
    struct elf_seg seg[ELF_MAX_SEGS];
    u32 entry, n, slot = 0;

    if (!paging_ok) { proc_err = "este procesador no tiene paginación de 4 MiB"; return 0; }
    int e = elf_parse(img, len, USER_BASE, USER_STACK_BOTTOM, &entry, seg, ELF_MAX_SEGS, &n);
    if (e) { proc_err = elf_err_text(e); return 0; }
    for (u32 i = 1; i < MAX_TASKS; i++) if (tasks[i].state == T_FREE) { slot = i; break; }
    if (!slot) { proc_err = "demasiados programas a la vez (usa 'ps' y 'matar')"; return 0; }

    struct task *p = &tasks[slot];
    p->pd = p->pt = p->kstack = 0;
    p->pt = pmm_alloc_page();
    p->pd = pmm_alloc_page();
    p->kstack = pmm_alloc_run(KSTACK_PAGES);
    if (!p->pd || !p->pt || !p->kstack) goto nomem;
    zero_page(p->pd);
    zero_page(p->pt);
    for (u32 i = 0; i < 1024; i++)                  /* el kernel se ve igual en todos los espacios */
        if (i < 256 || i >= 512) ((u32 *)p->pd)[i] = kernel_pd[i];
    ((u32 *)p->pd)[256] = p->pt | 7;                /* 0x40000000-0x403FFFFF: zona del programa */

    for (u32 s = 0; s < n; s++) {
        for (u32 a = seg[s].vaddr & ~0xFFFu; a < seg[s].vaddr + seg[s].memsz; a += PAGE_SIZE)
            if (!vm_map(p, a)) goto nomem;
        for (u32 k = 0; k < seg[s].filesz; k++) {
            u32 va = seg[s].vaddr + k;
            ((u8 *)(vm_phys(p, va) + (va & 0xFFF)))[0] = img[seg[s].off + k];
        }
    }
    for (u32 a = USER_STACK_BOTTOM; a < USER_END; a += PAGE_SIZE)
        if (!vm_map(p, a)) goto nomem;
    p->brk = 0;
    for (u32 s = 0; s < n; s++)
        if (seg[s].vaddr + seg[s].memsz > p->brk) p->brk = seg[s].vaddr + seg[s].memsz;
    p->kq_h = p->kq_n = 0;

    {   /* pila de kernel inicial: switch_to "vuelve" a task_start, que salta al anillo 3 */
        u32 *sp = (u32 *)(p->kstack + KSTACK_PAGES * PAGE_SIZE);
        *--sp = USER_END - 16;                      /* esp de usuario */
        *--sp = entry;                              /* eip de usuario */
        *--sp = (u32)task_start;
        *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; /* ebp, ebx, esi, edi */
        p->esp = (u32)sp;
    }
    p->pid = next_pid++;
    p->code = 0; p->faulted = 0; p->killed = 0;
    p->start = ticks;
    {
        u32 i = 0;
        while (name[i] && name[i] != '.' && i < sizeof(p->name) - 1) { p->name[i] = name[i]; i++; }
        p->name[i] = 0;
    }
    irq_off();
    p->state = T_READY;
    irq_on();
    return p->pid;

nomem:
    task_free(p);
    proc_err = "no queda memoria";
    return 0;
}

#endif /* LIRIO_HOST */

/* Explica por qué se detuvo el programa */
static void proc_fault_print(const struct task *t)
{
    switch (t->fvec) {
    case 0:  con_print("división entre cero", ATTR_NORMAL); break;
    case 6:  con_print("instrucción no válida", ATTR_NORMAL); break;
    case 13: con_print("violación de protección", ATTR_NORMAL); break;
    case 14:
        con_print("acceso no permitido a la memoria (dirección ", ATTR_NORMAL);
        put_hex32(t->faddr, ATTR_NORMAL);
        con_print(")", ATTR_NORMAL);
        break;
    default:
        con_print("excepción ", ATTR_NORMAL);
        put_num(t->fvec, ATTR_NORMAL);
    }
    con_print(" en eip ", ATTR_NORMAL);
    put_hex32(t->feip, ATTR_NORMAL);
}

#endif
