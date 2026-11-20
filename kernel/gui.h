/* Interfaz gráfica de LirioOS: framebuffer lineal de 32 bits, escritorio morado, dock y terminal en ventana.
 * No toca puertos ni hardware: solo dibuja en memoria, por eso se puede probar también en un PC normal. */

#ifndef LIRIO_GUI_H
#define LIRIO_GUI_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

#include "font.h"
#include "io.h"
#include "mem.h"
#include "fs.h"

#define NO_BG 0xFFFFFFFFu

/* ---------- Framebuffer ---------- */

#define MAX_W 1024
#define MAX_H 768

/* fb es el destino de dibujo actual (la memoria intermedia o el fondo guardado);
 * scr es la pantalla real. Todo se compone fuera de pantalla y después se copia. */
static volatile u32 *fb;
static u32 fb_pitch;            /* en píxeles de 32 bits */
static volatile u32 *scr;
static u32 scr_pitch;
static s32 fb_w, fb_h;
static u8  pos_r = 16, pos_g = 8, pos_b = 0;

static u32 back_buf[MAX_W * MAX_H];
static u32 wall_buf[MAX_W * MAX_H];

static void target(u32 *buf)
{
    fb = buf;
    fb_pitch = (u32)fb_w;
}

static void gui_set_fb(void *addr, u32 pitch_bytes, s32 w, s32 h, u8 rpos, u8 gpos, u8 bpos)
{
    scr = (volatile u32 *)addr;
    scr_pitch = pitch_bytes / 4;
    fb_w = w;
    fb_h = h;
    pos_r = rpos;
    pos_g = gpos;
    pos_b = bpos;
    target(back_buf);
}

static u32 pack(u32 rgb)
{
    return (((rgb >> 16) & 255u) << pos_r) | (((rgb >> 8) & 255u) << pos_g) | ((rgb & 255u) << pos_b);
}

static u32 unpack(u32 px)
{
    return (((px >> pos_r) & 255u) << 16) | (((px >> pos_g) & 255u) << 8) | ((px >> pos_b) & 255u);
}

static u32 mix(u32 a, u32 b, u32 t)     /* t = 0..255: 0 devuelve a, 255 devuelve b */
{
    u32 r = (((a >> 16) & 255u) * (255u - t) + ((b >> 16) & 255u) * t) / 255u;
    u32 g = (((a >> 8) & 255u) * (255u - t) + ((b >> 8) & 255u) * t) / 255u;
    u32 bl = ((a & 255u) * (255u - t) + (b & 255u) * t) / 255u;
    return (r << 16) | (g << 8) | bl;
}

static void px_set(s32 x, s32 y, u32 rgb)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    fb[(u32)y * fb_pitch + (u32)x] = pack(rgb);
}

static void px_blend(s32 x, s32 y, u32 rgb, u32 alpha)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    if (alpha >= 255) { px_set(x, y, rgb); return; }
    u32 old = unpack(fb[(u32)y * fb_pitch + (u32)x]);
    fb[(u32)y * fb_pitch + (u32)x] = pack(mix(old, rgb, alpha));
}

static void fill_rect(s32 x, s32 y, s32 w, s32 h, u32 rgb, u32 alpha)
{
    for (s32 j = 0; j < h; j++)
        for (s32 i = 0; i < w; i++)
            px_blend(x + i, y + j, rgb, alpha);
}

