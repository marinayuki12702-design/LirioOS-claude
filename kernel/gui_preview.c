/* Vista previa del escritorio en un PC normal (sin arrancar el kernel).
 * Uso: gcc -O1 -I. gui_preview.c -o gui_preview && OUT=vista.ppm ./gui_preview */
#include <stdio.h>
#include <stdlib.h>
#include "gui.h"
int main(void){
    int W=1024,H=768;
    u32 *buf = calloc(W*H,4);
    gui_set_fb(buf, W*4, W, H, 16, 8, 0);
    draw_desktop();
    con_print("  _     _      _          ___  ____  \n", ATTR_PROMPT);
    con_print(" | |   (_)_ __(_) ___    / _ \\/ ___| \n", ATTR_PROMPT);
    con_print(" | |   | | '__| |/ _ \\  | | | \\___ \\ \n", ATTR_PROMPT);
    con_print(" | |___| | |  | | (_) | | |_| |___) |\n", ATTR_PROMPT);
    con_print(" |_____|_|_|  |_|\\___/   \\___/|____/ \n\n", ATTR_PROMPT);
    con_print("LirioOS kernel 0.2 - escribe 'ayuda' para ver los comandos\n\n", ATTR_SOFT);
    con_print("lirio> ", ATTR_PROMPT); con_print("echo ñandú ÁÉÍÓÚ ¿qué? ¡sí!\n", ATTR_NORMAL);
    con_print("ñandú ÁÉÍÓÚ ¿qué? ¡sí!\n", ATTR_NORMAL);
    con_print("lirio> ", ATTR_PROMPT);
    FILE *f=fopen(getenv("OUT"),"wb"); fprintf(f,"P6\n%d %d\n255\n",W,H);
    for(int i=0;i<W*H;i++){ u32 p=unpack(buf[i]); fputc(p>>16,f); fputc((p>>8)&255,f); fputc(p&255,f);}    
    fclose(f); return 0;
}
