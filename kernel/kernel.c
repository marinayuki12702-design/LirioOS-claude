/* Kernel de LirioOS: modo gráfico, teclado español, sistema de archivos en memoria y terminal en ventana. */

#include "gui.h"
#include "progs.h"

/* ---------- Teclado español (consulta directa, juego de códigos 1) ---------- */

#define DEAD_ACUTE  0x81
#define DEAD_DIAER  0x82
#define DEAD_GRAVE  0x83
#define DEAD_CIRC   0x84

#define KEYS 0x58
#define KEY_UP   0x100
#define KEY_DOWN 0x101

/* Sin Shift. Las letras van en minúscula; Shift y Bloq Mayús las convierten. */
static const u8 km_norm[KEYS] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '\'', [0x0D] = 0xA1 /* ¡ */, [0x0E] = '\b', [0x0F] = ' ',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = DEAD_GRAVE, [0x1B] = '+', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = 0xF1 /* ñ */,
    [0x28] = DEAD_ACUTE, [0x29] = 0xBA /* º */, [0x2B] = 0xE7 /* ç */,
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '-',
    [0x39] = ' ', [0x56] = '<',
};

/* Con Shift (solo las teclas que no son letras) */
static const u8 km_shift[KEYS] = {
    [0x02] = '!', [0x03] = '"', [0x04] = 0xB7 /* · */, [0x05] = '$', [0x06] = '%',
    [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0A] = ')', [0x0B] = '=',
    [0x0C] = '?', [0x0D] = 0xBF /* ¿ */, [0x0E] = '\b', [0x0F] = ' ',
    [0x1A] = DEAD_CIRC, [0x1B] = '*', [0x1C] = '\n',
    [0x28] = DEAD_DIAER, [0x29] = 0xAA /* ª */,
    [0x33] = ';', [0x34] = ':', [0x35] = '_', [0x39] = ' ', [0x56] = '>',
};

/* Con AltGr (tecla Alt derecha) */
static const u8 km_altgr[KEYS] = {
    [0x02] = '|', [0x03] = '@', [0x04] = '#', [0x05] = '~', [0x07] = 0xAC /* ¬ */,
    [0x1A] = '[', [0x1B] = ']', [0x28] = '{', [0x29] = '\\', [0x2B] = '}',
    [0x0E] = '\b', [0x1C] = '\n', [0x39] = ' ', [0x56] = '|',
};

static int is_letter(u8 c)
{
    return (c >= 'a' && c <= 'z') || c == 0xF1 || c == 0xE7;
}

static u8 to_upper(u8 c)
{
    if (c >= 'a' && c <= 'z') return (u8)(c - 32);
    if (c == 0xF1) return 0xD1;   /* Ñ */
    if (c == 0xE7) return 0xC7;   /* Ç */
    return c;
}

/* Combina una tecla muerta (acento) con la letra siguiente */
static u8 accent(u8 dead, u8 c)
{
    static const char base[] = "aeiouAEIOU";
    static const u8 acute[] = { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA };
    static const u8 diaer[] = { 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC };
    static const u8 grave[] = { 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 };
    static const u8 circ[]  = { 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB };
    const u8 *t = dead == DEAD_ACUTE ? acute : dead == DEAD_DIAER ? diaer : dead == DEAD_GRAVE ? grave : circ;

    for (int i = 0; i < 10; i++)
        if ((u8)base[i] == c) return t[i];
    if (c == ' ')
        return dead == DEAD_ACUTE ? 0xB4 : dead == DEAD_DIAER ? 0xA8 : dead == DEAD_GRAVE ? '`' : '^';
    return c;
}

/* Procesa un byte del teclado y devuelve su carácter en Latin-1 (0 si no produce ninguno) */
static u32 kbd_byte(u8 sc)
{
    static u8 shift, altgr, caps, ext, dead;

    if (sc == 0xE0) { ext = 1; return 0; }
    u8 released = sc & 0x80;
    u8 code = sc & 0x7F;

    if (ext) {                           /* teclas extendidas: AltGr y las flechas arriba/abajo */
        ext = 0;
        if (code == 0x38) altgr = !released;
        else if (!released && code == 0x48) return KEY_UP;
        else if (!released && code == 0x50) return KEY_DOWN;
        return 0;
    }
    if (code == 0x2A || code == 0x36) { shift = !released; return 0; }
    if (released) return 0;
    if (code == 0x3A) { caps ^= 1; return 0; }
    if (code == 0x01) return 27;         /* Esc */
    if (code >= KEYS) return 0;

    u8 c;
    if (altgr) {
        c = km_altgr[code];
    } else {
        u8 base = km_norm[code];
        if (is_letter(base)) c = (caps ^ shift) ? to_upper(base) : base;
        else                 c = shift ? km_shift[code] : base;
    }
    if (!c) return 0;

    if (c >= DEAD_ACUTE && c <= DEAD_CIRC) { dead = c; return 0; }
    if (dead) { c = accent(dead, c); dead = 0; }
    return c;
}

/* ---------- Ratón PS/2 (consulta directa) ---------- */

static void kbc_wait_write(void)
{
    for (u32 i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2)) return;
}

