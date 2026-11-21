/* Gestión de memoria de LirioOS:
 *   1. Mapa de memoria que entrega GRUB (qué zonas de la RAM se pueden usar).
 *   2. Asignador de páginas físicas de 4 KiB (un bit por página).
 *   3. Montón del kernel: kmalloc / kfree para reservar y liberar trozos de cualquier tamaño. */

#ifndef LIRIO_MEM_H
#define LIRIO_MEM_H

#define PAGE_SIZE   4096u
#define PMM_PAGES   (1u << 20)                 /* 4 GiB de direcciones, 1 M de páginas */
#define PMM_LIMIT   (1u << 18)                 /* solo se usa el primer GiB: lo de encima es de los programas y de los dispositivos */

/* ---------- Mapa de memoria de Multiboot ---------- */

struct mb_mmap_entry {
    u32 size;                                   /* bytes que siguen a este campo */
    u32 addr_lo, addr_hi;
    u32 len_lo, len_hi;
    u32 type;                                   /* 1 = libre, 2 = reservada, 3 = ACPI, 4 = ACPI NVS, 5 = defectuosa */
} __attribute__((packed));

struct mem_region {
    u32 start;
    u32 end;                                    /* última dirección incluida */
    u32 type;
};

#define MEM_REGIONS_MAX 16
static struct mem_region mem_regions[MEM_REGIONS_MAX];
static u32 mem_region_n;

/* ---------- Páginas físicas ---------- */

static u32 pmm_bits[PMM_PAGES / 32];            /* 1 = ocupada o inexistente, 0 = libre */
static u32 pmm_total;                           /* páginas de RAM utilizable detectadas */
static u32 pmm_free;                            /* de ellas, cuántas están libres */

static int pmm_used(u32 p) { return (pmm_bits[p >> 5] >> (p & 31)) & 1; }

static void pmm_mark_free(u32 p)
{
    if (pmm_used(p)) { pmm_bits[p >> 5] &= ~(1u << (p & 31)); pmm_free++; }
}

static void pmm_mark_used(u32 p)
{
    if (!pmm_used(p)) { pmm_bits[p >> 5] |= 1u << (p & 31); pmm_free--; }
}

/* Reserva las páginas que tocan el rango [start, end) */
static void pmm_reserve(u32 start, u32 end)
{
    if (end <= start) return;
    u32 first = start / PAGE_SIZE;
    u32 last = (end - 1) / PAGE_SIZE;
    for (u32 p = first; p <= last && p < PMM_PAGES; p++) pmm_mark_used(p);
}

/* Reserva una sola página libre (por encima de 1 MiB). Devuelve su dirección, o 0 si no queda ninguna. */
__attribute__((unused)) static u32 pmm_alloc_page(void)
{
    for (u32 p = 256; p < PMM_PAGES; p++)
        if (!pmm_used(p)) { pmm_mark_used(p); return p * PAGE_SIZE; }
    return 0;
}

/* Reserva 'n' páginas seguidas. Devuelve la dirección de la primera, o 0 si no hay un hueco así. */
__attribute__((unused)) static u32 pmm_alloc_run(u32 n)
{
    u32 run = 0;
    for (u32 p = 256; p < PMM_PAGES; p++) {
        if (pmm_used(p)) { run = 0; continue; }
        if (++run == n) {
            u32 first = p + 1 - n;
            for (u32 q = first; q <= p; q++) pmm_mark_used(q);
            return first * PAGE_SIZE;
        }
    }
    return 0;
}

__attribute__((unused)) static void pmm_free_page(u32 addr)
{
    pmm_mark_free(addr / PAGE_SIZE);
}

/* 'mmap_addr'/'mmap_len': tabla de GRUB (si 'has_mmap'); si no, se usa 'upper_kb' (memoria sobre 1 MiB).
 * 'reserve_end': todo lo que hay por debajo (los primeros MiB y el propio kernel) queda reservado.
 * 'info': dirección de la estructura de Multiboot, que también hay que respetar. */
