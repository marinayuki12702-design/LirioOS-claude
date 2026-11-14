# Kernel de LirioOS

Kernel propio mínimo, escrito en C y ensamblador para x86 de 32 bits. Arranca con GRUB (Multiboot).

## Qué hace hoy

- Pantalla de texto VGA con colores morados y el logo de LirioOS en ASCII.
- Teclado por consulta directa (distribución US, solo minúsculas).
- Sistema de archivos **en memoria** (ramfs) con tres archivos de ejemplo.
- Terminal con los comandos `help`, `ls`, `cat`, `echo`, `clear`, `about`, `reboot` y `halt`.

## Compilar y probar

```
make          # genera lirioos-kernel.bin
make run      # si tienes QEMU instalado
make iso      # genera LirioOS-kernel.iso (necesita grub-mkrescue y xorriso)
```

Sin instalar nada, también puedes ejecutar el flujo **Compilar kernel de LirioOS** en la pestaña Actions de GitHub y descargar `LirioOS-kernel-iso`.
Para probarlo en VirtualBox: máquina nueva "Other/Unknown", sin disco, con esa ISO en la unidad óptica.

## Hoja de ruta

1. Entradas con Shift y teclado en español.
2. Interrupciones (IDT, PIC, temporizador y teclado).
3. Memoria: mapa de memoria de Multiboot y un asignador sencillo.
4. Disco (ATA) y lectura de FAT32.
5. Procesos y modo usuario.
