#include "lirio.h"

/* Da vueltas para siempre sin dormir: el temporizador le quita el procesador y el escritorio sigue vivo.
 * Se detiene con 'matar <pid>'. */
__attribute__((section(".text.start")))
void _start(void)
{
    volatile u32 vueltas = 0;
    texto("  [bucle] dando vueltas sin parar; usa 'matar' para detenerme\n");
    for (;;) vueltas++;
}
