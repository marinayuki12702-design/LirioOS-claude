/* Kernel de LirioOS: modo gráfico, teclado español, sistema de archivos en memoria y terminal en ventana. */

#include "gui.h"

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

/* ---------- Cadenas ---------- */

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_starts(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

/* ---------- Teclado español (consulta directa, juego de códigos 1) ---------- */

#define DEAD_ACUTE  0x81
#define DEAD_DIAER  0x82
#define DEAD_GRAVE  0x83
#define DEAD_CIRC   0x84

#define KEYS 0x58

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

    if (ext) {                           /* teclas extendidas: solo nos importa AltGr */
        ext = 0;
        if (code == 0x38) altgr = !released;
        return 0;
    }
    if (code == 0x2A || code == 0x36) { shift = !released; return 0; }
    if (released) return 0;
    if (code == 0x3A) { caps ^= 1; return 0; }
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
    cfg = (u8)((cfg | 2) & ~0x20);       /* reloj del ratón activo */
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
static u32 input_poll(void)
{
    u32 key = 0;
    s32 adx = 0, ady = 0;
    int pending = 0;
    u8 s;

    while (((s = inb(0x64)) & 1) && !key) {
        u8 d = inb(0x60);
        if (s & 0x20) {                              /* byte del ratón */
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

/* ---------- Sistema de archivos en memoria (ramfs) ---------- */

struct ramfs_file {
    const char *name;
    const char *data;      /* texto UTF-8 */
};

static const struct ramfs_file ramfs[] = {
    { "bienvenida.txt",
      "Bienvenida a LirioOS.\nEste es un kernel nuevo, escrito desde cero.\n" },
    { "lirio.txt",
      "El lirio es una flor elegante. Este sistema quiere serlo también.\n" },
    { "pendiente.txt",
      "Siguientes pasos: ratón, FAT32 de solo lectura, disco y procesos.\n" },
};

#define RAMFS_COUNT (sizeof(ramfs) / sizeof(ramfs[0]))

static const struct ramfs_file *ramfs_find(const char *name)
{
    for (u32 i = 0; i < RAMFS_COUNT; i++)
        if (str_eq(ramfs[i].name, name)) return &ramfs[i];
    return 0;
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
    con_print("LirioOS kernel 0.3 - escribe 'ayuda' para ver los comandos\n\n", ATTR_SOFT);
}

static void run_command(const char *line)
{
    if (line[0] == 0) return;

    if (str_eq(line, "help") || str_eq(line, "ayuda")) {
        con_print("Comandos: ayuda, ls, cat <archivo>, echo <texto>, limpiar, about, reiniciar, apagar\n", ATTR_NORMAL);
    } else if (str_eq(line, "ls")) {
        for (u32 i = 0; i < RAMFS_COUNT; i++) {
            con_print(ramfs[i].name, ATTR_NORMAL);
            con_putc('\n', ATTR_NORMAL);
        }
    } else if (str_starts(line, "cat ")) {
        const struct ramfs_file *f = ramfs_find(line + 4);
        if (f) con_print(f->data, ATTR_NORMAL);
        else   con_print("cat: archivo no encontrado\n", ATTR_NORMAL);
    } else if (str_starts(line, "echo ")) {
        for (const char *p = line + 5; *p; p++)
            con_putc((u8)*p, ATTR_NORMAL);       /* el texto tecleado ya está en Latin-1 */
        con_putc('\n', ATTR_NORMAL);
    } else if (str_eq(line, "clear") || str_eq(line, "limpiar")) {
        con_clear();
    } else if (str_eq(line, "about")) {
        con_print("LirioOS 0.3 - kernel propio en C, x86 de 32 bits, modo gráfico, ratón y sistema de archivos en memoria.\n", ATTR_NORMAL);
    } else if (str_eq(line, "reboot") || str_eq(line, "reiniciar")) {
        outb(0x64, 0xFE);
    } else if (str_eq(line, "halt") || str_eq(line, "apagar")) {
        con_print("Sistema detenido. Ya puedes cerrar la máquina.\n", ATTR_NORMAL);
        for (;;) __asm__ volatile ("cli; hlt");
    } else {
        con_print("comando desconocido: ", ATTR_NORMAL);
        for (const char *p = line; *p; p++)
            con_putc((u8)*p, ATTR_NORMAL);
        con_putc('\n', ATTR_NORMAL);
    }
}

/* ---------- Arranque ---------- */

struct mb_info {
    u32 flags;
    u8  pad[84];            /* campos que no usamos hasta el framebuffer */
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

    gui_set_fb((void *)mb->fb_addr_lo, mb->fb_pitch, (s32)mb->fb_width, (s32)mb->fb_height,
               mb->red_pos, mb->green_pos, mb->blue_pos);

    draw_desktop();
    mouse_init();
    banner();
    print_video_info(mb);

    for (;;) {
        con_print("lirio> ", ATTR_PROMPT);
        len = 0;
        for (;;) {
            u32 c = input_poll();
            if (!c) {
                __asm__ volatile ("pause");
                continue;
            }
            if (c == '\n') {
                con_putc('\n', ATTR_NORMAL);
                break;
            } else if (c == '\b') {
                if (len > 0) { len--; con_putc('\b', ATTR_NORMAL); }
            } else if (c >= 32 && len < sizeof(line) - 1) {
                line[len++] = (char)c;
                con_putc(c, ATTR_NORMAL);
            }
        }
        line[len] = 0;
        run_command(line);
    }
}
