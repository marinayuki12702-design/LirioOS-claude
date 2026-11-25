#include "lirio.h"

/* Explorador de archivos de LirioOS, como programa de usuario con ventana propia:
 * carpetas y archivos, abrir (doble clic), visor de texto con editor, visor de imágenes y archivo nuevo. */

#define CW 560
#define CH 360
#define COLS 5
#define CELL_W 108
#define CELL_H 96
#define MAX_ENT 15

#define C_BODY  0x241A3F
#define C_TEXT  0xF0E8FF
#define C_SOFT  0xB9A2F5
#define C_PINK  0xFF9BD2

static int v;
static char dir[64];                        /* carpeta abierta ("" = raíz), sin barra inicial */
static char file[64];                       /* archivo abierto ("" = ninguno) */
static int mode;                            /* 0 carpeta, 1 viendo un archivo, 2 editando */
static int sel = -1, hover = -1;            /* elemento seleccionado y elemento/botón bajo el ratón */
static u32 last_click;
static int err;                             /* el último guardado falló */
static int big;                             /* el archivo abierto es demasiado grande para verlo */
static int is_img;
static char names[MAX_ENT][48];
static int kinds[MAX_ENT];                  /* 1 archivo, 2 carpeta */
static int nent;
static char text[4100];
static int tlen;

/* ---- utilidades de texto ---- */
static void scopy(char *d, const char *s) { while ((*d++ = *s++)) { } }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void join(const char *d, const char *n, char *out)
{
    u32 o = 0;
    if (d[0]) { for (u32 i = 0; d[i] && o < 60; i++) out[o++] = d[i]; out[o++] = '/'; }
    for (u32 i = 0; n[i] && o < 62; i++) out[o++] = n[i];
    out[o] = 0;
}

static void parent(char *p)
{
    u32 n = largo(p);
    while (n > 0 && p[n - 1] != '/') n--;
    if (n > 0) n--;
    p[n] = 0;
}

static const char *absoluta(const char *p)         /* "imagenes/a.png" -> "/imagenes/a.png" */
{
    static char r[72];
    r[0] = '/';
    scopy(r + 1, p);
    return r;
}

/* ---- datos ---- */
static void listar_carpeta(void)
{
    nent = 0;
    for (int i = 0; i < MAX_ENT; i++) {
        int k = sys(15, (u32)absoluta(dir), (u32)i, (u32)names[i]);
        if (k <= 0) break;
        kinds[i] = k;
        nent++;
    }
    sel = hover = -1;
}

static void abrir_archivo(const char *path)
{
    int k = sys(16, (u32)absoluta(path), 0, 0);
    scopy(file, path);
    big = (k == 4);
    is_img = (k == 2);
    tlen = 0;
    text[0] = 0;
    if (k == 1) {
        tlen = leer_archivo(absoluta(path), text, 4000);
        if (tlen < 0) tlen = 0;
        text[tlen] = 0;
    }
    mode = 1;
    err = 0;
}

static void cerrar_archivo(void) { file[0] = 0; mode = 0; err = 0; }

static void paso_imagen(int d)
{
    int idx[MAX_ENT], n = 0, cur = -1;
    char p[64];
    for (int i = 0; i < nent; i++) {
        if (kinds[i] != 1) continue;
        join(dir, names[i], p);
        if (sys(16, (u32)absoluta(p), 0, 0) != 2) continue;
        if (seq(p, file)) cur = n;
        idx[n++] = i;
    }
    if (n < 2 || cur < 0) return;
    join(dir, names[idx[(cur + d + n) % n]], p);
    scopy(file, p);
}

static void archivo_nuevo(void)
{
    char nm[24], p[64];
    for (int n = 1; n < 100; n++) {
        u32 q = 0;
        const char *b = "nuevo";
        while (b[q]) { nm[q] = b[q]; q++; }
        if (n > 1) { if (n >= 10) nm[q++] = (char)('0' + n / 10); nm[q++] = (char)('0' + n % 10); }
        const char *e = ".txt";
        for (u32 j = 0; e[j]; j++) nm[q++] = e[j];
        nm[q] = 0;
        join(dir, nm, p);
        if (sys(16, (u32)absoluta(p), 0, 0) == 0) break;
    }
    scopy(file, p);
    tlen = 0; text[0] = 0; big = 0; is_img = 0;
    mode = 2; err = 0;
}

