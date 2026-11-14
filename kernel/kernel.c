/* Kernel mínimo de LirioOS: pantalla de texto, teclado, sistema de archivos en memoria y un terminal. */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define VGA_ADDR   ((volatile u16 *)0xB8000)
#define VGA_COLS   80
#define VGA_ROWS   25

/* Colores VGA: texto blanco sobre magenta para recordar al morado de LirioOS */
#define COLOR_NORMAL  0x5F
#define COLOR_TITLE   0x5E
#define COLOR_SOFT    0x5D

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

/* ---------- Pantalla ---------- */

static u32 cur_x, cur_y;
static u8  cur_color = COLOR_NORMAL;

static void cursor_update(void)
{
    u16 pos = (u16)(cur_y * VGA_COLS + cur_x);
    outb(0x3D4, 14);
    outb(0x3D5, (u8)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (u8)(pos & 0xFF));
}

static void screen_clear(void)
{
    for (u32 i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_ADDR[i] = (u16)(' ' | (cur_color << 8));
    cur_x = 0;
    cur_y = 0;
    cursor_update();
}

static void screen_scroll(void)
{
    for (u32 i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++)
        VGA_ADDR[i] = VGA_ADDR[i + VGA_COLS];
    for (u32 i = 0; i < VGA_COLS; i++)
        VGA_ADDR[VGA_COLS * (VGA_ROWS - 1) + i] = (u16)(' ' | (cur_color << 8));
    cur_y = VGA_ROWS - 1;
}

static void put_char(char c)
{
    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else if (c == '\b') {
        if (cur_x > 0) {
            cur_x--;
            VGA_ADDR[cur_y * VGA_COLS + cur_x] = (u16)(' ' | (cur_color << 8));
        }
    } else {
        VGA_ADDR[cur_y * VGA_COLS + cur_x] = (u16)((u8)c | (cur_color << 8));
        cur_x++;
    }
    if (cur_x >= VGA_COLS) { cur_x = 0; cur_y++; }
    if (cur_y >= VGA_ROWS) screen_scroll();
    cursor_update();
}

static void print(const char *s)
{
    while (*s) put_char(*s++);
}

static void print_color(const char *s, u8 color)
{
    u8 old = cur_color;
    cur_color = color;
    print(s);
    cur_color = old;
}

/* ---------- Teclado (consulta directa, distribución US básica) ---------- */

static const char scancode_map[58] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static char key_read(void)
{
    for (;;) {
        if (inb(0x64) & 1) {
            u8 sc = inb(0x60);
            if (sc & 0x80) continue;          /* tecla soltada */
            if (sc < sizeof(scancode_map) && scancode_map[sc])
                return scancode_map[sc];
        }
    }
}

/* ---------- Sistema de archivos en memoria (ramfs) ---------- */

struct ramfs_file {
    const char *name;
    const char *data;
};

static const struct ramfs_file ramfs[] = {
    { "bienvenida.txt",
      "Bienvenida a LirioOS.\nEste es un kernel nuevo, escrito desde cero.\n" },
    { "lirio.txt",
      "El lirio es una flor elegante. Este sistema quiere serlo tambien.\n" },
    { "pendiente.txt",
      "Siguientes pasos: FAT32 de solo lectura, disco, procesos.\n" },
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
    print_color("  _     _      _          ___  ____  \n", COLOR_TITLE);
    print_color(" | |   (_)_ __(_) ___    / _ \\/ ___| \n", COLOR_TITLE);
    print_color(" | |   | | '__| |/ _ \\  | | | \\___ \\ \n", COLOR_TITLE);
    print_color(" | |___| | |  | | (_) | | |_| |___) |\n", COLOR_TITLE);
    print_color(" |_____|_|_|  |_|\\___/   \\___/|____/ \n\n", COLOR_TITLE);
    print_color("LirioOS kernel 0.1 - escribe 'help' para ver los comandos\n\n", COLOR_SOFT);
}

static void run_command(const char *line)
{
    if (line[0] == 0) return;

    if (str_eq(line, "help")) {
        print("Comandos: help, ls, cat <archivo>, echo <texto>, clear, about, reboot, halt\n");
    } else if (str_eq(line, "ls")) {
        for (u32 i = 0; i < RAMFS_COUNT; i++) {
            print(ramfs[i].name);
            put_char('\n');
        }
    } else if (str_starts(line, "cat ")) {
        const struct ramfs_file *f = ramfs_find(line + 4);
        if (f) print(f->data);
        else   print("cat: archivo no encontrado\n");
    } else if (str_starts(line, "echo ")) {
        print(line + 5);
        put_char('\n');
    } else if (str_eq(line, "clear")) {
        screen_clear();
    } else if (str_eq(line, "about")) {
        print("LirioOS 0.1 - kernel propio en C, x86 de 32 bits, sistema de archivos en memoria.\n");
    } else if (str_eq(line, "reboot")) {
        outb(0x64, 0xFE);
    } else if (str_eq(line, "halt")) {
        print("Sistema detenido.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    } else {
        print("comando desconocido: ");
        print(line);
        put_char('\n');
    }
}

void kmain(u32 magic, u32 info)
{
    (void)info;
    char line[128];
    u32 len;

    screen_clear();
    banner();

    if (magic != 0x2BADB002)
        print("Aviso: arrancado sin Multiboot.\n");

    for (;;) {
        print_color("lirio> ", COLOR_TITLE);
        len = 0;
        for (;;) {
            char c = key_read();
            if (c == '\n') {
                put_char('\n');
                break;
            } else if (c == '\b') {
                if (len > 0) { len--; put_char('\b'); }
            } else if (c >= 32 && len < sizeof(line) - 1) {
                line[len++] = c;
                put_char(c);
            }
        }
        line[len] = 0;
        run_command(line);
    }
}