static int kbc_wait_read(void)
{
    for (u32 i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return 1;
    return 0;
}

static void mouse_cmd(u8 v)
{
    kbc_wait_write();
    outb(0x64, 0xD4);                    /* el siguiente byte va al ratón */
    kbc_wait_write();
    outb(0x60, v);
    if (kbc_wait_read()) inb(0x60);      /* confirmación (0xFA) */
}

static void mouse_init(void)
{
    while (inb(0x64) & 1) inb(0x60);     /* vacía lo que hubiera pendiente */
    kbc_wait_write();
    outb(0x64, 0xA8);                    /* activa el puerto del ratón */
    kbc_wait_write();
    outb(0x64, 0x20);                    /* lee el byte de configuración */
    u8 cfg = kbc_wait_read() ? inb(0x60) : 0;
    cfg = (u8)((cfg | 3) & ~0x20);       /* interrupciones del teclado y del ratón, y reloj del ratón activo */
    kbc_wait_write();
    outb(0x64, 0x60);
    kbc_wait_write();
    outb(0x60, cfg);
    mouse_cmd(0xF6);                     /* valores por defecto */
    mouse_cmd(0xF4);                     /* empieza a enviar datos */
}

static u8 mouse_pkt[3];
static u8 mouse_idx;
static u8 mouse_btn;

/* Junta los 3 bytes de un paquete; devuelve 1 cuando hay un movimiento completo */
static int mouse_feed(u8 d, s32 *dx, s32 *dy, u8 *btn)
{
    if (mouse_idx == 0 && !(d & 8)) return 0;      /* byte fuera de sitio: se descarta */
    mouse_pkt[mouse_idx++] = d;
    if (mouse_idx < 3) return 0;
    mouse_idx = 0;
    if (mouse_pkt[0] & 0xC0) return 0;             /* desbordamiento */
    *dx = (s32)mouse_pkt[1] - (((s32)mouse_pkt[0] << 4) & 0x100);
    *dy = (s32)mouse_pkt[2] - (((s32)mouse_pkt[0] << 3) & 0x100);
    *btn = mouse_pkt[0] & 7;
    return 1;
}

/* Atiende al teclado y al ratón; devuelve una tecla (Latin-1) o 0 si no hubo ninguna */
/* ---------- Cola de bytes del teclado y el ratón (la llenan las interrupciones) ---------- */

#define IQ_SIZE 256
static volatile u16 iq[IQ_SIZE];          /* byte + 0x100 si viene del ratón */
static volatile u32 iq_head, iq_tail;      /* head lo mueve quien guarda, tail quien lee */

static inline void irq_off(void) { __asm__ volatile ("cli"); }
static inline void irq_on(void)  { __asm__ volatile ("sti"); }

static void iq_push(u16 v)
{
    u32 n = (iq_head + 1) % IQ_SIZE;
    if (n == iq_tail) return;              /* cola llena: se pierde el byte */
    iq[iq_head] = v;
    iq_head = n;
}

/* IRQ 1: llegó una tecla */
void kbd_irq(void)
{
    /* Solo se lee si el controlador tiene de verdad un byte del teclado: si 'input_poll' ya se lo llevó,
     * leer otra vez devolvería el mismo byte y la tecla saldría repetida. */
    if ((inb(0x64) & 0x21) == 0x01) iq_push(inb(0x60));
    outb(0x20, 0x20);
}

/* IRQ 12: llegó un byte del ratón (hay que avisar al PIC esclavo y al maestro) */
void mouse_irq(void)
{
    if ((inb(0x64) & 0x21) == 0x21) iq_push((u16)(inb(0x60) | 0x100));
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static u32 input_poll(void)
{
    u32 key = 0;
    s32 adx = 0, ady = 0;
    int pending = 0;
    u8 s;

    /* Red de seguridad: si el controlador tiene algo que ninguna interrupción recogió, se recoge aquí */
    irq_off();
    while ((s = inb(0x64)) & 1) {
        u8 d = inb(0x60);
        iq_push((u16)(d | ((s & 0x20) ? 0x100 : 0)));
    }
    irq_on();

    while (iq_tail != iq_head && !key) {
        u16 e = iq[iq_tail];
        iq_tail = (iq_tail + 1) % IQ_SIZE;
        u8 d = (u8)(e & 0xFF);
        if (e & 0x100) {                             /* byte del ratón */
            s32 dx, dy;
            u8 btn;
            if (mouse_feed(d, &dx, &dy, &btn)) {
                if (btn != mouse_btn && pending) {
                    gui_mouse(adx, ady, mouse_btn);
                    adx = ady = 0;
                    pending = 0;
                }
                mouse_btn = btn;
                adx += dx;
                ady -= dy;                           /* en el ratón, arriba es positivo */
                pending = 1;
            }
        } else {
            key = kbd_byte(d);
        }
    }
    if (pending) gui_mouse(adx, ady, mouse_btn);
    return key;
}

/* ---------- Texto en pantalla ---------- */

/* El texto tecleado ya está en Latin-1: se escribe byte a byte */
static void put_latin1(const char *s, u8 attr)
{
    while (*s) con_putc((u8)*s++, attr);
}

static void put_num(u32 n, u8 attr)
{
    char buf[12];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) con_putc((u8)buf[--i], attr);
}

static void put_num2(u32 n, u8 attr)           /* siempre con dos cifras: 05 */
{
    con_putc((u8)('0' + (n / 10) % 10), attr);
    con_putc((u8)('0' + n % 10), attr);
}

/* ---------- Carpeta actual del terminal (el sistema de archivos está en fs.h) ---------- */

static char cwd[PATH_MAX];                     /* carpeta actual; vacío = raíz */

/* Convierte 'arg' (relativo a la carpeta actual o absoluto) en un camino limpio, sin barras
 * sobrantes y con los "." y ".." resueltos. Devuelve 0 si es demasiado largo. */
static int path_resolve(const char *arg, char *out)
{
    char full[2 * PATH_MAX];
    u32 n = 0;

    if (arg[0] != '/') {
        for (u32 i = 0; cwd[i]; i++) full[n++] = cwd[i];
        full[n++] = '/';
    }
    for (u32 i = 0; arg[i]; i++) {
        if (n >= 2 * PATH_MAX - 2) return 0;
        full[n++] = arg[i];
    }
    full[n] = 0;

    u32 o = 0, i = 0;
    out[0] = 0;
    while (full[i]) {
        char comp[PATH_MAX];
        u32 cl = 0;
        while (full[i] == '/') i++;
        if (!full[i]) break;
        while (full[i] && full[i] != '/') {
            if (cl >= PATH_MAX - 1) return 0;
            comp[cl++] = full[i++];
        }
        comp[cl] = 0;
        if (str_eq(comp, ".")) continue;
        if (str_eq(comp, "..")) {
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;
            out[o] = 0;
            continue;
        }
        if (o + cl + 2 >= PATH_MAX) return 0;
        if (o > 0) out[o++] = '/';
        for (u32 k = 0; k < cl; k++) out[o++] = comp[k];
        out[o] = 0;
    }
    return 1;
}

static void cmd_ls(const char *dir)
{
    char name[48];
    int is_dir;
    u32 i = 0;

    while (fs_entry(dir, i++, name, &is_dir)) {
        put_latin1(name, is_dir ? ATTR_SOFT : ATTR_NORMAL);
        if (is_dir) con_putc('/', ATTR_SOFT);
        con_putc('\n', ATTR_NORMAL);
    }
    if (i == 1) con_print("(vacía)\n", ATTR_SOFT);
}

/* ---------- Reloj de la máquina (RTC) ---------- */

static u8 cmos_read(u8 reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static u8 bcd(u8 v)
{
    return (u8)((v & 0x0F) + (v >> 4) * 10);
}

/* Lee el reloj de la máquina */
static void rtc_now(u32 *year, u32 *mon, u32 *day, u32 *hour, u32 *min, u32 *sec)
{
    u32 tries = 0;
    while ((cmos_read(0x0A) & 0x80) && tries++ < 100000) { }     /* espera si se está actualizando */

    u8 s = cmos_read(0x00), mi = cmos_read(0x02), h = cmos_read(0x04);
    u8 d = cmos_read(0x07), mo = cmos_read(0x08), y = cmos_read(0x09);
    u8 rb = cmos_read(0x0B);
    u8 pm = h & 0x80;

    if (!(rb & 4)) {                               /* los valores vienen en BCD */
        s = bcd(s); mi = bcd(mi); d = bcd(d); mo = bcd(mo); y = bcd(y);
        h = bcd(h & 0x7F);
    } else {
        h &= 0x7F;
    }
    if (!(rb & 2)) {                               /* formato de 12 horas */
        if (h == 12) h = 0;
        if (pm) h = (u8)(h + 12);
    }
    *year = 2000u + y; *mon = mo; *day = d; *hour = h; *min = mi; *sec = s;
}

static void cmd_date(void)
{
    u32 y, mo, d, h, mi, s;
    rtc_now(&y, &mo, &d, &h, &mi, &s);
    put_num(y, ATTR_NORMAL);   con_putc('-', ATTR_NORMAL);
    put_num2(mo, ATTR_NORMAL); con_putc('-', ATTR_NORMAL);
    put_num2(d, ATTR_NORMAL);  con_putc(' ', ATTR_NORMAL);
    put_num2(h, ATTR_NORMAL);  con_putc(':', ATTR_NORMAL);
    put_num2(mi, ATTR_NORMAL); con_putc(':', ATTR_NORMAL);
    put_num2(s, ATTR_NORMAL);
    con_print("  (reloj de la máquina)\n", ATTR_SOFT);
}

/* Pone la fecha y la hora del reloj en los archivos que se escriban en el disco */
static void fat_stamp(void)
{
    u32 y, mo, d, h, mi, s;
    rtc_now(&y, &mo, &d, &h, &mi, &s);
    if (y < 1980 || y > 2107 || mo < 1 || mo > 12 || d < 1 || d > 31) return;
    fat_date = (u16)(((y - 1980) << 9) | (mo << 5) | d);
    fat_time = (u16)((h << 11) | (mi << 5) | (s / 2));
}

/* ---------- Terminal ---------- */

static void banner(void)
{
    con_print("\n", ATTR_NORMAL);
    con_print("  _     _      _          ___  ____  \n", ATTR_PROMPT);
    con_print(" | |   (_)_ __(_) ___    / _ \\/ ___| \n", ATTR_PROMPT);
    con_print(" | |   | | '__| |/ _ \\  | | | \\___ \\ \n", ATTR_PROMPT);
    con_print(" | |___| | |  | | (_) | | |_| |___) |\n", ATTR_PROMPT);
    con_print(" |_____|_|_|  |_|\\___/   \\___/|____/ \n\n", ATTR_PROMPT);
    con_print("LirioOS kernel 0.15 - escribe 'ayuda' para ver los comandos\n\n", ATTR_SOFT);
}

/* Escribe 'v' como 8 cifras hexadecimales */
static void put_hex32(u32 v, u8 attr)
{
    con_print("0x", attr);
    for (int i = 28; i >= 0; i -= 4) con_putc((u8)"0123456789ABCDEF"[(v >> i) & 15], attr);
}

static void put_size(u32 kib, u8 attr)         /* 1536 -> 1 MiB, 639 -> 639 KiB */
{
    if (kib >= 1024) { put_num(kib / 1024, attr); con_print(" MiB", attr); }
    else             { put_num(kib, attr);        con_print(" KiB", attr); }
}

static void cmd_mem(void)
{
    u32 used, freeb, blocks;
    heap_stats(&used, &freeb, &blocks);
    con_print("Memoria (páginas de 4 KiB):\n", ATTR_SOFT);
    con_print("  RAM utilizable  ", ATTR_NORMAL); put_size(pmm_total * 4, ATTR_NORMAL); con_putc('\n', ATTR_NORMAL);
    con_print("  libre           ", ATTR_NORMAL); put_size(pmm_free * 4, ATTR_NORMAL);  con_putc('\n', ATTR_NORMAL);
    con_print("  en uso          ", ATTR_NORMAL); put_size((pmm_total - pmm_free) * 4, ATTR_NORMAL); con_print("  (kernel, pantalla y montón)\n", ATTR_SOFT);
    con_print("Montón del kernel: ", ATTR_SOFT);
    put_size(used / 1024, ATTR_NORMAL); con_print(" en uso de ", ATTR_NORMAL);
    put_size(heap_size / 1024, ATTR_NORMAL); con_print(" (", ATTR_NORMAL);
    put_num(blocks, ATTR_NORMAL); con_print(" bloques)\n", ATTR_NORMAL);
    con_print("Mapa de memoria de GRUB:\n", ATTR_SOFT);
    for (u32 i = 0; i < mem_region_n; i++) {
        con_print("  ", ATTR_NORMAL);
        put_hex32(mem_regions[i].start, ATTR_NORMAL);
        con_print(" - ", ATTR_NORMAL);
        put_hex32(mem_regions[i].end, ATTR_NORMAL);
        u32 t = mem_regions[i].type;
        con_print(t == 1 ? "  libre      " : t == 3 ? "  ACPI       " : t == 4 ? "  ACPI NVS   " : t == 5 ? "  defectuosa " : "  reservada  ", ATTR_NORMAL);
        put_size((mem_regions[i].end - mem_regions[i].start) / 1024 + 1, ATTR_SOFT);
        con_putc('\n', ATTR_NORMAL);
    }
}

/* ---------- Disco ---------- */

static void cmd_disco(void)
{
    if (!disk_present) {
        con_print("No hay ningún disco duro conectado. En VirtualBox añade un disco al controlador IDE.\n", ATTR_NORMAL);
        return;
    }
    con_print("Disco: ", ATTR_SOFT);
    put_latin1(disk_model, ATTR_NORMAL);
    con_print(", ", ATTR_NORMAL);
    put_size(disk_sectors / 2, ATTR_NORMAL);
    con_putc('\n', ATTR_NORMAL);
    if (fat_ready) {
        con_print("Formato FAT32, etiqueta '", ATTR_SOFT);
        put_latin1(fat_label, ATTR_NORMAL);
        con_print("': ", ATTR_SOFT);
        put_size(fat_count_free() * fat_spc / 2, ATTR_NORMAL);
        con_print(" libres. Sus archivos están en la carpeta 'disco'.\n", ATTR_NORMAL);
    } else {
        con_print("Sin formato FAT32. Escribe 'formatear si' para prepararlo (borra todo lo que tenga).\n", ATTR_NORMAL);
    }
}

static void cmd_formatear(const char *arg)
{
    if (!disk_present) {
        con_print("No hay ningún disco duro conectado.\n", ATTR_NORMAL);
        return;
    }
    if (!str_eq(arg, "si") && !str_eq(arg, "sí")) {
        con_print("Esto BORRA todo el disco (", ATTR_NORMAL);
        put_size(disk_sectors / 2, ATTR_NORMAL);
        con_print("). Si estás seguro, escribe: formatear si\n", ATTR_NORMAL);
        return;
    }
    con_print("Formateando como FAT32, puede tardar unos segundos...\n", ATTR_SOFT);
    if (fat_format()) {
        disk_forget();
        con_print("Listo: disco FAT32 con la etiqueta LIRIO. Ábrelo en la carpeta 'disco'.\n", ATTR_NORMAL);
    } else {
        con_print("No se pudo formatear el disco.\n", ATTR_NORMAL);
    }
}

/* mkdir y rm: solo dentro de la carpeta 'disco' (lo demás vive en la memoria y es fijo) */
static void cmd_disk_edit(const char *cmd, const char *arg)
{
    char path[PATH_MAX];
    int mk = str_eq(cmd, "mkdir");
    if (arg[0] == 0) {
        con_print(mk ? "uso: mkdir <carpeta>\n" : "uso: rm <archivo o carpeta vacía>\n", ATTR_NORMAL);
    } else if (!path_resolve(arg, path)) {
        con_print("ruta demasiado larga\n", ATTR_NORMAL);
    } else if (!is_disk(path) || path[5] == 0) {
        put_latin1(cmd, ATTR_NORMAL);
        con_print(": solo se puede dentro de la carpeta 'disco'.\n", ATTR_NORMAL);
    } else if (!fat_ready) {
        con_print("No hay un disco FAT32 montado (mira 'disco').\n", ATTR_NORMAL);
    } else if (mk ? fat_mkdir(disk_rel(path)) : fat_remove(disk_rel(path))) {
        disk_forget();
        con_print(mk ? "Carpeta creada.\n" : "Borrado.\n", ATTR_NORMAL);
    } else {
        con_print(mk ? "No se pudo crear (¿ya existe, el nombre no es 8.3 o no hay sitio?).\n"
                     : "No se pudo borrar (¿no existe, o es una carpeta con cosas dentro?).\n", ATTR_NORMAL);
    }
}

#include "proc.h"

/* ---------- Interrupciones y temporizador ---------- */

struct idt_entry {
    u16 off_lo;
    u16 sel;
    u8  zero;
    u8  flags;
    u16 off_hi;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

extern void idt_load(const struct idt_ptr *p);
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);

static struct idt_entry idt[256];


static u16 idt_cs;

static void idt_set(u32 vec, void (*handler)(void), u8 flags)
{
    u32 h = (u32)handler;
    idt[vec].off_lo = (u16)(h & 0xFFFF);
    idt[vec].sel = idt_cs;
    idt[vec].zero = 0;
    idt[vec].flags = flags;
    idt[vec].off_hi = (u16)(h >> 16);
}

static void interrupts_init(void)
{
    __asm__ volatile ("mov %%cs, %0" : "=r"(idt_cs));
    idt_set(0x20, irq0_stub, 0x8E);                 /* presente, anillo 0, puerta de interrupción de 32 bits */
    idt_set(0x21, irq1_stub, 0x8E);
    idt_set(0x2C, irq12_stub, 0x8E);
    for (u32 v = 0; v < 32; v++) idt_set(v, (void (*)(void))exc_table[v], 0x8E);    /* excepciones del procesador */
    idt_set(0x80, syscall_stub, 0xEE);              /* llamadas al sistema: el anillo 3 puede usarla */

    struct idt_ptr p = { (u16)(sizeof(idt) - 1), (u32)idt };
    idt_load(&p);

    /* PIC: las IRQ 0-7 pasan a los vectores 0x20-0x27 y las 8-15 a 0x28-0x2F;  */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xF8); outb(0xA1, 0xEF);        /* activos: reloj (0), teclado (1), cascada (2) y ratón (12) */

    /* PIT (canal 0): 1193182 Hz / 11932 = 100 interrupciones por segundo */
    outb(0x43, 0x36);
    outb(0x40, (u8)(11932 & 0xFF));
    outb(0x40, (u8)(11932 >> 8));

    irq_on();
}

/* ---------- Programas de usuario ---------- */

static void cmd_programas(void)
{
    con_print("Programas incluidos en el sistema (se ejecutan con 'ejecutar <nombre>'):\n", ATTR_SOFT);
    for (u32 i = 0; i < BUILTIN_PROGS_COUNT; i++) {
        con_print("  ", ATTR_NORMAL);
        con_print(builtin_progs[i].name, ATTR_NORMAL);
        con_print("  (", ATTR_SOFT);
        put_size(builtin_progs[i].len / 1024 + 1, ATTR_SOFT);
        con_print(")\n", ATTR_SOFT);
    }
    con_print("También puedes ejecutar un archivo .elf de la carpeta 'disco'.\n", ATTR_SOFT);
}

#define PROG_MAX (256u * 1024u)

static void cmd_ejecutar(const char *arg)
{
    const u8 *img = 0;
    u8 *buf = 0;
    u32 len = 0;
    char path[PATH_MAX];

    if (arg[0] == 0) { con_print("uso: ejecutar <programa>   (mira 'programas')\n", ATTR_NORMAL); return; }

    for (u32 i = 0; i < BUILTIN_PROGS_COUNT && !img; i++) {
        const char *nm = builtin_progs[i].name;
        u32 k = 0;
        while (nm[k] && arg[k] == nm[k]) k++;
        if (nm[k] == 0 && (arg[k] == 0 || (arg[k] == '.' && arg[k + 1] == 'e' && arg[k + 2] == 'l' && arg[k + 3] == 'f' && arg[k + 4] == 0))) {
            img = builtin_progs[i].data;
            len = builtin_progs[i].len;
        }
    }
    if (!img) {
        if (!path_resolve(arg, path)) { con_print("ejecutar: ruta demasiado larga\n", ATTR_NORMAL); return; }
        if (!is_disk(path)) {
            con_print("ejecutar: no existe ese programa (mira 'programas' o usa un .elf de 'disco')\n", ATTR_NORMAL);
            return;
        }
        buf = (u8 *)kmalloc(PROG_MAX);
        if (!buf) { con_print("ejecutar: no hay memoria\n", ATTR_NORMAL); return; }
        if (!fat_read_file(disk_rel(path), buf, PROG_MAX, &len)) {
            kfree(buf);
            con_print("ejecutar: no existe: ", ATTR_NORMAL);
            put_latin1(arg, ATTR_NORMAL);
            con_putc('\n', ATTR_NORMAL);
            return;
        }
        img = buf;
    }

    u32 pid = proc_spawn(arg, img, len);
    if (buf) kfree(buf);
    if (!pid) {
        con_print("ejecutar: ", ATTR_NORMAL);
        con_print(proc_err, ATTR_NORMAL);
        con_putc('\n', ATTR_NORMAL);
    } else {
        con_print("[pid ", ATTR_SOFT); put_num(pid, ATTR_SOFT);
        con_print("] en marcha en segundo plano ('ps' los lista, 'matar <pid>' los detiene)\n", ATTR_SOFT);
    }
}

static void cmd_matar(const char *arg)
{
    u32 pid = 0;
    int ok = arg[0] >= '0' && arg[0] <= '9';
    for (const char *p = arg; *p; p++) {
        if (*p < '0' || *p > '9') { ok = 0; break; }
        pid = pid * 10 + (u32)(*p - '0');
    }
    if (!ok) con_print("uso: matar <pid>   (mira 'ps')\n", ATTR_NORMAL);
    else if (!proc_kill(pid)) con_print("matar: no hay ningún programa con ese pid\n", ATTR_NORMAL);
}

static const char *const known_cmds[] = {
    "ayuda", "help", "ls", "cd", "pwd", "cat", "echo", "limpiar", "clear", "about", "uname",
    "whoami", "date", "fecha", "uptime", "mem", "disco", "formatear", "rm", "mkdir", "ejecutar", "programas", "ps", "matar", "reiniciar", "reboot", "apagar", "halt",
};
#define KNOWN_COUNT (sizeof(known_cmds) / sizeof(known_cmds[0]))

/* Comandos de Linux que alguien teclearía por costumbre */
static const char *const linux_cmds[] = {
    "sudo", "su", "apt", "apt-get", "dpkg", "pacman", "dnf", "yum", "snap", "pip", "npm",
};
static const char *const edit_cmds[] = { "touch", "cp", "mv", "nano", "vim" };

static u32 edit_distance(const char *a, const char *b)
{
    u32 la = slen(a), lb = slen(b);
    u32 d[17][17];
    if (la > 16 || lb > 16) return 99;
    for (u32 i = 0; i <= la; i++) d[i][0] = i;
    for (u32 j = 0; j <= lb; j++) d[0][j] = j;
    for (u32 i = 1; i <= la; i++)
        for (u32 j = 1; j <= lb; j++) {
            u32 v = d[i - 1][j] + 1;
            u32 cost = a[i - 1] == b[j - 1] ? 0 : 1;
            if (d[i][j - 1] + 1 < v) v = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < v) v = d[i - 1][j - 1] + cost;
            d[i][j] = v;
        }
    return d[la][lb];
}

