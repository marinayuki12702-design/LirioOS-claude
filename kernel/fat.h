/* Sistema de archivos FAT32 de LirioOS: montar, listar, leer, escribir, crear carpetas, borrar y formatear.
 *
 * Límites (a propósito, para que sea sencillo y seguro):
 *   - sectores de 512 bytes y nombres cortos 8.3 (los nombres largos de otros sistemas se ven con su nombre corto);
 *   - los nombres que se crean usan A-Z, 0-9 y unos pocos signos, en mayúsculas en el disco;
 *   - cada escritura busca un grupo libre en toda la tabla FAT y lo encadena; no hay caché de escritura:
 *     cada cambio llega al disco antes de volver. */

#ifndef LIRIO_FAT_H
#define LIRIO_FAT_H

#include "ata.h"

static int  fat_ready;                  /* 1 = hay un volumen FAT32 montado */
static u32  fat_part;                   /* sector donde empieza el volumen */
static u32  fat_spc, fat_res, fat_nfats, fat_fatsz, fat_root, fat_data, fat_clusters;
static char fat_label[12];
static u16  fat_date = 0x5D72;          /* 2026-11-18; el kernel lo actualiza con el reloj al arrancar */
static u16  fat_time;
static void (*fat_clock)(void);         /* si existe, el kernel la llama antes de escribir para poner la hora */
static void (*fat_clock)(void);         /* si existe, pone fat_date y fat_time con la hora actual */

#define FAT_MASK 0x0FFFFFFFu
#define FAT_EOC  0x0FFFFFF8u            /* desde aquí hacia arriba = fin de cadena */

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static void fat_zero(void *p, u32 n) { u8 *b = (u8 *)p; for (u32 i = 0; i < n; i++) b[i] = 0; }
static int fat_eq11(const u8 *a, const u8 *b) { for (int i = 0; i < 11; i++) if (a[i] != b[i]) return 0; return 1; }

static u32 fat_lba(u32 cluster) { return fat_data + (cluster - 2) * fat_spc; }

/* ---------- Tabla FAT ---------- */

static u32 fc_lba = 0xFFFFFFFFu;        /* sector de la FAT guardado en la caché de lectura */
static u8  fc_buf[512];
static u32 fat_hint = 2;                /* por dónde seguir buscando clusters libres */

static u32 fat_get(u32 c)
{
    u32 lba = fat_part + fat_res + c / 128;
    if (lba != fc_lba) {
        if (!disk_read(lba, fc_buf)) { fc_lba = 0xFFFFFFFFu; return FAT_MASK; }
        fc_lba = lba;
    }
    return rd32(fc_buf + (c % 128) * 4) & FAT_MASK;
}

/* Cuántos clusters libres quedan (recorre toda la tabla) */
static u32 fat_count_free(void)
{
    u32 n = 0;
    for (u32 c = 2; c < fat_clusters + 2; c++) if (fat_get(c) == 0) n++;
    return n;
}

/* Contador de clusters libres que el sistema guarda en el sector FSInfo (otros sistemas lo leen para
 * saber cuánto espacio queda sin recorrer toda la FAT). Se calcula al primer cambio y se mantiene al día. */
static u32 fat_free_cnt;
static int fat_free_known;
static u32 fat_fsinfo;                  /* sector del FSInfo, 0 si no hay */

static void fat_need_count(void)
{
    if (!fat_free_known) { fat_free_cnt = fat_count_free(); fat_free_known = 1; }
}

static void fat_fsinfo_sync(void)
{
    u8 sec[512];
    if (!fat_fsinfo || !fat_free_known || !disk_read(fat_fsinfo, sec)) return;
    if (rd32(sec) != 0x41615252u || rd32(sec + 484) != 0x61417272u) return;
    wr32(sec + 488, fat_free_cnt);
    wr32(sec + 492, fat_hint >= 2 ? fat_hint : 0xFFFFFFFFu);
    disk_write(fat_fsinfo, sec);
}

