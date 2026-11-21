#include "lirio.h"

/* Cuenta hasta 6 con una pausa entre número y número: varios a la vez se mezclan */
__attribute__((section(".text.start")))
void _start(void)
{
    for (int i = 1; i <= 6; i++) {
        texto("  [contar ");
        numero((u32)mi_pid());
        texto("] ");
        numero((u32)i);
        texto("\n");
        dormir(60);
    }
    salir(0);
}