/* ---- dibujo ---- */
static void boton(int x, int w, const char *t, int id, int dim)
{
    int hot = (hover == id);
    ventana_rect(v, x, 8, w, 28, dim ? 0x3A2D63 : (hot ? 0x9B7BE6 : 0x5B4A9A));
    ventana_texto(v, x + (w - (int)largo(t) * 8) / 2, 14, t, dim ? 0x9C8FCF : 0xFFFFFF, 1);
}

#define B_SUBIR   100
#define B_INICIO  101
#define B_NUEVO   102
#define B_ATRAS   103
#define B_EDITAR  104
#define B_ANT     105
#define B_SIG     106
#define B_GUARDAR 107
#define B_CANCEL  108

static void texto_ajustado(int x, int y, int cursor)
{
    char line[120];
    int n = 0, cols = 0, row = 0;
    for (int i = 0; i <= tlen; i++) {
        int fin = (i == tlen);
        char c = fin ? 0 : text[i];
        if (!fin && c != '\n' && !((c & 0xC0) == 0x80)) cols++;
        int flush = fin || c == '\n' || cols > 64 || n > 112;
        if (!flush) { line[n++] = c; continue; }
        if (!fin && c != '\n') { line[n++] = c; cols = 1; n--; i--; cols = 0; }   /* parte la línea y repite este carácter */
        if (fin && cursor) line[n++] = '_';
        line[n] = 0;
        if (row < 15) ventana_texto(v, x, y + row * 18, line, C_TEXT, 1);
        row++;
        n = 0; cols = 0;
        if (fin) break;
    }
}

static void dibujar(void)
{
    ventana_rect(v, 0, 0, CW, CH, C_BODY);

    if (mode == 0) {
        boton(8, 76, "Subir", B_SUBIR, dir[0] == 0);
        boton(92, 76, "Inicio", B_INICIO, 0);
        boton(176, 76, "Nuevo", B_NUEVO, 0);
        char ruta[72];
        ruta[0] = '/';
        scopy(ruta + 1, dir);
        ventana_texto(v, 266, 14, ruta, C_PINK, 1);
        for (int i = 0; i < nent; i++) {
            int cx = 8 + (i % COLS) * CELL_W, cy = 44 + (i / COLS) * CELL_H;
            if (i == sel) ventana_rect(v, cx + 4, cy + 2, CELL_W - 8, CELL_H - 6, 0x4A3A80);
            else if (i == hover) ventana_rect(v, cx + 4, cy + 2, CELL_W - 8, CELL_H - 6, 0x33265C);
            int mx = cx + CELL_W / 2;
            if (kinds[i] == 2) {                         /* carpeta */
                ventana_rect(v, mx - 30, cy + 8, 24, 10, 0xF2A8D2);
                ventana_rect(v, mx - 32, cy + 14, 64, 44, 0xFFC2E2);
                ventana_rect(v, mx - 22, cy + 32, 44, 4, 0xE58EBF);
            } else {                                     /* archivo */
                ventana_rect(v, mx - 21, cy + 10, 42, 52, 0xF0E8FF);
                ventana_rect(v, mx - 21, cy + 10, 42, 10, 0xD9C8FF);
                for (int k = 0; k < 3; k++) ventana_rect(v, mx - 13, cy + 26 + k * 10, 26, 3, C_SOFT);
            }
            char nm[14];
            int n = 0;
            while (names[i][n] && n < 12) { nm[n] = names[i][n]; n++; }
            nm[n] = 0;
            ventana_texto(v, mx - n * 4, cy + 68, nm, C_TEXT, 1);
        }
        if (nent == 0) ventana_texto(v, 20, 60, "Esta carpeta está vacía.", C_SOFT, 1);
    } else if (mode == 2) {
        boton(8, 84, "Guardar", B_GUARDAR, 0);
        boton(100, 84, "Cancelar", B_CANCEL, 0);
        ventana_texto(v, 200, 14, file, C_PINK, 1);
        if (err) ventana_texto(v, 200, 30, "No se pudo guardar (¿disco?)", 0xFF8FB1, 1);
        ventana_rect(v, 8, 44, CW - 16, CH - 52, 0x1B1230);
        texto_ajustado(16, 52, 1);
    } else {
        boton(8, 76, "Atrás", B_ATRAS, 0);
        if (is_img) {
            boton(92, 76, "Anterior", B_ANT, 0);
            boton(176, 84, "Siguiente", B_SIG, 0);
            ventana_texto(v, 276, 14, file, C_PINK, 1);
            sys5(17, (u32)v, (8u << 16) | 44u, ((u32)(CW - 16) << 16) | (u32)(CH - 52), (u32)absoluta(file), 0);
        } else {
            if (!big) boton(92, 76, "Editar", B_EDITAR, 0);
            ventana_texto(v, big ? 100 : 176, 14, file, C_PINK, 1);
            ventana_rect(v, 8, 44, CW - 16, CH - 52, 0x1B1230);
            if (big) ventana_texto(v, 16, 52, "Este archivo es demasiado grande para abrirlo aquí.", C_SOFT, 1);
            else texto_ajustado(16, 52, 0);
        }
    }
    ventana_mostrar(v);
}