static int fat_put(u32 c, u32 v)
{
    u8 sec[512];
    fc_lba = 0xFFFFFFFFu;
    for (u32 f = 0; f < fat_nfats; f++) {
        u32 lba = fat_part + fat_res + f * fat_fatsz + c / 128;
        if (!disk_read(lba, sec)) return 0;
        u32 old = rd32(sec + (c % 128) * 4);
        wr32(sec + (c % 128) * 4, (old & 0xF0000000u) | (v & FAT_MASK));
        if (!disk_write(lba, sec)) return 0;
    }
    return 1;
}

/* Reserva un cluster libre y lo marca como final de cadena. Devuelve 0 si el disco está lleno. */
static u32 fat_alloc(void)
{
    u32 last = fat_clusters + 2;
    for (u32 n = 0; n < fat_clusters; n++) {
        u32 c = fat_hint + n;
        if (c >= last) c = 2 + (c - last);
        if (fat_get(c) == 0) {
            if (!fat_put(c, FAT_MASK)) return 0;
            fat_hint = c + 1;
            if (fat_free_known) fat_free_cnt--;
            return c;
        }
    }
    return 0;
}

static void fat_free_chain(u32 c)
{
    u32 guard = 0;
    while (c >= 2 && c < FAT_EOC && guard++ < 0x10000000u) {
        u32 n = fat_get(c);
        if (fat_put(c, 0) && fat_free_known) fat_free_cnt++;
        c = n;
    }
}

static int fat_zero_cluster(u32 c)
{
    u8 z[512];
    fat_zero(z, 512);
    for (u32 s = 0; s < fat_spc; s++)
        if (!disk_write(fat_lba(c) + s, z)) return 0;
    return 1;
}

/* ---------- Montar ---------- */

static int fat_parse_bpb(u32 lba, const u8 *s)
{
    if (rd16(s + 510) != 0xAA55) return 0;
    u32 bps = rd16(s + 11), spc = s[13], res = rd16(s + 14), nf = s[16];
    u32 rootent = rd16(s + 17), fatsz16 = rd16(s + 22), fatsz32 = rd32(s + 36);
    u32 tot = rd32(s + 32) ? rd32(s + 32) : rd16(s + 19);
    u32 rootc = rd32(s + 44);
    if (bps != 512 || spc == 0 || (spc & (spc - 1)) || res == 0 || nf == 0) return 0;
    if (rootent != 0 || fatsz16 != 0 || fatsz32 == 0 || rootc < 2) return 0;
    if (tot < res + nf * fatsz32) return 0;
    fat_part = lba;
    fat_spc = spc;
    fat_res = res;
    fat_nfats = nf;
    fat_fatsz = fatsz32;
    fat_root = rootc;
    fat_data = lba + res + nf * fatsz32;
    fat_clusters = (tot - res - nf * fatsz32) / spc;
    for (int i = 0; i < 11; i++) fat_label[i] = (char)s[71 + i];
    fat_label[11] = 0;
    for (int i = 10; i >= 0 && fat_label[i] == ' '; i--) fat_label[i] = 0;
    fat_hint = 2;
    fc_lba = 0xFFFFFFFFu;
    fat_free_known = 0;
    fat_fsinfo = rd16(s + 48) ? lba + rd16(s + 48) : 0;
    return 1;
}

static int fat_mount(void)
{
    u8 s[512], s2[512];
    fat_ready = 0;
    if (!disk_present || !disk_read(0, s)) return 0;
    if (fat_parse_bpb(0, s)) { fat_ready = 1; return 1; }       /* disco sin tabla de particiones */
    if (rd16(s + 510) == 0xAA55) {                                /* con tabla de particiones (MBR) */
        for (int i = 0; i < 4; i++) {
            const u8 *p = s + 446 + i * 16;
            if (p[4] != 0x0B && p[4] != 0x0C) continue;
            u32 lba = rd32(p + 8);
            if (lba && disk_read(lba, s2) && fat_parse_bpb(lba, s2)) { fat_ready = 1; return 1; }
        }
    }
    return 0;
}