static int in_list(const char *w, const char *const *list, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (str_eq(w, list[i])) return 1;
    return 0;
}

static void run_command(const char *line)
{
    char cmd[24];
    char path[PATH_MAX];
    u32 n = 0;

    while (*line == ' ') line++;
    if (!*line) return;
    while (*line && *line != ' ' && n < sizeof(cmd) - 1) cmd[n++] = *line++;
    cmd[n] = 0;
    while (*line && *line != ' ') line++;
    while (*line == ' ') line++;
    const char *arg = line;                        /* lo que sigue al nombre del comando */

    if (str_eq(cmd, "help") || str_eq(cmd, "ayuda")) {
        con_print("Comandos de LirioOS:\n", ATTR_SOFT);
        con_print("  ayuda           muestra esta ayuda\n", ATTR_NORMAL);
        con_print("  ls [carpeta]    lista los archivos y carpetas\n", ATTR_NORMAL);
        con_print("  cd <carpeta>    entra en una carpeta (cd .. sube, cd / va a la raíz)\n", ATTR_NORMAL);
        con_print("  pwd             muestra la carpeta actual\n", ATTR_NORMAL);
        con_print("  cat <archivo>   muestra el contenido de un archivo\n", ATTR_NORMAL);
        con_print("  echo [texto]    escribe un texto\n", ATTR_NORMAL);
        con_print("  date            fecha y hora del reloj de la máquina\n", ATTR_NORMAL);
        con_print("  uptime          cuánto lleva encendido el sistema\n", ATTR_NORMAL);
        con_print("  mem             memoria: total, libre y mapa de la RAM\n", ATTR_NORMAL);
        con_print("  disco           estado del disco duro y de su carpeta 'disco'\n", ATTR_NORMAL);
        con_print("  formatear si    deja el disco como FAT32 vacío (borra todo)\n", ATTR_NORMAL);
        con_print("  mkdir <ruta>    crea una carpeta (solo dentro de 'disco')\n", ATTR_NORMAL);
        con_print("  rm <ruta>       borra un archivo o una carpeta vacía (solo en 'disco')\n", ATTR_NORMAL);
        con_print("  programas       lista los programas incluidos en el sistema\n", ATTR_NORMAL);
        con_print("  ejecutar <prog> lanza un programa en segundo plano (incluido o .elf del disco)\n", ATTR_NORMAL);
        con_print("  ps, matar <pid> programas en marcha / detener uno\n", ATTR_NORMAL);
        con_print("  whoami, uname   quién eres y qué sistema es este\n", ATTR_NORMAL);
        con_print("  limpiar         borra la pantalla\n", ATTR_NORMAL);
        con_print("  reiniciar, apagar\n", ATTR_NORMAL);
        con_print("Flecha arriba/abajo: comandos anteriores.\n", ATTR_SOFT);
    } else if (str_eq(cmd, "ls")) {
        if (arg[0] == 0) {
            cmd_ls(cwd);
        } else if (!path_resolve(arg, path)) {
            con_print("ls: ruta demasiado larga\n", ATTR_NORMAL);
        } else if (fs_is_dir(path)) {
            cmd_ls(path);
        } else if (fs_file(path)) {
            put_latin1(arg, ATTR_NORMAL);
            con_putc('\n', ATTR_NORMAL);
        } else {
            con_print("ls: no existe: ", ATTR_NORMAL);
            put_latin1(arg, ATTR_NORMAL);
            con_putc('\n', ATTR_NORMAL);
        }
    } else if (str_eq(cmd, "cd")) {
        if (arg[0] == 0) arg = "/";
        if (!path_resolve(arg, path)) {
            con_print("cd: ruta demasiado larga\n", ATTR_NORMAL);
        } else if (fs_is_dir(path)) {
            for (u32 i = 0; i < PATH_MAX; i++) cwd[i] = path[i];
        } else if (fs_file(path)) {
            con_print("cd: no es una carpeta: ", ATTR_NORMAL);
            put_latin1(arg, ATTR_NORMAL);
            con_print("   (para ver un archivo usa 'cat')\n", ATTR_SOFT);
        } else {
            con_print("cd: no existe: ", ATTR_NORMAL);
            put_latin1(arg, ATTR_NORMAL);
            con_putc('\n', ATTR_NORMAL);
        }
    } else if (str_eq(cmd, "pwd")) {
        con_putc('/', ATTR_NORMAL);
        put_latin1(cwd, ATTR_NORMAL);
        con_putc('\n', ATTR_NORMAL);
    } else if (str_eq(cmd, "cat")) {
        if (arg[0] == 0) {
            con_print("uso: cat <archivo>\n", ATTR_NORMAL);
        } else if (!path_resolve(arg, path)) {
            con_print("cat: ruta demasiado larga\n", ATTR_NORMAL);
        } else if (fs_file(path) && fs_file(path)->img) {
            con_print("cat: es una imagen: ábrela desde Archivos (dock)\n", ATTR_NORMAL);
        } else if (fs_file(path)) {
            con_print(fs_file(path)->data, ATTR_NORMAL);
        } else if (fs_is_dir(path)) {
            con_print("cat: es una carpeta (usa 'ls')\n", ATTR_NORMAL);
        } else {
            con_print("cat: no existe: ", ATTR_NORMAL);
            put_latin1(arg, ATTR_NORMAL);
            con_putc('\n', ATTR_NORMAL);
        }
    } else if (str_eq(cmd, "echo")) {
        put_latin1(arg, ATTR_NORMAL);
        con_putc('\n', ATTR_NORMAL);
    } else if (str_eq(cmd, "date") || str_eq(cmd, "fecha")) {
        cmd_date();
    } else if (str_eq(cmd, "programas")) {
        cmd_programas();
    } else if (str_eq(cmd, "ps")) {
        proc_list();
    } else if (str_eq(cmd, "matar")) {
        cmd_matar(arg);
    } else if (str_eq(cmd, "ejecutar")) {
        cmd_ejecutar(arg);
    } else if (str_eq(cmd, "mem")) {
        cmd_mem();
    } else if (str_eq(cmd, "disco")) {
        cmd_disco();
    } else if (str_eq(cmd, "formatear")) {
        cmd_formatear(arg);
    } else if (str_eq(cmd, "mkdir") || str_eq(cmd, "rm")) {
        cmd_disk_edit(cmd, arg);
    } else if (str_eq(cmd, "uptime")) {
        u32 t = ticks;
        con_print("Encendido hace ", ATTR_NORMAL);
        put_num(t / 6000, ATTR_NORMAL);
        con_print(" min ", ATTR_NORMAL);
        put_num((t / 100) % 60, ATTR_NORMAL);
        con_print(" s (", ATTR_NORMAL);
        put_num(t, ATTR_NORMAL);
        con_print(" tics del temporizador)\n", ATTR_SOFT);
    } else if (str_eq(cmd, "whoami")) {
        con_print("lirio\n", ATTR_NORMAL);
    } else if (str_eq(cmd, "uname")) {
        con_print("LirioOS 0.15 - kernel propio, x86 de 32 bits\n", ATTR_NORMAL);
    } else if (str_eq(cmd, "clear") || str_eq(cmd, "limpiar")) {
        con_clear();
    } else if (str_eq(cmd, "about")) {
        con_print("LirioOS 0.15 - kernel propio en C, modo gráfico, ratón, ventanas y sistema de archivos en memoria.\n", ATTR_NORMAL);
    } else if (str_eq(cmd, "reboot") || str_eq(cmd, "reiniciar")) {
        outb(0x64, 0xFE);
    } else if (str_eq(cmd, "halt") || str_eq(cmd, "apagar")) {
        con_print("Sistema detenido. Ya puedes cerrar la máquina.\n", ATTR_NORMAL);
        for (;;) __asm__ volatile ("cli; hlt");
    } else if (in_list(cmd, linux_cmds, sizeof(linux_cmds) / sizeof(linux_cmds[0]))) {
        put_latin1(cmd, ATTR_NORMAL);
        con_print(" es un comando de Linux. LirioOS no es Linux: aquí no hay usuarios ni paquetes (todavía).\n", ATTR_NORMAL);
    } else if (in_list(cmd, edit_cmds, sizeof(edit_cmds) / sizeof(edit_cmds[0]))) {
        put_latin1(cmd, ATTR_NORMAL);
        con_print(": todavía no existe. Para escribir texto usa Archivos > Nuevo o Editar; para borrar, rm; para carpetas, mkdir.\n", ATTR_NORMAL);
    } else {
        con_print("comando desconocido: ", ATTR_NORMAL);
        put_latin1(cmd, ATTR_NORMAL);
        con_putc('\n', ATTR_NORMAL);
        u32 best = 99, bi = 0;
        for (u32 i = 0; i < KNOWN_COUNT; i++) {
            u32 d = edit_distance(cmd, known_cmds[i]);
            if (d < best) { best = d; bi = i; }
        }
        if (best <= 1 || (best == 2 && slen(cmd) >= 5)) {
            con_print("¿Quisiste decir '", ATTR_SOFT);
            put_latin1(known_cmds[bi], ATTR_SOFT);
            con_print("'?\n", ATTR_SOFT);
        } else {
            con_print("Escribe 'ayuda' para ver los comandos.\n", ATTR_SOFT);
        }
    }
}

