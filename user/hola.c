#include "lirio.h"

__attribute__((section(".text.start")))
void _start(void)
{
    texto("Hola desde un programa de LirioOS en modo usuario!\n");
    texto("  mi pid es ");
    numero((u32)mi_pid());
    texto("\n");
    for (int i = 1; i <= 3; i++) {
        texto("  latido ");
        numero((u32)i);
        texto("\n");
        dormir(30);
    }
    salir(7);
}
