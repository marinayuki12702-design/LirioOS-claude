#include "lirio.h"

/* Programa de pintar: arrastra el ratón para dibujar, 1-6 cambian el color, C borra, Esc sale */
static const u32 colores[6] = { 0xFF9BD2, 0xB9A2F5, 0x9BE7A8, 0xFFD479, 0x8FD3FF, 0xFFFFFF };

__attribute__((section(".text.start")))
void _start(void)
{
    int v = ventana_crear(320, 220, "Pintar");
    if (v < 0) { texto("pintar: no pude abrir la ventana\n"); salir(1); }
    u32 color = colores[0];
    struct evento e;
    int lx = -1, ly = -1;

    ventana_rect(v, 0, 0, 320, 220, 0x1B1230);
    ventana_texto(v, 8, 6, "Arrastra para pintar. 1-6 color, C borra, Esc sale", 0xB9A2F5, 1);
    for (int i = 0; i < 6; i++) ventana_rect(v, 8 + i * 26, 26, 22, 14, colores[i]);
    ventana_mostrar(v);

    for (;;) {
        int r = ventana_evento(v, &e, 1);
        if (r < 0) salir(0);
        do {
            if (e.tipo == 1) {
                if (e.a == 27) salir(0);
                if (e.a >= '1' && e.a <= '6') color = colores[e.a - '1'];
                if (e.a == 'c' || e.a == 'C') ventana_rect(v, 0, 46, 320, 174, 0x1B1230);
            } else if (e.tipo == 2) {
                if (e.c && (int)e.b > 46) {
                    int x = (int)e.a, y = (int)e.b;
                    if (lx >= 0) {                           /* une con el punto anterior para que no queden huecos */
                        int dx = x - lx, dy = y - ly;
                        int n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
                        for (int k = 0; k <= n; k++)
                            ventana_rect(v, lx + (n ? dx * k / n : 0) - 2, ly + (n ? dy * k / n : 0) - 2, 5, 5, color);
                    } else {
                        ventana_rect(v, x - 2, y - 2, 5, 5, color);
                    }
                    lx = x; ly = y;
                } else {
                    lx = -1;
                }
            }
        } while (ventana_evento(v, &e, 0) == 1);
        ventana_mostrar(v);
    }
}
