#include "lirio.h"

/* Intenta escribir en la memoria del kernel: el procesador lo impide y el kernel lo mata */
__attribute__((section(".text.start")))
void _start(void)
{
    texto("Voy a escribir en la memoria del kernel...\n");
    *(volatile u32 *)0x100000 = 1234;
    texto("esto no deberia verse\n");
    salir(0);
}
