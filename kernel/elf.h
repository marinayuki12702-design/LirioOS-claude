/* Lector de programas ELF de 32 bits (el formato ejecutable de Linux y de LirioOS).
 * Solo comprueba y extrae los segmentos que hay que cargar en memoria; no reserva nada. */

#ifndef LIRIO_ELF_H
#define LIRIO_ELF_H

#define ELF_MAX_SEGS 4

struct elf_seg {
    u32 off;                                    /* dónde empiezan los datos en el archivo */
    u32 vaddr;                                  /* dirección virtual donde van */
    u32 filesz;                                 /* bytes que se copian del archivo */
    u32 memsz;                                  /* bytes que ocupa en memoria (el resto se rellena con ceros) */
};

static u32 elf_u16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 elf_u32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* Comprueba el archivo. 'lo'/'hi' delimitan la zona de memoria permitida para el programa.
 * Devuelve 0 si todo está bien, o un código de error (ver elf_err_text). */
static int elf_parse(const u8 *img, u32 len, u32 lo, u32 hi, u32 *entry, struct elf_seg *seg, u32 maxseg, u32 *nseg)
{
    *nseg = 0;
    if (len < 52 || img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') return 1;
    if (img[4] != 1 || img[5] != 1) return 2;                   /* 32 bits, little endian */
    if (elf_u16(img + 16) != 2 || elf_u16(img + 18) != 3) return 2;   /* ejecutable, x86 */

    u32 e_entry = elf_u32(img + 24), phoff = elf_u32(img + 28);
    u32 phentsize = elf_u16(img + 42), phnum = elf_u16(img + 44);
    if (phentsize < 32 || phnum == 0 || phnum > 64) return 3;
    if (phoff > len || phnum * phentsize > len - phoff) return 6;

    u32 n = 0;
    for (u32 i = 0; i < phnum; i++) {
        const u8 *p = img + phoff + i * phentsize;
        if (elf_u32(p) != 1) continue;                          /* solo segmentos PT_LOAD */
        u32 off = elf_u32(p + 4), va = elf_u32(p + 8), fs = elf_u32(p + 16), ms = elf_u32(p + 20);
        if (ms == 0) continue;
        if (fs > ms) return 4;
        if (va < lo || va >= hi || ms > hi - va) return 4;
        if (off > len || fs > len - off) return 6;
        if (n == maxseg) return 3;
        seg[n].off = off; seg[n].vaddr = va; seg[n].filesz = fs; seg[n].memsz = ms;
        n++;
    }
    if (n == 0) return 3;
    int ok = 0;
    for (u32 i = 0; i < n; i++)
        if (e_entry >= seg[i].vaddr && e_entry - seg[i].vaddr < seg[i].memsz) ok = 1;
    if (!ok) return 5;
    *entry = e_entry;
    *nseg = n;
    return 0;
}

static const char *elf_err_text(int e)
{
    switch (e) {
    case 1: return "no es un programa ELF";
    case 2: return "ELF no compatible (hace falta x86 de 32 bits)";
    case 3: return "segmentos de programa no válidos";
    case 4: return "el programa usa direcciones fuera de su zona (0x40000000 a 0x403EFFFF)";
    case 5: return "punto de entrada no válido";
    case 6: return "archivo ELF incompleto";
    }
    return "error desconocido";
}

#endif