/* ---------- Nombres 8.3 ---------- */

/* "nuevo.txt" -> "NUEVO   TXT". Devuelve 0 si el nombre no cabe o tiene signos no permitidos. */
static int fat_make83(const char *s, u8 *out)
{
    int base = 0, ext = 0, dot = 0;
    for (int i = 0; i < 11; i++) out[i] = ' ';
    for (u32 i = 0; s[i]; i++) {
        char ch = s[i];
        if (ch == '.') {
            if (dot || base == 0) return 0;
            dot = 1;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        int ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        for (const char *q = "$%'-_@~!(){}^#&"; *q && !ok; q++) if (*q == ch) ok = 1;
        if (!ok) return 0;
        if (!dot) { if (base >= 8) return 0; out[base++] = (u8)ch; }
        else      { if (ext >= 3) return 0; out[8 + ext++] = (u8)ch; }
    }
    return base > 0;
}

/* Nombre corto del disco -> "nuevo.txt" para mostrarlo */
static void fat_show83(const u8 *d, char *out)
{
    u32 o = 0;
    for (int i = 0; i < 8 && d[i] != ' '; i++) out[o++] = (char)((d[i] >= 'A' && d[i] <= 'Z') ? d[i] + 32 : d[i]);
    if (d[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && d[i] != ' '; i++) out[o++] = (char)((d[i] >= 'A' && d[i] <= 'Z') ? d[i] + 32 : d[i]);
    }
    out[o] = 0;
}

/* ---------- Directorios ---------- */

static u32 fat_ent_cluster(const u8 *d) { return ((u32)rd16(d + 20) << 16) | rd16(d + 26); }

/* Busca 'name11' en el directorio que empieza en el cluster 'dir'. Si está, copia su entrada (32 bytes)
 * en 'e' y apunta con elba/eoff al sitio donde vive. */
static int fat_find(u32 dir, const u8 *name11, u8 *e, u32 *elba, u32 *eoff)
{
    u8 sec[512];
    u32 c = dir, guard = 0;
    while (c >= 2 && c < FAT_EOC && guard++ < 0x1000000u) {
        for (u32 s = 0; s < fat_spc; s++) {
            u32 lba = fat_lba(c) + s;
            if (!disk_read(lba, sec)) return 0;
            for (u32 o = 0; o < 512; o += 32) {
                const u8 *d = sec + o;
                if (d[0] == 0) return 0;                                  /* fin del directorio */
                if (d[0] == 0xE5 || d[11] == 0x0F || (d[11] & 0x08)) continue;
                if (fat_eq11(d, name11)) {
                    for (int i = 0; i < 32; i++) e[i] = d[i];
                    *elba = lba;
                    *eoff = o;
                    return 1;
                }
            }
        }
        c = fat_get(c);
    }
    return 0;
}

/* Busca un hueco para una entrada nueva; si el directorio está lleno, lo alarga con otro cluster. */
static int fat_slot(u32 dir, u32 *elba, u32 *eoff)
{
    u8 sec[512];
    u32 c = dir, last = dir, guard = 0;
    while (c >= 2 && c < FAT_EOC && guard++ < 0x1000000u) {
        for (u32 s = 0; s < fat_spc; s++) {
            u32 lba = fat_lba(c) + s;
            if (!disk_read(lba, sec)) return 0;
            for (u32 o = 0; o < 512; o += 32)
                if (sec[o] == 0 || sec[o] == 0xE5) { *elba = lba; *eoff = o; return 1; }
        }
        last = c;
        c = fat_get(c);
    }
    u32 n = fat_alloc();
    if (!n) return 0;
    if (!fat_zero_cluster(n) || !fat_put(last, n)) return 0;
    *elba = fat_lba(n);
    *eoff = 0;
    return 1;
}

static int fat_put_entry(u32 lba, u32 off, const u8 *name11, u32 attr, u32 cluster, u32 size)
{
    u8 sec[512];
    if (fat_clock) fat_clock();
    if (!disk_read(lba, sec)) return 0;
    u8 *d = sec + off;
    fat_zero(d, 32);
    for (int i = 0; i < 11; i++) d[i] = name11[i];
    d[11] = (u8)attr;
    wr16(d + 14, fat_time);                                   /* creación */
    wr16(d + 16, fat_date);
    wr16(d + 18, fat_date);                                   /* último acceso */
    wr16(d + 20, cluster >> 16);
    wr16(d + 22, fat_time);                                   /* modificación */
    wr16(d + 24, fat_date);
    wr16(d + 26, cluster & 0xFFFF);
    wr32(d + 28, size);
    return disk_write(lba, sec);
}

/* Sigue una ruta de carpetas ("a/b") desde la raíz y devuelve el cluster de la última */
static int fat_walk(const char *rel, u32 *cluster)
{
    u32 c = fat_root;
    u32 i = 0;
    while (rel[i]) {
        char comp[16];
        u32 n = 0;
        while (rel[i] == '/') i++;
        if (!rel[i]) break;
        while (rel[i] && rel[i] != '/') { if (n >= 15) return 0; comp[n++] = rel[i++]; }
        comp[n] = 0;
        u8 name[11], e[32];
        u32 lba, off;
        if (!fat_make83(comp, name) || !fat_find(c, name, e, &lba, &off)) return 0;
        if (!(e[11] & 0x10)) return 0;
        c = fat_ent_cluster(e);
        if (c == 0) c = fat_root;
    }
    *cluster = c;
    return 1;
}

/* "a/b/c.txt" -> carpeta "a/b" y nombre "c.txt" */
static int fat_split(const char *rel, char *dir, char *leaf)
{
    u32 n = slen(rel);
    while (n > 0 && rel[n - 1] == '/') n--;
    u32 cut = n;
    while (cut > 0 && rel[cut - 1] != '/') cut--;
    if (n == cut || n - cut > 15) return 0;
    u32 k = 0;
    for (u32 i = cut; i < n; i++) leaf[k++] = rel[i];
    leaf[k] = 0;
    u32 dn = cut > 0 ? cut - 1 : 0;
    for (u32 i = 0; i < dn; i++) dir[i] = rel[i];
    dir[dn] = 0;
    return 1;
}

/* ---------- Listado (se guarda una copia para no leer el disco en cada repintado) ---------- */

struct fat_ent {
    char name[13];
    u8   is_dir;
    u32  size;
    u32  cluster;
};

#define FAT_ENTS 128
static struct fat_ent fat_ents[FAT_ENTS];
static u32  fat_ents_n;
static char fat_ents_path[PATH_MAX];
static int  fat_ents_ok;

static void fat_forget(void) { fat_ents_ok = 0; }

static int fat_list(const char *rel)
{
    if (!fat_ready) return 0;
    if (fat_ents_ok && str_eq(fat_ents_path, rel)) return 1;
    u32 dir;
    if (!fat_walk(rel, &dir)) return 0;

    fat_ents_n = 0;
    for (int pass = 0; pass < 2; pass++) {                    /* primero carpetas y luego archivos */
        u8 sec[512];
        u32 c = dir, guard = 0;
        int end = 0;
        while (!end && c >= 2 && c < FAT_EOC && guard++ < 0x1000000u) {
            for (u32 s = 0; s < fat_spc && !end; s++) {
                if (!disk_read(fat_lba(c) + s, sec)) return 0;
                for (u32 o = 0; o < 512; o += 32) {
                    const u8 *d = sec + o;
                    if (d[0] == 0) { end = 1; break; }
                    if (d[0] == 0xE5 || d[0] == '.' || d[11] == 0x0F || (d[11] & 0x08)) continue;
                    int isd = (d[11] & 0x10) != 0;
                    if (isd != (pass == 0) || fat_ents_n >= FAT_ENTS) continue;
                    struct fat_ent *en = &fat_ents[fat_ents_n++];
                    fat_show83(d, en->name);
                    en->is_dir = (u8)isd;
                    en->size = rd32(d + 28);
                    en->cluster = fat_ent_cluster(d);
                }
            }
            if (!end) c = fat_get(c);
        }
    }
    u32 k = 0;
    while (rel[k] && k < PATH_MAX - 1) { fat_ents_path[k] = rel[k]; k++; }
    fat_ents_path[k] = 0;
    fat_ents_ok = 1;
    return 1;
}

static int fat_entry(const char *rel, u32 idx, char *name, int *is_dir)
{
    if (!fat_list(rel) || idx >= fat_ents_n) return 0;
    u32 k = 0;
    while (fat_ents[idx].name[k] && k < 47) { name[k] = fat_ents[idx].name[k]; k++; }
    name[k] = 0;
    *is_dir = fat_ents[idx].is_dir;
    return 1;
}

static int fat_is_dir(const char *rel)
{
    u32 c;
    return fat_ready && fat_walk(rel, &c);
}

/* ---------- Leer y escribir archivos ---------- */

/* Lee hasta 'max' bytes del archivo y los deja en buf. Devuelve 1 si existe y es un archivo. */
static int fat_read_file(const char *rel, u8 *buf, u32 max, u32 *len)
{
    char dir[PATH_MAX], leaf[16];
    u32 dc, lba, off;
    u8 name[11], e[32], sec[512];
    if (!fat_ready || !fat_split(rel, dir, leaf) || !fat_walk(dir, &dc)) return 0;
    if (!fat_make83(leaf, name) || !fat_find(dc, name, e, &lba, &off) || (e[11] & 0x10)) return 0;

    u32 size = rd32(e + 28), c = fat_ent_cluster(e), got = 0, guard = 0;
    if (size > max) size = max;
    while (got < size && c >= 2 && c < FAT_EOC && guard++ < 0x1000000u) {
        for (u32 s = 0; s < fat_spc && got < size; s++) {
            if (!disk_read(fat_lba(c) + s, sec)) return 0;
            u32 n = size - got < 512 ? size - got : 512;
            for (u32 i = 0; i < n; i++) buf[got + i] = sec[i];
            got += n;
        }
        c = fat_get(c);
    }
    *len = got;
    return 1;
}

/* (las tres operaciones que cambian el disco llevan después la cuenta de clusters libres al FSInfo) */
/* Crea o reemplaza un archivo. Primero se escribe lo nuevo y solo después se libera lo viejo. */
static int fat_write_file_inner(const char *rel, const u8 *data, u32 len)
{
    if (fat_ready) fat_need_count();
    char dir[PATH_MAX], leaf[16];
    u32 dc, lba = 0, off = 0;
    u8 name[11], e[32], sec[512];
    if (!fat_ready || !fat_split(rel, dir, leaf) || !fat_walk(dir, &dc) || !fat_make83(leaf, name)) return 0;
    if (fat_clock) fat_clock();
    int exists = fat_find(dc, name, e, &lba, &off);
    if (exists && (e[11] & 0x10)) return 0;
    u32 old = exists ? fat_ent_cluster(e) : 0;

    u32 per = 512 * fat_spc;
    u32 need = (len + per - 1) / per;
    u32 first = 0, prev = 0;
    for (u32 i = 0; i < need; i++) {
        u32 n = fat_alloc();
        if (!n) { if (first) fat_free_chain(first); return 0; }
        if (prev) { if (!fat_put(prev, n)) return 0; } else first = n;
        prev = n;
    }

    u32 c = first, done = 0;
    while (c >= 2 && c < FAT_EOC) {
        for (u32 s = 0; s < fat_spc; s++) {
            fat_zero(sec, 512);
            u32 n = (len - done) < 512 ? len - done : 512;
            if (done < len) for (u32 i = 0; i < n; i++) sec[i] = data[done + i];
            done += n;
            if (!disk_write(fat_lba(c) + s, sec)) return 0;
        }
        c = fat_get(c);
    }

    if (!exists && !fat_slot(dc, &lba, &off)) { if (first) fat_free_chain(first); return 0; }
    if (!fat_put_entry(lba, off, name, 0x20, first, len)) return 0;
    if (old) fat_free_chain(old);
    fat_forget();
    return 1;
}

static int fat_mkdir_inner(const char *rel)
{
    if (fat_ready) fat_need_count();
    char dir[PATH_MAX], leaf[16];
    u32 dc, lba, off;
    u8 name[11], e[32], sec[512];
    if (!fat_ready || !fat_split(rel, dir, leaf) || !fat_walk(dir, &dc) || !fat_make83(leaf, name)) return 0;
    if (fat_find(dc, name, e, &lba, &off)) return 0;
    if (fat_clock) fat_clock();
    u32 n = fat_alloc();
    if (!n) return 0;
    if (!fat_zero_cluster(n)) return 0;

    if (fat_clock) fat_clock();
    fat_zero(sec, 512);                                      /* "." y ".." */
    for (int i = 0; i < 11; i++) { sec[i] = ' '; sec[32 + i] = ' '; }
    sec[0] = '.'; sec[32] = '.'; sec[33] = '.';
    sec[11] = sec[43] = 0x10;
    wr16(sec + 20, n >> 16);  wr16(sec + 26, n & 0xFFFF);
    u32 parent = (dc == fat_root) ? 0 : dc;
    wr16(sec + 52, parent >> 16);  wr16(sec + 58, parent & 0xFFFF);
    for (int k = 0; k < 2; k++) { wr16(sec + k * 32 + 22, fat_time); wr16(sec + k * 32 + 24, fat_date); }
    if (!disk_write(fat_lba(n), sec)) return 0;

    if (!fat_slot(dc, &lba, &off) || !fat_put_entry(lba, off, name, 0x10, n, 0)) { fat_free_chain(n); return 0; }
    fat_forget();
    return 1;
}

/* Borra un archivo o una carpeta vacía */
static int fat_remove_inner(const char *rel)
{
    if (fat_ready) fat_need_count();
    char dir[PATH_MAX], leaf[16];
    u32 dc, lba, off;
    u8 name[11], e[32], sec[512];
    if (!fat_ready || !fat_split(rel, dir, leaf) || !fat_walk(dir, &dc) || !fat_make83(leaf, name)) return 0;
    if (!fat_find(dc, name, e, &lba, &off)) return 0;
    u32 cl = fat_ent_cluster(e);

    if (e[11] & 0x10) {                                       /* una carpeta tiene que estar vacía */
        u32 c = cl, guard = 0;
        int end = 0;
        while (!end && c >= 2 && c < FAT_EOC && guard++ < 0x1000000u) {
            for (u32 s = 0; s < fat_spc && !end; s++) {
                if (!disk_read(fat_lba(c) + s, sec)) return 0;
                for (u32 o = 0; o < 512; o += 32) {
                    const u8 *d = sec + o;
                    if (d[0] == 0) { end = 1; break; }
                    if (d[0] == 0xE5 || d[0] == '.' || d[11] == 0x0F) continue;
                    return 0;
                }
            }
            if (!end) c = fat_get(c);
        }
    }
    if (!disk_read(lba, sec)) return 0;
    sec[off] = 0xE5;
    if (!disk_write(lba, sec)) return 0;
    if (cl) fat_free_chain(cl);
    fat_forget();
    return 1;
}

/* ---------- Formatear ---------- */

static int fat_write_file(const char *rel, const u8 *data, u32 len)
{
    int r = fat_write_file_inner(rel, data, len);
    fat_fsinfo_sync();
    return r;
}

static int fat_mkdir(const char *rel)
{
    int r = fat_mkdir_inner(rel);
    fat_fsinfo_sync();
    return r;
}

static int fat_remove(const char *rel)
{
    int r = fat_remove_inner(rel);
    fat_fsinfo_sync();
    return r;
}

/* Deja el disco entero como un FAT32 vacío (sin tabla de particiones). Borra todo lo que hubiera. */
static int fat_format(void)
{
    if (!disk_present || disk_sectors < 4096) return 0;
    u32 total = disk_sectors;
    u32 spc = total <= 1048576u ? 1 : (total <= 16777216u ? 8 : 16);
    u32 res = 32, nf = 2, fatsz = 1;
    for (;;) {                                                /* la FAT más pequeña que alcanza para todos sus clusters */
        u32 cl = (total - res - nf * fatsz) / spc;
        u32 need = ((cl + 2) * 4 + 511) / 512;
        if (need <= fatsz) break;
        fatsz = need;
    }
    u32 clusters = (total - res - nf * fatsz) / spc;

    u8 b[512], z[512];
    fat_zero(z, 512);
    fat_zero(b, 512);
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
    const char *oem = "LIRIOOS ";
    for (int i = 0; i < 8; i++) b[3 + i] = (u8)oem[i];
    wr16(b + 11, 512);  b[13] = (u8)spc;  wr16(b + 14, res);  b[16] = (u8)nf;
    b[21] = 0xF8;                                             /* disco fijo */
    wr16(b + 24, 63);  wr16(b + 26, 255);
    wr32(b + 32, total);  wr32(b + 36, fatsz);
    wr32(b + 44, 2);  wr16(b + 48, 1);  wr16(b + 50, 6);
    b[64] = 0x80;  b[66] = 0x29;
    wr32(b + 67, 0x4C495249u);                                /* número de serie: "LIRI" */
    const char *lab = "LIRIO      ";
    for (int i = 0; i < 11; i++) b[71 + i] = (u8)lab[i];
    const char *typ = "FAT32   ";
    for (int i = 0; i < 8; i++) b[82 + i] = (u8)typ[i];
    wr16(b + 510, 0xAA55);

    u8 fi[512];                                               /* sector de información (FSInfo) */
    fat_zero(fi, 512);
    wr32(fi, 0x41615252u);  wr32(fi + 484, 0x61417272u);
    wr32(fi + 488, clusters - 1);  wr32(fi + 492, 3);        /* libres (todos menos la raíz) y por dónde seguir */
    wr32(fi + 508, 0xAA550000u);

    if (!disk_write(0, b) || !disk_write(6, b) || !disk_write(1, fi) || !disk_write(7, fi)) return 0;
    for (u32 s = 2; s < res; s++)
        if (s != 6 && s != 7 && !disk_write(s, z)) return 0;

    for (u32 f = 0; f < nf; f++) {
        u32 base = res + f * fatsz;
        u8 first[512];
        fat_zero(first, 512);
        wr32(first, 0x0FFFFFF8u);  wr32(first + 4, 0x0FFFFFFFu);  wr32(first + 8, 0x0FFFFFFFu);
        if (!disk_write(base, first)) return 0;
        for (u32 s = 1; s < fatsz; s++)
            if (!disk_write(base + s, z)) return 0;
    }
    u32 data = res + nf * fatsz;
    u8 rootsec[512];                                          /* carpeta raíz: solo la etiqueta del volumen */
    fat_zero(rootsec, 512);
    for (int i = 0; i < 11; i++) rootsec[i] = (u8)lab[i];
    rootsec[11] = 0x08;
    wr16(rootsec + 22, fat_time);  wr16(rootsec + 24, fat_date);
    if (!disk_write(data, rootsec)) return 0;
    for (u32 s = 1; s < spc; s++)
        if (!disk_write(data + s, z)) return 0;

    fat_forget();
    if (!fat_mount()) return 0;
    fat_free_cnt = clusters - 1;
    fat_free_known = 1;
    return 1;
}

#endif