static int in_round(s32 px, s32 py, s32 w, s32 h, s32 r)
{
    s32 cx, cy;
    if (px < r) cx = r; else if (px >= w - r) cx = w - r - 1; else return 1;
    if (py < r) cy = r; else if (py >= h - r) cy = h - r - 1; else return 1;
    s32 dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

static void fill_rrect(s32 x, s32 y, s32 w, s32 h, s32 r, u32 rgb, u32 alpha)
{
    for (s32 j = 0; j < h; j++)
        for (s32 i = 0; i < w; i++)
            if (in_round(i, j, w, h, r))
                px_blend(x + i, y + j, rgb, alpha);
}

static void fill_circle(s32 cx, s32 cy, s32 r, u32 rgb, u32 alpha)
{
    for (s32 dy = -r; dy <= r; dy++)
        for (s32 dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                px_blend(cx + dx, cy + dy, rgb, alpha);
}

static s32 iabs(s32 v) { return v < 0 ? -v : v; }

/* ---------- Copia a pantalla y cursor del ratón ---------- */

#define CUR_W 12
#define CUR_H 19

static const char *const cursor_sprite[CUR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX  X..X    ",
    "X    X..X   ",
    "     X..X   ",
    "      X..X  ",
    "      X..X  ",
    "       XX   ",
};

static s32 mouse_x, mouse_y;

/* Tics del temporizador (100 por segundo): los suma la interrupción del reloj, ver kernel.c */
static volatile u32 ticks;
#define DBL_TICKS 60            /* dos clics con menos de 0,6 s entre ellos = doble clic */

/* Copia una zona de la memoria intermedia a la pantalla con el cursor ya mezclado, de modo que cada
 * píxel se escribe una sola vez con su valor final (así el cursor no desaparece ni parpadea). */
static void present_rect(s32 x, s32 y, s32 w, s32 h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    for (s32 j = 0; j < h; j++) {
        s32 py = y + j;
        s32 cj = py - mouse_y;
        const u32 *src = back_buf + (u32)py * (u32)fb_w;
        volatile u32 *dst = scr + (u32)py * scr_pitch;
        for (s32 i = 0; i < w; i++) {
            s32 px = x + i;
            u32 v = src[px];
            s32 ci = px - mouse_x;
            if (cj >= 0 && cj < CUR_H && ci >= 0 && ci < CUR_W) {
                char c = cursor_sprite[cj][ci];
                if (c != ' ') v = pack(c == 'X' ? 0x2E2150 : 0xFFFFFF);
            }
            dst[px] = v;
        }
    }
}

static void present(void)
{
    present_rect(0, 0, fb_w, fb_h);
}

static void cursor_move_to(s32 nx, s32 ny)
{
    s32 ox = mouse_x, oy = mouse_y;
    mouse_x = nx;
    mouse_y = ny;
    present_rect(ox, oy, CUR_W, CUR_H);     /* borra el cursor viejo */
    present_rect(nx, ny, CUR_W, CUR_H);     /* y pone el nuevo, ya mezclado con el fondo */
}

/* ---------- Texto ---------- */

/* Lee un carácter de un texto UTF-8 y lo devuelve como código Latin-1 */
static u32 next_char(const char **s)
{
    u8 c = (u8)**s;
    (*s)++;
    if (c < 0x80) return c;
    if ((c == 0xC2 || c == 0xC3) && **s) {
        u8 n = (u8)**s;
        (*s)++;
        return (u32)(((c & 0x1Fu) << 6) | (n & 0x3Fu));
    }
    return '?';
}

static void draw_glyph(s32 x, s32 y, u32 ch, u32 fg, u32 bg, s32 scale)
{
    if (ch < 32 || ch > 255) ch = ' ';
    const u8 *g = font8x16[ch - 32];
    for (s32 row = 0; row < 16; row++) {
        u8 bits = g[row];
        for (s32 col = 0; col < 8; col++) {
            int on = bits & (0x80 >> col);
            if (on) {
                if (scale == 1) px_set(x + col, y + row, fg);
                else fill_rect(x + col * scale, y + row * scale, scale, scale, fg, 255);
            } else if (bg != NO_BG) {
                if (scale == 1) px_set(x + col, y + row, bg);
                else fill_rect(x + col * scale, y + row * scale, scale, scale, bg, 255);
            }
        }
    }
}

static s32 text_width(const char *s, s32 scale)
{
    s32 n = 0;
    while (*s) { next_char(&s); n++; }
    return n * 8 * scale;
}

static void draw_text(s32 x, s32 y, const char *s, u32 fg, s32 scale)
{
    while (*s) {
        u32 c = next_char(&s);
        draw_glyph(x, y, c, fg, NO_BG, scale);
        x += 8 * scale;
    }
}

static void draw_text_shadow(s32 x, s32 y, const char *s, u32 fg, u32 shadow)
{
    draw_text(x + 1, y + 1, s, shadow, 1);
    draw_text(x, y, s, fg, 1);
}

/* ---------- Fondo de escritorio ---------- */

/* Onda suave: aproxima un seno con dos parábolas. Devuelve -amp..amp */
static s32 wave(s32 x, s32 amp, s32 period, s32 phase)
{
    s32 p = (((x + phase) % period) * 256) / period;
    s32 v;
    if (p < 128) v = p * (128 - p);
    else v = -((p - 128) * (256 - p));
    return amp * v / 4096;
}

static void sparkle(s32 cx, s32 cy, s32 r, u32 alpha)
{
    fill_circle(cx, cy, r / 2 + 2, 0xFFFFFF, alpha / 5);
    for (s32 dy = -r; dy <= r; dy++)
        for (s32 dx = -r; dx <= r; dx++)
            if (iabs(dx) * iabs(dy) * 2 <= r)
                px_blend(cx + dx, cy + dy, 0xFFFFFF, alpha);
}

struct band {
    s32 base_pct, amp, period, phase;
    u32 left, right;
};

static void draw_wallpaper(void)
{
    for (s32 y = 0; y < fb_h; y++)
        for (s32 x = 0; x < fb_w; x++) {
            u32 t = (u32)((3 * y * 255 / fb_h + x * 255 / fb_w) / 4);
            px_set(x, y, mix(0xF3ECFF, 0xB79BF2, t));
        }

    static const struct band bands[3] = {
        { 62, 34, 1500,   0, 0xE6DBFF, 0xBFA6F5 },
        { 72, 30, 1300, 320, 0xC9B2FA, 0x9B7BE6 },
        { 84, 24, 1200, 640, 0x8D6BDC, 0x6F4CC4 },
    };

    for (s32 b = 0; b < 3; b++) {
        for (s32 x = 0; x < fb_w; x++) {
            s32 top = fb_h * bands[b].base_pct / 100 + wave(x, bands[b].amp, bands[b].period, bands[b].phase);
            u32 col = mix(bands[b].left, bands[b].right, (u32)(x * 255 / fb_w));
            for (s32 y = top; y < fb_h; y++)
                px_set(x, y, col);
            if (b < 2) {                      /* línea blanca fina sobre los dos primeros bordes */
                px_blend(x, top, 0xFFFFFF, b == 0 ? 210 : 120);
                px_blend(x, top + 1, 0xFFFFFF, b == 0 ? 120 : 60);
            }
        }
    }

    sparkle(fb_w * 84 / 100, fb_h * 17 / 100, 26, 235);
    sparkle(fb_w * 12 / 100, fb_h * 28 / 100, 18, 220);
    sparkle(fb_w * 67 / 100, fb_h * 11 / 100, 12, 210);
    sparkle(fb_w * 96 / 100, fb_h * 49 / 100, 14, 200);
    sparkle(fb_w * 5 / 100,  fb_h * 55 / 100, 10, 190);
    sparkle(fb_w * 44 / 100, fb_h * 8 / 100,  9, 190);
    sparkle(fb_w * 55 / 100, fb_h * 82 / 100, 32, 235);
    sparkle(fb_w * 90 / 100, fb_h * 78 / 100, 20, 215);
    sparkle(fb_w * 22 / 100, fb_h * 84 / 100, 13, 200);
}

/* ---------- Escritorio: barra superior, iconos y dock ---------- */

static void draw_topbar(void)
{
    fill_rect(0, 0, fb_w, 24, 0x2E2150, 215);
    sparkle(16, 12, 7, 255);
    draw_text(30, 4, "LirioOS", 0xFFFFFF, 1);
    draw_text(fb_w - 8 * 3 - 12, 4, "ES", 0xD9C8FF, 1);
}

static void draw_desktop_icons(void)
{
    /* Carpeta de inicio */
    fill_rrect(32, 52, 20, 10, 4, 0xF2A8D2, 255);
    fill_rrect(26, 58, 44, 32, 6, 0xFFC2E2, 255);
    fill_rect(36, 70, 24, 3, 0xE58EBF, 255);
    draw_text_shadow(48 - text_width("Inicio", 1) / 2, 96, "Inicio", 0xFFFFFF, 0x4B2F93);

    /* Papelera */
    fill_rect(30, 148, 36, 6, 0xB9A2F5, 255);
    fill_rect(42, 143, 12, 5, 0xB9A2F5, 255);
    fill_rrect(33, 156, 30, 36, 5, 0xD9C8FF, 255);
    for (s32 i = 0; i < 3; i++)
        fill_rect(40 + i * 8, 162, 3, 24, 0xA58BEA, 255);
    draw_text_shadow(48 - text_width("Papelera", 1) / 2, 200, "Papelera", 0xFFFFFF, 0x4B2F93);
}

/* ---------- Estado de las ventanas ---------- */

#define CON_COLS 96
#define CON_ROWS 26

#define WIN_W 800
#define WIN_H 480
#define TITLE_H 34
#define BODY_COLOR 0x241A3F
#define APP_COLOR  0x2B2147

static s32 win_x, win_y;            /* ventana del terminal */
static s32 app_x, app_y;            /* ventana de aplicación (una a la vez) */
static s32 app_w = 460, app_h = 250;
static int term_vis = 1;            /* el terminal está a la vista */
static int app_vis;                 /* hay una aplicación abierta */
static int app_kind;                /* icono del dock que la abrió (1..4) */
static int app_top;                 /* 1 = la aplicación está por encima del terminal */
static s32 con_px0, con_py0;

/* ---------- Dock ---------- */

#define DOCK_N    5
#define DOCK_SIZE 52
#define DOCK_GAP  14
#define DOCK_PAD  16

static const char *const dock_names[DOCK_N] = { "Terminal", "Archivos", "Navegador", "Ajustes", "Más" };
static int dock_hover = -1;

static void dock_geom(s32 *x0, s32 *y0, s32 *w, s32 *h)
{
    *w = DOCK_N * DOCK_SIZE + (DOCK_N - 1) * DOCK_GAP + 2 * DOCK_PAD;
    *h = DOCK_SIZE + 24;
    *x0 = (fb_w - *w) / 2;
    *y0 = fb_h - *h - 14;
}

static void dock_icon_pos(int i, s32 *x, s32 *y)
{
    s32 x0, y0, w, h;
    dock_geom(&x0, &y0, &w, &h);
    *x = x0 + DOCK_PAD + i * (DOCK_SIZE + DOCK_GAP);
    *y = y0 + 12;
}

/* Icono del dock bajo el punto (px, py), o -1 */
static int dock_hit(s32 px, s32 py)
{
    for (int i = 0; i < DOCK_N; i++) {
        s32 x, y;
        dock_icon_pos(i, &x, &y);
        if (px >= x - DOCK_GAP / 2 && px < x + DOCK_SIZE + DOCK_GAP / 2 && py >= y - 8 && py < y + DOCK_SIZE + 8)
            return i;
    }
    return -1;
}

static void draw_dock(void)
{
    const s32 size = DOCK_SIZE;
    s32 x0, y0, w, h;
    dock_geom(&x0, &y0, &w, &h);

    fill_rrect(x0, y0 + 4, w, h, 20, 0x2E2150, 40);   /* sombra suave */
    fill_rrect(x0, y0, w, h, 20, 0xFFFFFF, 120);

    for (int i = 0; i < DOCK_N; i++) {
        s32 ix, iy;
        dock_icon_pos(i, &ix, &iy);
        s32 cx = ix + size / 2, cy = iy + size / 2;

        if (i == dock_hover)
            fill_rrect(ix - 6, iy - 6, size + 12, size + 12, 17, 0xFFFFFF, 110);

        switch (i) {
        case 0:  /* Terminal */
            fill_rrect(ix, iy, size, size, 13, 0x2B2147, 255);
            draw_glyph(ix + 10, iy + 10, '>', 0xFF9BD2, NO_BG, 2);
            draw_glyph(ix + 22, iy + 14, '_', 0xFFFFFF, NO_BG, 2);
            break;
        case 1:  /* Archivos */
            fill_rrect(ix, iy, size, size, 13, 0xF6B6D8, 255);
            fill_rrect(ix + 10, iy + 12, 16, 8, 3, 0xFFFFFF, 255);
            fill_rrect(ix + 8, iy + 17, 36, 24, 5, 0xFFFFFF, 255);
            break;
        case 2:  /* Navegador */
            fill_rrect(ix, iy, size, size, 13, 0x8FB8FF, 255);
            fill_circle(cx, cy, 18, 0xFFFFFF, 255);
            fill_circle(cx, cy, 14, 0x8FB8FF, 255);
            fill_rect(cx - 1, cy - 14, 3, 29, 0xFFFFFF, 255);
            fill_rect(cx - 14, cy - 1, 29, 3, 0xFFFFFF, 255);
            break;
        case 3:  /* Ajustes */
            fill_rrect(ix, iy, size, size, 13, 0xB79BF2, 255);
            fill_rect(cx - 3, cy - 20, 6, 40, 0xFFFFFF, 255);
            fill_rect(cx - 20, cy - 3, 40, 6, 0xFFFFFF, 255);
            fill_circle(cx, cy, 13, 0xFFFFFF, 255);
            fill_circle(cx, cy, 6, 0xB79BF2, 255);
            break;
        default: /* Más */
            fill_rrect(ix, iy, size, size, 13, 0x5F3DBA, 255);
            for (s32 k = -1; k <= 1; k++)
                fill_circle(cx + k * 13, cy, 4, 0xFFFFFF, 255);
            break;
        }

        int open = (i == 0) ? term_vis : (app_vis && app_kind == i);
        if (open) fill_circle(cx, y0 + h - 7, 2, 0x5F3DBA, 255);      /* aplicación abierta */
    }

    if (dock_hover >= 0) {                                             /* nombre sobre el icono */
        s32 ix, iy;
        dock_icon_pos(dock_hover, &ix, &iy);
        const char *name = dock_names[dock_hover];
        s32 bw = text_width(name, 1) + 20;
        s32 bx = ix + size / 2 - bw / 2, by = y0 - 34;
        fill_rrect(bx, by, bw, 24, 8, 0x2E2150, 235);
        draw_text(bx + 10, by + 4, name, 0xFFFFFF, 1);
    }
}

/* ---------- Marco de ventana ---------- */

static void draw_frame(s32 x, s32 y, s32 w, s32 h, const char *title, u32 body, int active)
{
    /* Sombra: solo el borde, porque el interior queda tapado por la ventana */
    for (s32 i = 1; i <= 4; i++) {
        s32 sx = x - i, sy = y - i + 8, sw = w + 2 * i, sh = h + 2 * i;
        for (s32 j = 0; j < sh; j++) {
            s32 ay = sy + j;
            int inner_row = ay >= y + 14 && ay < y + h - 14;
            for (s32 k = 0; k < sw; k++) {
                s32 ax = sx + k;
                if (inner_row && ax >= x + 14 && ax < x + w - 14) {
                    k = x + w - 14 - sx - 1;
                    continue;
                }
                if (in_round(k, j, sw, sh, 12 + i))
                    px_blend(ax, ay, 0x2E2150, 14);
            }
        }
    }

    u32 bar = active ? 0x7C5CD0 : 0xA593DD;
    fill_rrect(x, y, w, h, 12, body, 255);
    fill_rrect(x, y, w, TITLE_H, 12, bar, 255);
    fill_rect(x, y + 20, w, 14, bar, 255);

    fill_circle(x + 20, y + 17, 6, 0xFF8FB1, 255);     /* cerrar */
    fill_circle(x + 40, y + 17, 6, 0xFFD479, 255);     /* minimizar */
    fill_circle(x + 60, y + 17, 6, 0x9BE7A8, 255);     /* centrar */

    draw_text(x + (w - text_width(title, 1)) / 2, y + 9, title, 0xFFFFFF, 1);
}

/* ---------- Consola (el texto del terminal) ---------- */

static u8  con_ch[CON_ROWS][CON_COLS];
static u8  con_at[CON_ROWS][CON_COLS];
static s32 con_x, con_y;

static const u32 con_palette[3] = { 0xF0E8FF, 0xFF9BD2, 0xB9A2F5 };

#define ATTR_NORMAL 0
#define ATTR_PROMPT 1
#define ATTR_SOFT   2

static void compose(void);

static void con_draw_cell(s32 r, s32 c)
{
    draw_glyph(con_px0 + c * 8, con_py0 + r * 16, con_ch[r][c], con_palette[con_at[r][c]], BODY_COLOR, 1);
}

static int rects_overlap(s32 ax, s32 ay, s32 aw, s32 ah, s32 bx, s32 by, s32 bw, s32 bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* 0 = el terminal no se ve; 1 = se puede dibujar directamente; 2 = algo lo tapa y hay que recomponer todo */
static int con_mode(void)
{
    s32 dx, dy, dw, dh;
    if (!term_vis) return 0;
    if (con_py0 < 24) return 2;
    dock_geom(&dx, &dy, &dw, &dh);
    if (rects_overlap(con_px0, con_py0, CON_COLS * 8, CON_ROWS * 16, dx, dy - 36, dw, dh + 36)) return 2;
    if (app_vis && app_top &&
        rects_overlap(con_px0, con_py0, CON_COLS * 8, CON_ROWS * 16, app_x - 8, app_y - 8, app_w + 16, app_h + 16))
        return 2;
    return 1;
}

static void con_cursor(int on)
{
    int m = con_mode();
    if (m == 0) return;
    if (m == 2) {
        if (on) { compose(); present(); }
        return;
    }
    if (on) fill_rect(con_px0 + con_x * 8, con_py0 + con_y * 16 + 14, 8, 2, 0xFF9BD2, 255);
    else    con_draw_cell(con_y, con_x);
    present_rect(con_px0 + con_x * 8, con_py0 + con_y * 16, 8, 16);
}

static void con_cell_show(s32 r, s32 c)
{
    if (con_mode() != 1) return;
    con_draw_cell(r, c);
    present_rect(con_px0 + c * 8, con_py0 + r * 16, 8, 16);
}

/* Dibuja todo el texto en la memoria intermedia (sin copiarlo a la pantalla) */
static void con_paint(void)
{
    for (s32 r = 0; r < CON_ROWS; r++)
        for (s32 c = 0; c < CON_COLS; c++)
            con_draw_cell(r, c);
    fill_rect(con_px0 + con_x * 8, con_py0 + con_y * 16 + 14, 8, 2, 0xFF9BD2, 255);
}

static void con_redraw(void)
{
    int m = con_mode();
    if (m == 0) return;
    if (m == 2) { compose(); present(); return; }
    con_paint();
    present_rect(con_px0, con_py0, CON_COLS * 8, CON_ROWS * 16);
}

static void con_reset(void)
{
    for (s32 r = 0; r < CON_ROWS; r++)
        for (s32 c = 0; c < CON_COLS; c++) {
            con_ch[r][c] = ' ';
            con_at[r][c] = ATTR_NORMAL;
        }
    con_x = 0;
    con_y = 0;
}

static void con_clear(void)
{
    con_reset();
    con_redraw();
}

static void con_scroll(void)
{
    for (s32 r = 1; r < CON_ROWS; r++)
        for (s32 c = 0; c < CON_COLS; c++) {
            con_ch[r - 1][c] = con_ch[r][c];
            con_at[r - 1][c] = con_at[r][c];
        }
    for (s32 c = 0; c < CON_COLS; c++) {
        con_ch[CON_ROWS - 1][c] = ' ';
        con_at[CON_ROWS - 1][c] = ATTR_NORMAL;
    }
    con_y = CON_ROWS - 1;
    con_redraw();
}

/* Escribe un carácter Latin-1 en el terminal */
static void con_putc(u32 ch, u8 attr)
{
    con_cursor(0);
    if (ch == '\n') {
        con_x = 0;
        con_y++;
    } else if (ch == '\b') {
        if (con_x > 0) {
            con_x--;
            con_ch[con_y][con_x] = ' ';
            con_cell_show(con_y, con_x);
        }
    } else {
        con_ch[con_y][con_x] = (u8)ch;
        con_at[con_y][con_x] = attr;
        con_cell_show(con_y, con_x);
        con_x++;
    }
    if (con_x >= CON_COLS) { con_x = 0; con_y++; }
    if (con_y >= CON_ROWS) con_scroll();
    con_cursor(1);
}

/* Escribe un texto UTF-8 (el del código fuente) */
static void con_print(const char *s, u8 attr)
{
    while (*s) con_putc(next_char(&s), attr);
}

/* ---------- Ventanas ---------- */

static void draw_term(void)
{
    draw_frame(win_x, win_y, WIN_W, WIN_H, "lirio@lirioos: terminal", BODY_COLOR, !(app_vis && app_top));
    con_px0 = win_x + 14;
    con_py0 = win_y + TITLE_H + 10;
    con_paint();
}

/* ---------- Explorador de archivos ---------- */

#define EXP_COLS 5
#define EXP_CW   108
#define EXP_CH   96
#define EXP_MAX  15
#define EXP_UP   1000
#define EXP_HOME 1001
#define EXP_BACK 1002
#define EXP_PREV 1003
#define EXP_NEXT 1004
#define EXP_NEW    1005
#define EXP_EDIT   1006
#define EXP_SAVE   1007
#define EXP_CANCEL 1008

static char exp_dir[PATH_MAX];      /* carpeta que se está viendo ("" = raíz) */
static char exp_file[PATH_MAX];     /* archivo abierto en el visor ("" = ninguno) */
static int  exp_hover = -1;         /* elemento o botón bajo el ratón */
static int  exp_sel = -1;           /* elemento seleccionado (a falta del segundo clic) */
static u32  exp_click;              /* tic del último clic sobre un elemento */
static u32  ed_key_tick;            /* tic de la última tecla (el cursor parpadea desde ahí) */
static int  ed_shown = 1;           /* ¿se dibujó el cursor del editor la última vez? */
static int  exp_editing;            /* 1 = se está editando exp_file */
static int  exp_err;                /* 1 = el último "Guardar" falló */
static char ed_buf[FS_TEXT_MAX + 1];    /* texto en edición (UTF-8) */
static u32  ed_len;

static void exp_cell(int i, s32 *x, s32 *y)
{
    *x = app_x + 14 + (i % EXP_COLS) * EXP_CW;
    *y = app_y + 88 + (i / EXP_COLS) * EXP_CH;
}

static int exp_is_img(void);
static int exp_too_big(void);

/* Qué hay bajo (px, py) en el explorador: número de elemento, un botón (EXP_*) o -1 */
static int exp_target(s32 px, s32 py)
{
    char name[48];
    int is_dir;

    if (!app_vis || app_kind != 1) return -1;
    if (px < app_x || px >= app_x + app_w || py < app_y || py >= app_y + app_h) return -1;
    if (py >= app_y + 44 && py < app_y + 72) {
        if (exp_editing) {
            if (px >= app_x + 14 && px < app_x + 98) return EXP_SAVE;
            if (px >= app_x + 106 && px < app_x + 190) return EXP_CANCEL;
            return -1;
        }
        if (!exp_file[0] && px >= app_x + 182 && px < app_x + 258) return EXP_NEW;
        if (exp_file[0] && !exp_is_img() && !exp_too_big() && px >= app_x + 98 && px < app_x + 174) return EXP_EDIT;
        if (px >= app_x + 14 && px < app_x + 90) return exp_file[0] ? EXP_BACK : EXP_UP;
        if (!exp_file[0] && px >= app_x + 98 && px < app_x + 174) return EXP_HOME;
        if (exp_is_img() && px >= app_x + 98 && px < app_x + 174) return EXP_PREV;
        if (exp_is_img() && px >= app_x + 182 && px < app_x + 266) return EXP_NEXT;
        return -1;
    }
    if (exp_file[0]) return -1;
    for (int i = 0; i < EXP_MAX && fs_entry(exp_dir, (u32)i, name, &is_dir); i++) {
        s32 cx, cy;
        exp_cell(i, &cx, &cy);
        if (px >= cx && px < cx + EXP_CW && py >= cy && py < cy + EXP_CH) return i;
    }
    return -1;
}

/* ¿El archivo abierto en el visor es una imagen? */
static int exp_is_img(void)
{
    const struct ramfs_file *f = exp_file[0] ? fs_file(exp_file) : 0;
    return f && f->img;
}

/* ¿El archivo abierto es demasiado grande para editarlo? (solo pasa con archivos del disco) */
static int exp_too_big(void)
{
    return exp_file[0] && is_disk(exp_file) && fs_file(exp_file) && disk_big;
}

/* Pasa a la imagen anterior (-1) o siguiente (+1) de la carpeta, dando la vuelta al llegar al final */
static void exp_step(int dir)
{
    char name[48], path[PATH_MAX];
    int is_dir, total = 0, cur = -1;

    for (u32 i = 0; fs_entry(exp_dir, i, name, &is_dir); i++) {
        if (is_dir) continue;
        fs_join(exp_dir, name, path);
        const struct ramfs_file *f = fs_file(path);
        if (!f || !f->img) continue;
        if (str_eq(path, exp_file)) cur = total;
        total++;
    }
    if (total < 2 || cur < 0) return;
    int want = (cur + dir + total) % total, n = 0;
    for (u32 i = 0; fs_entry(exp_dir, i, name, &is_dir); i++) {
        if (is_dir) continue;
        fs_join(exp_dir, name, path);
        const struct ramfs_file *f = fs_file(path);
        if (!f || !f->img) continue;
        if (n++ != want) continue;
        u32 k = 0;
        while (path[k]) { exp_file[k] = path[k]; k++; }
        exp_file[k] = 0;
        return;
    }
}

/* Dibuja una imagen ajustada (sin deformarla) y centrada dentro del recuadro bw x bh */
static void draw_image_fit(const struct img *im, s32 x, s32 y, s32 bw, s32 bh)
{
    s32 dw = bw, dh = (s32)im->h * bw / (s32)im->w;
    if (dh > bh) { dh = bh; dw = (s32)im->w * bh / (s32)im->h; }
    if (dw < 1 || dh < 1) return;
    s32 ox = x + (bw - dw) / 2, oy = y + (bh - dh) / 2;
    for (s32 j = 0; j < dh; j++) {
        const u8 *row = im->idx + (u32)(j * (s32)im->h / dh) * im->w;
        for (s32 i = 0; i < dw; i++)
            px_set(ox + i, oy + j, im->pal[row[i * (s32)im->w / dw]]);
    }
}

static void explorer_click(int t)
{
    char name[48], path[PATH_MAX];
    int is_dir;

    if (t != exp_sel) exp_sel = -1;
    if (t == EXP_UP) {
        fs_parent(exp_dir);
    } else if (t == EXP_HOME) {
        exp_dir[0] = 0;
    } else if (t == EXP_BACK) {
        exp_file[0] = 0;
    } else if (t == EXP_PREV || t == EXP_NEXT) {
        exp_step(t == EXP_NEXT ? 1 : -1);
    } else if (t == EXP_NEW) {                        /* archivo nuevo: nuevo.txt, nuevo2.txt... (se crea al guardar) */
        for (int n = 1; n < 100; n++) {
            char nm[24];
            u32 q = 0;
            const char *b = "nuevo";
            while (b[q]) { nm[q] = b[q]; q++; }
            if (n > 1) { if (n >= 10) nm[q++] = (char)('0' + n / 10); nm[q++] = (char)('0' + n % 10); }
            const char *e = ".txt";
            for (u32 j = 0; e[j]; j++) nm[q++] = e[j];
            nm[q] = 0;
            fs_join(exp_dir, nm, path);
            if (!fs_file(path)) break;
        }
        u32 k = 0;
        while (path[k]) { exp_file[k] = path[k]; k++; }
        exp_file[k] = 0;
        ed_len = 0; ed_buf[0] = 0; exp_editing = 1; exp_err = 0; ed_key_tick = ticks;
    } else if (t == EXP_EDIT) {
        const struct ramfs_file *f = fs_file(exp_file);
        ed_len = 0;
        if (f && f->data)
            while (f->data[ed_len] && ed_len < FS_TEXT_MAX) { ed_buf[ed_len] = f->data[ed_len]; ed_len++; }
        ed_buf[ed_len] = 0;
        exp_editing = 1; exp_err = 0; ed_key_tick = ticks;
    } else if (t == EXP_SAVE) {
        if (fs_write(exp_file, ed_buf)) exp_editing = 0;
        else exp_err = 1;
    } else if (t == EXP_CANCEL) {
        exp_editing = 0;
        if (!fs_file(exp_file)) exp_file[0] = 0;      /* un archivo nuevo sin guardar no existe */
    } else if (t >= 0 && fs_entry(exp_dir, (u32)t, name, &is_dir)) {
        int dbl = (t == exp_sel) && (u32)(ticks - exp_click) < DBL_TICKS;
        exp_sel = t;
        exp_click = ticks;
        if (dbl) {                                    /* doble clic: abrir */
            exp_sel = -1;
            fs_join(exp_dir, name, path);
            char *dst = is_dir ? exp_dir : exp_file;
            u32 k = 0;
            while (path[k]) { dst[k] = path[k]; k++; }
            dst[k] = 0;
        }
    }
    exp_hover = -1;
}

static void draw_button(s32 x, s32 y, s32 w, const char *label, int hot, int dim)
{
    fill_rrect(x, y, w, 28, 9, dim ? 0x3A2D63 : (hot ? 0x9B7BE6 : 0x5B4A9A), 255);
    draw_text(x + (w - text_width(label, 1)) / 2, y + 6, label, dim ? 0x9C8FCF : 0xFFFFFF, 1);
}

static void draw_folder_icon(s32 cx, s32 top)
{
    fill_rrect(cx - 30, top, 24, 10, 4, 0xF2A8D2, 255);
    fill_rrect(cx - 32, top + 6, 64, 44, 8, 0xFFC2E2, 255);
    fill_rect(cx - 22, top + 24, 44, 4, 0xE58EBF, 255);
}

static void draw_file_icon(s32 cx, s32 top)
{
    fill_rrect(cx - 21, top + 2, 42, 52, 6, 0xF0E8FF, 255);
    for (s32 i = 0; i < 3; i++)
        fill_rect(cx - 13, top + 18 + i * 10, 26, 3, 0xB9A2F5, 255);
    fill_rrect(cx - 21, top + 2, 42, 10, 6, 0xD9C8FF, 255);
}

/* Escribe un texto con saltos de línea, cortando donde no cabe */
static void draw_wrapped(s32 x, s32 y, s32 maxcols, s32 maxlines, const char *s, u32 color)
{
    s32 col = 0, line = 0;
    while (*s && line < maxlines) {
        u32 c = next_char(&s);
        if (c == '\n') { col = 0; line++; continue; }
        if (col >= maxcols) {
            col = 0;
            line++;
            if (line >= maxlines) break;
        }
        draw_glyph(x + col * 8, y + line * 18, c, color, NO_BG, 1);
        col++;
    }
}

/* ---------- Editor de texto (dentro de la ventana de Archivos) ---------- */

#define ED_COLS  ((app_w - 56) / 8)
#define ED_LINES ((app_h - 130) / 18)

/* Dónde queda el cursor al final del texto (mismo ajuste de líneas que al dibujar) */
static void ed_layout(s32 *col, s32 *line)
{
    const char *s = ed_buf;
    s32 c = 0, l = 0;
    while (*s) {
        u32 ch = next_char(&s);
        if (ch == '\n') { c = 0; l++; continue; }
        if (c >= ED_COLS) { c = 0; l++; }
        c++;
    }
    if (c >= ED_COLS) { c = 0; l++; }
    *col = c;
    *line = l;
}

static void draw_editor_body(s32 x, s32 y, s32 w, s32 h)
{
    fill_rrect(x + 14, y + 88, w - 28, h - 102, 10, 0x1E1535, 255);
    s32 cc, cl;
    ed_layout(&cc, &cl);
    s32 top = (cl >= ED_LINES) ? cl - ED_LINES + 1 : 0;         /* con mucho texto, se ve el final */
    const char *s = ed_buf;
    s32 c = 0, l = 0;
    while (*s) {
        u32 ch = next_char(&s);
        if (ch == '\n') { c = 0; l++; continue; }
        if (c >= ED_COLS) { c = 0; l++; }
        if (l >= top && l - top < ED_LINES) draw_glyph(x + 28 + c * 8, y + 102 + (l - top) * 18, ch, 0xF0E8FF, NO_BG, 1);
        c++;
    }
    ed_shown = ((ticks - ed_key_tick) / 50) % 2 == 0;           /* parpadea cada medio segundo */
    if (ed_shown) fill_rect(x + 28 + cc * 8, y + 102 + (cl - top) * 18, 8, 16, 0xFF9BD2, 255);

    char st[72];
    u32 si = 0;
    if (exp_err) {
        const char *m = "No se pudo guardar: no hay memoria o archivos libres";
        while (*m) st[si++] = *m++;
    } else {
        char dg[8]; int dn = 0; u32 v = ed_len;
        if (v == 0) dg[dn++] = '0';
        while (v > 0) { dg[dn++] = (char)('0' + v % 10); v /= 10; }
        while (dn > 0) st[si++] = dg[--dn];
        const char *m = " de 4000 bytes - escribe con el teclado";
        while (*m) st[si++] = *m++;
    }
    st[si] = 0;
    draw_text(x + 16, y + h - 26, st, exp_err ? 0xFF9BD2 : 0xB9A2F5, 1);
}

static void draw_explorer(void)
{
    s32 x = app_x, y = app_y, w = app_w, h = app_h;
    int viewing = exp_file[0] != 0;
    char bar[PATH_MAX + 2];
    u32 k = 0;

    draw_frame(x, y, w, h, "Archivos", APP_COLOR, app_top);

    int img_view = viewing && exp_is_img();
    s32 bar_x = x + 182;
    if (exp_editing) {
        draw_button(x + 14, y + 44, 84, "Guardar", exp_hover == EXP_SAVE, 0);
        draw_button(x + 106, y + 44, 84, "Cancelar", exp_hover == EXP_CANCEL, 0);
        bar_x = x + 198;
    } else if (viewing) {
        draw_button(x + 14, y + 44, 76, "Volver", exp_hover == EXP_BACK, 0);
        if (!img_view) draw_button(x + 98, y + 44, 76, "Editar", exp_hover == EXP_EDIT, exp_too_big());
        if (img_view) {
            draw_button(x + 98, y + 44, 76, "Anterior", exp_hover == EXP_PREV, 0);
            draw_button(x + 182, y + 44, 84, "Siguiente", exp_hover == EXP_NEXT, 0);
            bar_x = x + 274;
        }
    } else {
        draw_button(x + 14, y + 44, 76, "Subir", exp_hover == EXP_UP, exp_dir[0] == 0);
        draw_button(x + 98, y + 44, 76, "Inicio", exp_hover == EXP_HOME, 0);
        draw_button(x + 182, y + 44, 76, "Nuevo", exp_hover == EXP_NEW, 0);
        bar_x = x + 266;
    }

    bar[k++] = '/';
    for (const char *p = viewing ? exp_file : exp_dir; *p && k < PATH_MAX; p++) bar[k++] = *p;
    bar[k] = 0;
    fill_rrect(bar_x, y + 44, x + w - 14 - bar_x, 28, 9, 0x241A3F, 255);
    draw_text(bar_x + 12, y + 50, bar, 0xD9C8FF, 1);

    if (exp_editing) { draw_editor_body(x, y, w, h); return; }

    if (viewing) {
        const struct ramfs_file *f = fs_file(exp_file);
        fill_rrect(x + 14, y + 88, w - 28, h - 102, 10, 0x1E1535, 255);
        if (f && f->img) {
            draw_image_fit(f->img, x + 24, y + 98, w - 48, h - 146);
            char cap[24];
            u32 ci = 0;
            u32 vals[2] = { f->img->w, f->img->h };
            for (int q = 0; q < 2; q++) {
                char dg[6]; int dn = 0; u32 v = vals[q];
                while (v > 0) { dg[dn++] = (char)('0' + v % 10); v /= 10; }
                while (dn > 0) cap[ci++] = dg[--dn];
                if (q == 0) cap[ci++] = 'x';
            }
            const char *tail = " píxeles";
            for (u32 j = 0; tail[j]; j++) cap[ci++] = tail[j];
            cap[ci] = 0;
            draw_text(x + (w - text_width(cap, 1)) / 2, y + h - 40, cap, 0xB9A2F5, 1);
            return;
        }
        draw_wrapped(x + 28, y + 102, (w - 56) / 8, (h - 130) / 18,
                     f ? f->data : "(el archivo no existe)", 0xF0E8FF);
        return;
    }

    char name[48];
    int is_dir;
    u32 count = 0;
    for (int i = 0; i < EXP_MAX && fs_entry(exp_dir, (u32)i, name, &is_dir); i++) {
        s32 cx, cy;
        exp_cell(i, &cx, &cy);
        if (exp_sel == i) fill_rrect(cx + 4, cy + 4, EXP_CW - 8, EXP_CH - 8, 12, 0xB9A2F5, 90);
        if (exp_hover == i) fill_rrect(cx + 4, cy + 4, EXP_CW - 8, EXP_CH - 8, 12, 0xFFFFFF, 45);
        const struct img *timg = 0;
        if (!is_dir) {
            char tp[PATH_MAX];
            fs_join(exp_dir, name, tp);
            timg = fs_img(tp);
        }
        if (is_dir) {
            draw_folder_icon(cx + EXP_CW / 2, cy + 12);
        } else if (timg) {
            fill_rrect(cx + EXP_CW / 2 - 35, cy + 8, 70, 54, 8, 0x1E1535, 255);
            draw_image_fit(timg, cx + EXP_CW / 2 - 32, cy + 11, 64, 48);
        } else {
            draw_file_icon(cx + EXP_CW / 2, cy + 10);
        }

        char shown[16];
        u32 n = slen(name);
        if (n > 12) {                                   /* nombre largo: se acorta con ".." */
            for (u32 j = 0; j < 10; j++) shown[j] = name[j];
            shown[10] = '.'; shown[11] = '.'; shown[12] = 0;
        } else {
            for (u32 j = 0; j <= n; j++) shown[j] = name[j];
        }
        draw_text(cx + EXP_CW / 2 - text_width(shown, 1) / 2, cy + 70, shown, 0xFFFFFF, 1);
        count++;
    }

    if (count == 0) {
        const char *msg = "Esta carpeta está vacía";
        draw_text(x + (w - text_width(msg, 1)) / 2, y + 180, msg, 0xB9A2F5, 1);
    }

    char st[24];
    u32 si = 0;
    char digits[8];
    int dn = 0;
    u32 v = count;
    if (v == 0) digits[dn++] = '0';
    while (v > 0) { digits[dn++] = (char)('0' + v % 10); v /= 10; }
    while (dn > 0) st[si++] = digits[--dn];
    const char *unit = count == 1 ? " elemento" : " elementos";
    for (u32 j = 0; unit[j]; j++) st[si++] = unit[j];
    st[si] = 0;
    draw_text(x + 16, y + h - 26, st, 0xB9A2F5, 1);
}

static const char *const app_lines[DOCK_N][3] = {
    { 0, 0, 0 },
    { "Aquí irá el explorador de archivos.", "Necesita leer discos (FAT32).", "Mientras tanto, usa 'ls' y 'cat' en el terminal." },
    { "Aquí irá el navegador web.", "Necesita red (TCP/IP y TLS), que aún no existe.", 0 },
    { "Aquí irán los ajustes del sistema.", "Idioma del teclado, fondo y pantalla.", 0 },
    { "Más aplicaciones, poco a poco: vídeo y cámara.", "Las imágenes ya se abren desde Archivos > imagenes.", 0 },
};

static void draw_app(void)
{
    if (app_kind == 1) { draw_explorer(); return; }
    const char *name = dock_names[app_kind];
    draw_frame(app_x, app_y, app_w, app_h, name, APP_COLOR, app_top);
    draw_text(app_x + (app_w - text_width(name, 2)) / 2, app_y + 56, name, 0xFF9BD2, 2);
    for (int i = 0; i < 3; i++) {
        const char *l = app_lines[app_kind][i];
        if (l) draw_text(app_x + (app_w - text_width(l, 1)) / 2, app_y + 108 + i * 22, l, 0xF0E8FF, 1);
    }
    fill_rrect(app_x + app_w / 2 - 62, app_y + app_h - 50, 124, 26, 13, 0x7C5CD0, 255);
    draw_text(app_x + (app_w - text_width("Próximamente", 1)) / 2, app_y + app_h - 45, "Próximamente", 0xFFFFFF, 1);
}

/* Compone el escritorio entero en la memoria intermedia: fondo guardado + iconos + ventanas + dock */
static void compose(void)
{
    target(back_buf);
    for (u32 i = 0; i < (u32)fb_w * (u32)fb_h; i++)
        back_buf[i] = wall_buf[i];
    draw_desktop_icons();
    if (app_vis && !app_top) draw_app();
    if (term_vis) draw_term();
    if (app_vis && app_top) draw_app();
    draw_dock();
    draw_topbar();
}

/* Ventana 0 = terminal, 1 = aplicación */
static int win_visible(int w) { return w == 0 ? term_vis : app_vis; }

static void win_geom(int w, s32 *x, s32 *y, s32 *ww, s32 *hh)
{
    if (w == 0) { *x = win_x; *y = win_y; *ww = WIN_W; *hh = WIN_H; }
    else        { *x = app_x; *y = app_y; *ww = app_w; *hh = app_h; }
}

static void win_set_pos(int w, s32 x, s32 y)
{
    if (w == 0) { win_x = x; win_y = y; }
    else        { app_x = x; app_y = y; }
}

static void win_default(int w)
{
    if (w == 0) win_set_pos(0, (fb_w - WIN_W) / 2, 44);
    else        win_set_pos(1, (fb_w - app_w) / 2 + 60, 100);
}

static void win_hide(int w)
{
    if (w == 0) term_vis = 0;
    else        app_vis = 0;
}

static void win_raise(int w) { app_top = (w == 1); }

static int in_circle(s32 px, s32 py, s32 cx, s32 cy, s32 r)
{
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
}

static void draw_desktop(void)
{
    win_default(0);
    win_default(1);
    term_vis = 1;
    app_vis = 0;
    app_top = 0;
    dock_hover = -1;
    mouse_x = fb_w / 2;
    mouse_y = fb_h / 2;

    target(wall_buf);           /* el fondo se dibuja una sola vez y se guarda */
    draw_wallpaper();
    con_reset();
    compose();
    present();
}

/* Teclado para el editor: devuelve 1 si la tecla era para él (ventana de Archivos delante y editando) */
static int gui_key(u32 c)
{
    if (!(app_vis && app_top && app_kind == 1 && exp_editing)) return 0;
    if (c == '\b') {
        while (ed_len > 0 && (((u8)ed_buf[ed_len - 1]) & 0xC0) == 0x80) ed_len--;
        if (ed_len > 0) ed_len--;
        ed_buf[ed_len] = 0;
    } else if (c == '\n' || (c >= 32 && c < 256 && c != 127)) {
        u32 need = (c < 128) ? 1 : 2;
        if (ed_len + need > FS_TEXT_MAX) return 1;
        if (c < 128) {
            ed_buf[ed_len++] = (char)c;
        } else {
            ed_buf[ed_len++] = (char)(0xC0 | (c >> 6));
            ed_buf[ed_len++] = (char)(0x80 | (c & 0x3F));
        }
        ed_buf[ed_len] = 0;
    } else {
        return 1;
    }
    exp_err = 0;
    ed_key_tick = ticks;
    compose();
    present();
    return 1;
}

/* Se llama cuando no hay nada que hacer: hace parpadear el cursor del editor */
static void gui_tick(void)
{
    if (!(app_vis && app_top && app_kind == 1 && exp_editing)) return;
    int on = ((ticks - ed_key_tick) / 50) % 2 == 0;
    if (on != ed_shown) {
        compose();                                      /* al dibujar, ed_shown se actualiza */
        present();
    }
}

static void open_app(int kind)
{
    app_kind = kind;
    app_vis = 1;
    app_w = (kind == 1) ? 580 : 460;
    app_h = (kind == 1) ? 400 : 250;
    if (kind == 1) {
        exp_dir[0] = 0;
        exp_file[0] = 0;
        exp_hover = -1;
        exp_editing = 0;
        exp_sel = -1;
    }
    win_default(1);
    win_raise(1);
}

/* Clic en un icono del dock */
static void dock_click(int i)
{
    if (i == 0) {
        if (!term_vis)                       { term_vis = 1; win_raise(0); }
        else if (!(app_vis && app_top))      { term_vis = 0; }     /* ya estaba delante: se esconde */
        else                                 { win_raise(0); }
    } else if (app_vis && app_kind == i) {
        if (app_top) app_vis = 0;
        else         win_raise(1);
    } else {
        open_app(i);
    }
}

static int drag_on, left_before, drag_win;
static s32 drag_dx, drag_dy;

/* Movimiento del ratón (dx, dy en píxeles; botones: bit 0 = izquierdo) */
static void gui_mouse(s32 dx, s32 dy, u8 buttons)
{
    s32 nx = mouse_x + dx, ny = mouse_y + dy;
    int left = buttons & 1;
    int redraw = 0;

    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > fb_w - 1) nx = fb_w - 1;
    if (ny > fb_h - 1) ny = fb_h - 1;

    int hv = dock_hit(nx, ny);
    if (hv != dock_hover) {
        dock_hover = hv;
        redraw = 1;
    }
    int et = (app_vis && app_top) ? exp_target(nx, ny) : -1;
    if (et != exp_hover) {
        exp_hover = et;
        redraw = 1;
    }

    if (left && !left_before) {
        if (hv >= 0) {
            dock_click(hv);
            redraw = 1;
        } else {
            int consumed = 0;
            for (int k = 0; k < 2; k++) {                  /* de la ventana de arriba a la de abajo */
                int w = (k == 0) ? (app_top ? 1 : 0) : (app_top ? 0 : 1);
                s32 x, y, ww, hh;
                if (!win_visible(w)) continue;
                win_geom(w, &x, &y, &ww, &hh);
                if (nx < x || nx >= x + ww || ny < y || ny >= y + hh) continue;
                if (ny < y + TITLE_H) {
                    if (in_circle(nx, ny, x + 20, y + 17, 9) || in_circle(nx, ny, x + 40, y + 17, 9)) {
                        win_hide(w);
                    } else if (in_circle(nx, ny, x + 60, y + 17, 9)) {
                        win_default(w);
                        win_raise(w);
                    } else {
                        drag_on = 1;
                        drag_win = w;
                        drag_dx = nx - x;
                        drag_dy = ny - y;
                        win_raise(w);
                    }
                } else {
                    int was_top = (w == (app_top ? 1 : 0));
                    win_raise(w);
                    if (w == 1 && was_top) {               /* el clic llega a lo que haya dentro */
                        int t = exp_target(nx, ny);
                        if (t != -1) explorer_click(t);
                    }
                }
                consumed = 1;
                redraw = 1;
                break;
            }
            if (!consumed && nx >= 20 && nx < 76 && ny >= 48 && ny < 112) {   /* icono "Inicio" del escritorio */
                open_app(1);
                redraw = 1;
            }
        }
    }
    if (!left) drag_on = 0;
    left_before = left;

    if (drag_on) {
        s32 x, y, ww, hh;
        win_geom(drag_win, &x, &y, &ww, &hh);
        s32 wx = nx - drag_dx, wy = ny - drag_dy;
        if (wx < -(ww - 120)) wx = -(ww - 120);
        if (wx > fb_w - 120)  wx = fb_w - 120;
        if (wy < 24)          wy = 24;
        if (wy > fb_h - 40)   wy = fb_h - 40;
        if (wx != x || wy != y) {
            win_set_pos(drag_win, wx, wy);
            redraw = 1;
        }
    }

    if (redraw) {
        mouse_x = nx;
        mouse_y = ny;
        compose();
        present();
    } else {
        cursor_move_to(nx, ny);
    }
}

#endif
