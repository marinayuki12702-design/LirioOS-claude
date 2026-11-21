#include "lirio.h"

/* Pide una nota por teclado, la guarda en /disco/notas.txt y la vuelve a leer del disco */
__attribute__((section(".text.start")))
void _start(void)
{
    char *nota = pedir_memoria(256);
    char *leida = pedir_memoria(256);
    u32 n = 0;

    if (!nota || !leida) { texto("sin memoria\n"); salir(1); }
    texto("Escribe una nota y pulsa Enter (Esc sale): ");
    for (;;) {
        int c = leer_tecla();
        if (c == '\n') break;
        if (c == '\b') {
            if (n > 0) { n--; texto("\b"); }
            continue;
        }
        if (c >= 32 && n < 200) { nota[n++] = (char)c; escribir(&nota[n - 1], 1); }
    }
    nota[n++] = '\n';
    texto("\n");

    if (escribir_archivo("/disco/notas.txt", nota, n) != 0) {
        texto("No pude guardar: necesitas un disco con formato (comando 'disco').\n");
        salir(1);
    }
    int k = leer_archivo("/disco/notas.txt", leida, 255);
    if (k < 0) { texto("No pude volver a leerla.\n"); salir(1); }
    leida[k] = 0;
    texto("Guardada en /disco/notas.txt y leída de nuevo del disco:\n  ");
    texto(leida);
    salir(0);
}