/* Historial de comandos (flechas arriba y abajo) */
#define HIST 8
static char hist[HIST][128];
static u32 hist_n;

static void hist_add(const char *line)
{
    if (!line[0]) return;
    if (hist_n > 0 && str_eq(hist[hist_n - 1], line)) return;
    if (hist_n == HIST) {
        for (u32 i = 1; i < HIST; i++)
            for (u32 k = 0; k < 128; k++) hist[i - 1][k] = hist[i][k];
        hist_n--;
    }
    u32 k = 0;
    while (line[k] && k < 127) { hist[hist_n][k] = line[k]; k++; }
    hist[hist_n][k] = 0;
    hist_n++;
}

/* ---------- Arranque ---------- */

struct mb_info {
    u32 flags;
    u32 mem_lower, mem_upper;           /* memoria baja y alta, en KiB */
    u32 pad_a[8];                       /* arranque, línea de órdenes, módulos, símbolos */
    u32 mmap_length, mmap_addr;         /* mapa de memoria (flag 6) */
    u32 pad_b[9];                       /* unidades, BIOS, VBE... hasta el framebuffer */
    u32 fb_addr_lo, fb_addr_hi;
    u32 fb_pitch, fb_width, fb_height;
    u8  fb_bpp, fb_type;
    u16 reserved;           /* GRUB alinea a 4 bytes la información de color */
    u8  red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
};


