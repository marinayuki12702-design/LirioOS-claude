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

static void cmd_date(void)
{
    u32 tries = 0;
    while ((cmos_read(0x0A) & 0x80) && tries++ < 100000) { }     /* espera si se está actualizando */

    u8 sec = cmos_read(0x00), min = cmos_read(0x02), hour = cmos_read(0x04);
    u8 day = cmos_read(0x07), mon = cmos_read(0x08), year = cmos_read(0x09);
    u8 rb = cmos_read(0x0B);
    u8 pm = hour & 0x80;

    if (!(rb & 4)) {                               /* los valores vienen en BCD */
        sec = bcd(sec); min = bcd(min); day = bcd(day); mon = bcd(mon); year = bcd(year);
        hour = bcd(hour & 0x7F);
    } else {
        hour &= 0x7F;
    }
    if (!(rb & 2)) {                               /* formato de 12 horas */
        if (hour == 12) hour = 0;
        if (pm) hour = (u8)(hour + 12);
    }

    put_num(2000u + year, ATTR_NORMAL); con_putc('-', ATTR_NORMAL);
    put_num2(mon, ATTR_NORMAL);         con_putc('-', ATTR_NORMAL);
    put_num2(day, ATTR_NORMAL);         con_putc(' ', ATTR_NORMAL);
    put_num2(hour, ATTR_NORMAL);        con_putc(':', ATTR_NORMAL);
    put_num2(min, ATTR_NORMAL);         con_putc(':', ATTR_NORMAL);
    put_num2(sec, ATTR_NORMAL);
    con_print("  (reloj de la máquina)\n", ATTR_SOFT);
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
    con_print("LirioOS kernel 0.7 - escribe 'ayuda' para ver los comandos\n\n", ATTR_SOFT);
}

static const char *const known_cmds[] = {
    "ayuda", "help", "ls", "cd", "pwd", "cat", "echo", "limpiar", "clear", "about", "uname",
    "whoami", "date", "fecha", "reiniciar", "reboot", "apagar", "halt",
};
#define KNOWN_COUNT (sizeof(known_cmds) / sizeof(known_cmds[0]))

/* Comandos de Linux que alguien teclearía por costumbre */
static const char *const linux_cmds[] = {
    "sudo", "su", "apt", "apt-get", "dpkg", "pacman", "dnf", "yum", "snap", "pip", "npm",
};
static const char *const edit_cmds[] = { "mkdir", "rm", "touch", "cp", "mv", "nano", "vim" };

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
    } else if (str_eq(cmd, "whoami")) {
        con_print("lirio\n", ATTR_NORMAL);
    } else if (str_eq(cmd, "uname")) {
        con_print("LirioOS 0.7 - kernel propio, x86 de 32 bits\n", ATTR_NORMAL);
    } else if (str_eq(cmd, "clear") || str_eq(cmd, "limpiar")) {
        con_clear();
    } else if (str_eq(cmd, "about")) {
        con_print("LirioOS 0.7 - kernel propio en C, modo gráfico, ratón, ventanas y sistema de archivos en memoria.\n", ATTR_NORMAL);
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
        con_print(": todavía no se pueden crear ni cambiar archivos (el sistema de archivos es de solo lectura).\n", ATTR_NORMAL);
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
        u32 hpos = hist_n;

        con_print("lirio:/", ATTR_PROMPT);
        con_print(cwd, ATTR_PROMPT);
        con_print("> ", ATTR_PROMPT);
        len = 0;
        for (;;) {
            u32 c = input_poll();
            if (!c) {
                __asm__ volatile ("pause");
                continue;
            }
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
