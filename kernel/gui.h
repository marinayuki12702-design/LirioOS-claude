/* Interfaz gráfica de LirioOS: framebuffer lineal de 32 bits, escritorio morado, dock y terminal en ventana.
 * No toca puertos ni hardware: solo dibuja en memoria, por eso se puede probar también en un PC normal. */

#ifndef LIRIO_GUI_H
#define LIRIO_GUI_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

#include "font.h"

#define NO_BG 0xFFFFFFFFu

/* ---------- Framebuffer ---------- */

static volatile u32 *fb;
static u32 fb_pitch;            /* en píxeles de 32 bits */
static s32 fb_w, fb_h;
static u8  pos_r = 16, pos_g = 8, pos_b = 0;

static void gui_set_fb(void *addr, u32 pitch_bytes, s32 w, s32 h, u8 rpos, u8 gpos, u8 bpos)
{
    fb = (volatile u32 *)addr;
    fb_pitch = pitch_bytes / 4;
    fb_w = w;
    fb_h = h;
    pos_r = rpos;
    pos_g = gpos;
    pos_b = bpos;
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

static void draw_dock(void)
{
    const s32 n = 5, size = 52, gap = 14, pad = 16;
    s32 w = n * size + (n - 1) * gap + 2 * pad;
    s32 h = size + 24;
    s32 x0 = (fb_w - w) / 2;
    s32 y0 = fb_h - h - 14;

    fill_rrect(x0, y0 + 4, w, h, 20, 0x2E2150, 40);   /* sombra suave */
    fill_rrect(x0, y0, w, h, 20, 0xFFFFFF, 120);

    for (s32 i = 0; i < n; i++) {
        s32 ix = x0 + pad + i * (size + gap);
        s32 iy = y0 + 12;
        s32 cx = ix + size / 2, cy = iy + size / 2;
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
    }
    fill_circle(x0 + pad + size / 2, y0 + h - 7, 2, 0x5F3DBA, 255);   /* aplicación abierta */
}

/* ---------- Ventana de terminal ---------- */

#define CON_COLS 96
#define CON_ROWS 26

#define WIN_W 800
#define WIN_H 480
#define BODY_COLOR 0x241A3F

static s32 win_x, win_y;
static s32 con_px0, con_py0;
static u8  con_ch[CON_ROWS][CON_COLS];
static u8  con_at[CON_ROWS][CON_COLS];
static s32 con_x, con_y;

static const u32 con_palette[3] = { 0xF0E8FF, 0xFF9BD2, 0xB9A2F5 };

#define ATTR_NORMAL 0
#define ATTR_PROMPT 1
#define ATTR_SOFT   2

static void draw_window(void)
{
    win_x = (fb_w - WIN_W) / 2;
    win_y = 44;

    for (s32 i = 1; i <= 4; i++)
        fill_rrect(win_x - i, win_y - i + 8, WIN_W + 2 * i, WIN_H + 2 * i, 12 + i, 0x2E2150, 14);

    fill_rrect(win_x, win_y, WIN_W, WIN_H, 12, BODY_COLOR, 255);
    fill_rrect(win_x, win_y, WIN_W, 34, 12, 0x7C5CD0, 255);
    fill_rect(win_x, win_y + 20, WIN_W, 14, 0x7C5CD0, 255);

    fill_circle(win_x + 20, win_y + 17, 6, 0xFF8FB1, 255);
    fill_circle(win_x + 40, win_y + 17, 6, 0xFFD479, 255);
    fill_circle(win_x + 60, win_y + 17, 6, 0x9BE7A8, 255);

    const char *title = "lirio@lirioos: terminal";
    draw_text(win_x + (WIN_W - text_width(title, 1)) / 2, win_y + 9, title, 0xFFFFFF, 1);

    con_px0 = win_x + 14;
    con_py0 = win_y + 34 + 10;
}

static void con_draw_cell(s32 r, s32 c)
{
    draw_glyph(con_px0 + c * 8, con_py0 + r * 16, con_ch[r][c], con_palette[con_at[r][c]], BODY_COLOR, 1);
}

static void con_cursor(int on)
{
    if (on) fill_rect(con_px0 + con_x * 8, con_py0 + con_y * 16 + 14, 8, 2, 0xFF9BD2, 255);
    else    con_draw_cell(con_y, con_x);
}

static void con_redraw(void)
{
    for (s32 r = 0; r < CON_ROWS; r++)
        for (s32 c = 0; c < CON_COLS; c++)
            con_draw_cell(r, c);
}

static void con_clear(void)
{
    for (s32 r = 0; r < CON_ROWS; r++)
        for (s32 c = 0; c < CON_COLS; c++) {
            con_ch[r][c] = ' ';
            con_at[r][c] = ATTR_NORMAL;
        }
    con_x = 0;
    con_y = 0;
    con_redraw();
    con_cursor(1);
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
            con_draw_cell(con_y, con_x);
        }
    } else {
        con_ch[con_y][con_x] = (u8)ch;
        con_at[con_y][con_x] = attr;
        con_draw_cell(con_y, con_x);
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

static void draw_desktop(void)
{
    draw_wallpaper();
    draw_desktop_icons();
    draw_window();
    draw_dock();
    draw_topbar();
    con_clear();
}

#endif
