#include "lirio.h"

/* Reloj de tiempo encendido con una barra que avanza; se actualiza solo cada segundo */
static void cifras(char *s, u32 v)
{
    s[0] = (char)('0' + v / 10 % 10);
    s[1] = (char)('0' + v % 10);
}

__attribute__((section(".text.start")))
void _start(void)
{
    int v = ventana_crear(240, 96, "Reloj");
    if (v < 0) { texto("reloj: no pude abrir la ventana\n"); salir(1); }
    struct evento e;
    for (;;) {
        u32 s = ticks() / 100;
        char hora[9] = "00:00:00";
        cifras(hora, s / 3600);
        cifras(hora + 3, s / 60 % 60);
        cifras(hora + 6, s % 60);
        ventana_rect(v, 0, 0, 240, 96, 0x1B1230);
        ventana_texto(v, 54, 12, hora, 0xFF9BD2, 2);
        ventana_texto(v, 20, 48, "tiempo encendido", 0xB9A2F5, 1);
        ventana_rect(v, 20, 72, 200, 8, 0x3A2A66);
        ventana_rect(v, 20, 72, (int)(s % 60) * 200 / 59, 8, 0x9BE7A8);
        ventana_mostrar(v);
        for (int i = 0; i < 20; i++) {                       /* dormir en trozos por si se cierra la ventana */
            if (ventana_evento(v, &e, 0) < 0) salir(0);
            dormir(5);
        }
    }
}