static void con_print_num(u32 n)
{
    char buf[12];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) con_putc((u8)buf[--i], ATTR_SOFT);
}

static void print_video_info(const struct mb_info *mb)
{
    con_print("Pantalla: ", ATTR_SOFT);
    con_print_num(mb->fb_width);
    con_print("x", ATTR_SOFT);
    con_print_num(mb->fb_height);
    con_print("x", ATTR_SOFT);
    con_print_num(mb->fb_bpp);
    con_print("  rojo=", ATTR_SOFT);
    con_print_num(mb->red_pos);
    con_print(" verde=", ATTR_SOFT);
    con_print_num(mb->green_pos);
    con_print(" azul=", ATTR_SOFT);
    con_print_num(mb->blue_pos);
    con_print("\n\n", ATTR_SOFT);
}

/* Mensaje de emergencia en modo texto, por si GRUB no pudo dar un modo gráfico */
static void text_fatal(const char *msg)
{
    volatile u16 *vga = (volatile u16 *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) vga[i] = (u16)(' ' | (0x5F << 8));
    for (int i = 0; msg[i] && i < 80; i++) vga[i] = (u16)((u8)msg[i] | (0x5F << 8));
    for (;;) __asm__ volatile ("cli; hlt");
}

void kmain(u32 magic, u32 info)
{
    const struct mb_info *mb = (const struct mb_info *)info;
    char line[128];
    u32 len;

    if (magic != 0x2BADB002 || !(mb->flags & (1u << 12)) || mb->fb_type != 1 || mb->fb_bpp != 32 || mb->fb_addr_hi != 0 ||
        mb->fb_width > MAX_W || mb->fb_height > MAX_H)
        text_fatal("LirioOS necesita un modo grafico de 32 bits (arranca con GRUB).");

    extern char _kernel_end[];
    mem_init((mb->flags >> 6) & 1, mb->mmap_addr, mb->mmap_length, mb->mem_upper,
             (u32)_kernel_end, (u32)mb);
    {   /* montón del kernel: hasta 8 MiB (o la cuarta parte de la RAM libre en máquinas pequeñas) */
        u32 pages = pmm_free / 4 > 2048 ? 2048 : pmm_free / 4;
        u32 base = pages ? pmm_alloc_run(pages) : 0;
        if (base) heap_init((void *)base, pages * PAGE_SIZE);
    }

    gui_set_fb((void *)mb->fb_addr_lo, mb->fb_pitch, (s32)mb->fb_width, (s32)mb->fb_height,
               mb->red_pos, mb->green_pos, mb->blue_pos);

    draw_desktop();
    mouse_init();
    banner();
    print_video_info(mb);
    gdt_init();
    paging_init();
    interrupts_init();

    fat_clock = fat_stamp;
    if (disk_init()) {
        con_print("Disco: ", ATTR_SOFT);
        put_latin1(disk_model, ATTR_NORMAL);
        con_print(", ", ATTR_NORMAL);
        put_size(disk_sectors / 2, ATTR_NORMAL);
        con_print(fat_mount() ? " (FAT32 montado: carpeta 'disco')\n" : " (sin formato FAT32: escribe 'disco')\n", ATTR_NORMAL);
    }

    for (;;) {
        u32 hpos = hist_n;

        con_print("lirio:/", ATTR_PROMPT);
        con_print(cwd, ATTR_PROMPT);
        con_print("> ", ATTR_PROMPT);
        len = 0;
        for (;;) {
            u32 c = input_poll();
            if (!c) {
                gui_tick();
                sched_reap();                          /* avisa de los programas que han terminado */
                sched_idle();                          /* deja su turno a los programas, o duerme hasta la siguiente interrupción */
                continue;
            }
            if (proc_key(c)) continue;                /* un programa que lee el teclado se queda con las teclas (Esc lo detiene) */
            if (gui_key(c)) continue;                 /* con el editor abierto, las teclas son para él */
            if (c == KEY_UP || c == KEY_DOWN) {
                if (c == KEY_UP && hpos > 0) hpos--;
                else if (c == KEY_DOWN && hpos < hist_n) hpos++;
                else continue;
                while (len > 0) { len--; con_putc('\b', ATTR_NORMAL); }
                if (hpos < hist_n)
                    for (const char *p = hist[hpos]; *p; p++) { line[len++] = *p; con_putc((u8)*p, ATTR_NORMAL); }
                continue;
            }
            if (c == '\n') {
                con_putc('\n', ATTR_NORMAL);
                break;
            } else if (c == '\b') {
                if (len > 0) { len--; con_putc('\b', ATTR_NORMAL); }
            } else if (c >= 32 && c < 256 && len < sizeof(line) - 1) {
                line[len++] = (char)c;
                con_putc(c, ATTR_NORMAL);
            }
        }
        line[len] = 0;
        hist_add(line);
        run_command(line);
    }
}