/* ---- ratón y teclado ---- */
static int objetivo(int x, int y)                   /* botón (>= 100), casilla o -1 */
{
    if (y >= 8 && y < 36) {
        if (mode == 0) {
            if (x >= 8 && x < 84) return B_SUBIR;
            if (x >= 92 && x < 168) return B_INICIO;
            if (x >= 176 && x < 252) return B_NUEVO;
        } else if (mode == 2) {
            if (x >= 8 && x < 92) return B_GUARDAR;
            if (x >= 100 && x < 184) return B_CANCEL;
        } else {
            if (x >= 8 && x < 84) return B_ATRAS;
            if (is_img && x >= 92 && x < 168) return B_ANT;
            if (is_img && x >= 176 && x < 260) return B_SIG;
            if (!is_img && !big && x >= 92 && x < 168) return B_EDITAR;
        }
        return -1;
    }
    if (mode != 0 || y < 44) return -1;
    int c = (x - 8) / CELL_W, r = (y - 44) / CELL_H;
    if (x < 8 || c >= COLS) return -1;
    int i = r * COLS + c;
    return i < nent ? i : -1;
}

static void clic(int t)
{
    if (t < 0) { sel = -1; return; }
    if (t == B_SUBIR) { parent(dir); listar_carpeta(); }
    else if (t == B_INICIO) { dir[0] = 0; listar_carpeta(); }
    else if (t == B_NUEVO) archivo_nuevo();
    else if (t == B_ATRAS) cerrar_archivo();
    else if (t == B_EDITAR) { mode = 2; err = 0; }
    else if (t == B_ANT) paso_imagen(-1);
    else if (t == B_SIG) paso_imagen(1);
    else if (t == B_GUARDAR) {
        if (escribir_archivo(absoluta(file), text, (u32)tlen) == 0) { mode = 1; listar_carpeta(); }
        else err = 1;
    } else if (t == B_CANCEL) {
        if (sys(16, (u32)absoluta(file), 0, 0) == 0) { file[0] = 0; mode = 0; } else mode = 1;
    } else if (t < nent) {
        u32 ahora = ticks();
        int doble = (t == sel) && ahora - last_click < 60;
        sel = t;
        last_click = ahora;
        if (doble) {
            char p[64];
            join(dir, names[t], p);
            if (kinds[t] == 2) { scopy(dir, p); listar_carpeta(); }
            else abrir_archivo(p);
        }
    }
}

static void tecla(u32 c)
{
    if (mode != 2) { if (c == 27) { if (mode == 1) cerrar_archivo(); else salir(0); } return; }
    if (c == 27) { clic(B_CANCEL); return; }
    if (c == '\b') {
        while (tlen > 0 && (text[tlen - 1] & 0xC0) == 0x80) tlen--;
        if (tlen > 0) tlen--;
    } else if (c == '\n' || (c >= 32 && c < 256 && c != 127)) {
        u32 need = c < 128 ? 1 : 2;
        if (tlen + (int)need > 4000) return;
        if (c < 128) text[tlen++] = (char)c;
        else { text[tlen++] = (char)(0xC0 | (c >> 6)); text[tlen++] = (char)(0x80 | (c & 0x3F)); }
    }
    text[tlen] = 0;
}

__attribute__((section(".text.start")))
void _start(void)
{
    v = ventana_crear(CW, CH, "Archivos");
    if (v < 0) { texto("archivos: no pude abrir la ventana\n"); salir(1); }
    listar_carpeta();
    dibujar();

    struct evento e;
    int abajo = 0;
    for (;;) {
        if (ventana_evento(v, &e, 1) < 0) salir(0);
        do {
            if (e.tipo == 1) {
                tecla(e.a);
            } else if (e.tipo == 2) {
                int t = objetivo((int)e.a, (int)e.b);
                if (e.c && !abajo) clic(t);
                else if (!e.c) hover = (t >= 0 && (t >= 100 || mode == 0)) ? t : -1;
                abajo = (int)e.c;
            }
        } while (ventana_evento(v, &e, 0) == 1);
        dibujar();
    }
}