static void mem_init(int has_mmap, u32 mmap_addr, u32 mmap_len, u32 upper_kb, u32 reserve_end, u32 info)
{
    for (u32 i = 0; i < PMM_PAGES / 32; i++) pmm_bits[i] = 0xFFFFFFFFu;
    pmm_total = 0;
    pmm_free = 0;
    mem_region_n = 0;

    if (has_mmap) {
        u32 off = 0;
        while (off + sizeof(struct mb_mmap_entry) <= mmap_len) {
            const struct mb_mmap_entry *e = (const struct mb_mmap_entry *)(mmap_addr + off);
            off += e->size + 4;
            if (e->addr_hi != 0) continue;                         /* por encima de 4 GiB: no se usa */
            u32 start = e->addr_lo;
            u32 len = e->len_lo;
            if (e->len_hi != 0 || len > 0xFFFFFFFFu - start) len = 0xFFFFFFFFu - start;
            if (len == 0) continue;
            if (mem_region_n < MEM_REGIONS_MAX) {
                mem_regions[mem_region_n].start = start;
                mem_regions[mem_region_n].end = start + len - 1;
                mem_regions[mem_region_n].type = e->type;
                mem_region_n++;
            }
            if (e->type != 1) continue;
            u32 first = (start + PAGE_SIZE - 1) / PAGE_SIZE;         /* solo páginas enteras */
            u32 last_excl = (start + len) / PAGE_SIZE;
            if (start + len < start) last_excl = PMM_LIMIT;
            for (u32 p = first; p < last_excl && p < PMM_LIMIT; p++) {
                if (pmm_used(p)) { pmm_bits[p >> 5] &= ~(1u << (p & 31)); pmm_free++; pmm_total++; }
            }
        }
    } else {
        u32 first = 256, last_excl = 256 + upper_kb / 4;
        mem_regions[0].start = 0x100000;
        mem_regions[0].end = 0x100000 + upper_kb * 1024u - 1;
        mem_regions[0].type = 1;
        mem_region_n = 1;
        for (u32 p = first; p < last_excl && p < PMM_LIMIT; p++) {
            pmm_bits[p >> 5] &= ~(1u << (p & 31));
            pmm_free++;
            pmm_total++;
        }
    }

    pmm_reserve(0, reserve_end);                                  /* BIOS, primeros MiB y el kernel */
    pmm_reserve(info, info + 128);                                /* estructura de Multiboot */
    if (has_mmap) pmm_reserve(mmap_addr, mmap_addr + mmap_len);   /* y la tabla del mapa */
}

/* ---------- Montón del kernel ---------- */

struct hblk {
    u32 size;                                   /* bytes de datos (múltiplo de 8), sin contar esta cabecera */
    u32 used;                                   /* 1 = en uso, 0 = libre */
};

#define HBLK_SIZE ((u32)sizeof(struct hblk))

static u8 *heap_base;
static u32 heap_size;

static void heap_init(void *base, u32 size)
{
    heap_base = (u8 *)base;
    heap_size = size & ~7u;
    struct hblk *b = (struct hblk *)heap_base;
    b->size = heap_size - HBLK_SIZE;
    b->used = 0;
}

static void *kmalloc(u32 n)
{
    if (!heap_base) return 0;
    n = (n + 7u) & ~7u;
    if (n == 0) n = 8;
    u32 off = 0;
    while (off + HBLK_SIZE <= heap_size) {
        struct hblk *b = (struct hblk *)(heap_base + off);
        if (!b->used && b->size >= n) {
            if (b->size >= n + HBLK_SIZE + 8) {                   /* sobra: se parte en dos bloques */
                struct hblk *rest = (struct hblk *)(heap_base + off + HBLK_SIZE + n);
                rest->size = b->size - n - HBLK_SIZE;
                rest->used = 0;
                b->size = n;
            }
            b->used = 1;
            return heap_base + off + HBLK_SIZE;
        }
        off += HBLK_SIZE + b->size;
    }
    return 0;
}

static void kfree(void *p)
{
    u8 *a = (u8 *)p;
    if (!p || a < heap_base + HBLK_SIZE || a >= heap_base + heap_size) return;
    struct hblk *b = (struct hblk *)(a - HBLK_SIZE);
    if (!b->used) return;                                         /* doble liberación: se ignora */
    b->used = 0;

    u32 off = 0;                                                  /* une los bloques libres vecinos */
    while (off + HBLK_SIZE <= heap_size) {
        struct hblk *c = (struct hblk *)(heap_base + off);
        u32 next = off + HBLK_SIZE + c->size;
        if (!c->used && next + HBLK_SIZE <= heap_size) {
            struct hblk *d = (struct hblk *)(heap_base + next);
            if (!d->used) { c->size += HBLK_SIZE + d->size; continue; }
        }
        off = next;
    }
}

/* Estadísticas del montón: bytes en uso, bytes libres y número de bloques */
static void heap_stats(u32 *used, u32 *free_bytes, u32 *blocks)
{
    *used = *free_bytes = *blocks = 0;
    u32 off = 0;
    while (heap_base && off + HBLK_SIZE <= heap_size) {
        struct hblk *b = (struct hblk *)(heap_base + off);
        if (b->used) *used += b->size; else *free_bytes += b->size;
        (*blocks)++;
        off += HBLK_SIZE + b->size;
    }
}

#endif
