/* Sistema de archivos en memoria (ramfs) de LirioOS: carpetas y archivos de solo lectura.
 * Lo usan el terminal (ls, cd, cat) y el explorador de archivos gráfico. */

#ifndef LIRIO_FS_H
#define LIRIO_FS_H

#include "images.h"

#define PATH_MAX 96

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static u32 slen(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

#include "fat.h"

/* Carpeta "disco": lo que hay en el disco duro, si tiene un volumen FAT32 montado */
static int is_disk(const char *p)
{
    return p[0] == 'd' && p[1] == 'i' && p[2] == 's' && p[3] == 'c' && p[4] == 'o' && (p[5] == 0 || p[5] == '/');
}

static const char *disk_rel(const char *p) { return p[5] == '/' ? p + 6 : p + 5; }

struct ramfs_file {
    const char *path;      /* sin barra inicial: "docs/lirio.txt" */
    const char *data;      /* texto UTF-8 (0 si es una imagen) */
    const struct img *img; /* imagen (0 si es texto) */
    u8 heap;               /* 1 = ruta y texto están en el montón (se pueden liberar) */
};

#define RAMFS_MAX 128

static struct ramfs_file ramfs[RAMFS_MAX] = {
    { "bienvenida.txt",
      "Bienvenida a LirioOS.\nEste es un kernel nuevo, escrito desde cero.\nEscribe 'ayuda' para ver los comandos.\n", 0, 0 },
    { "docs/lirio.txt",
      "El lirio es una flor elegante. Este sistema quiere serlo también.\n", 0, 0 },
    { "docs/pendiente.txt",
      "Siguientes pasos: aplicaciones, FAT32 de solo lectura, disco y procesos.\n", 0, 0 },
    { "docs/kernel.txt",
      "Kernel propio de LirioOS: C y ensamblador, x86 de 32 bits.\nModo gráfico, teclado español, ratón PS/2 y ventanas.\n", 0, 0 },
    { "docs/poema.txt",
      "El lirio abre en silencio\nsu luz de lila y de lluvia.\nNadie lo ve amanecer,\npero el jardín lo adivina.\n", 0, 0 },
    { "imagenes/lirio.img", 0, &img_lirio, 0 },
    { "imagenes/olas.img", 0, &img_olas, 0 },
    { "imagenes/destellos.img", 0, &img_destellos, 0 },
    { "notas/ideas.txt",
      "Ideas para LirioOS:\n- Un visor de imágenes (hecho).\n- Un editor de texto.\n- Un reproductor de música.\n", 0, 0 },
    { "notas/lista.txt",
      "Hecho:\n- Kernel con modo gráfico.\n- Teclado español y ratón.\n- Dock y ventanas.\n\nPor hacer:\n- Disco y FAT32.\n- Interrupciones.\n", 0, 0 },
};

static u32 ramfs_n = 0;         /* cuántas entradas hay (se calcula la primera vez) */
#define RAMFS_COUNT ramfs_n

/* Los archivos que se crean o se editan guardan su ruta y su texto en el montón del kernel (mem.h) */
#define FS_TEXT_MAX 4000

static int fs_ready;

static void fs_init(void)          /* cuenta las entradas iniciales (la primera vez que se usa el sistema de archivos) */
{
    if (fs_ready) return;
    fs_ready = 1;
    while (ramfs[ramfs_n].path) ramfs_n++;
}

/* ¿Está 'file' dentro de la carpeta 'dir'? (la raíz es "") */
static int in_dir(const char *file, const char *dir)
{
    u32 n = slen(dir);
    if (n == 0) return 1;
    for (u32 i = 0; i < n; i++)
        if (file[i] != dir[i]) return 0;
    return file[n] == '/';
}

static int fs_is_dir(const char *path)
{
    if (is_disk(path)) return fat_ready && fat_is_dir(disk_rel(path));
    fs_init();
    if (path[0] == 0) return 1;
    for (u32 i = 0; i < RAMFS_COUNT; i++)
        if (in_dir(ramfs[i].path, path)) return 1;
    return 0;
}

/* Copia del último archivo leído del disco: así no se vuelve a leer en cada repintado */
static struct ramfs_file disk_file;
static char *disk_buf;
static char disk_cache_path[PATH_MAX];
static int  disk_cache_ok;
static int  disk_big;                      /* el archivo es más grande de lo que se carga (no se puede editar) */

static void disk_forget(void)
{
    disk_cache_ok = 0;
    fat_forget();
}

static const struct ramfs_file *fs_file(const char *path)
{
    if (is_disk(path)) {
        if (!fat_ready) return 0;
        if (disk_cache_ok && str_eq(disk_cache_path, path)) return &disk_file;
        u8 *buf = (u8 *)kmalloc(FS_TEXT_MAX + 1);
        u32 len = 0;
        if (!buf) return 0;
        if (!fat_read_file(disk_rel(path), buf, FS_TEXT_MAX, &len)) { kfree(buf); return 0; }
        buf[len] = 0;
        disk_big = 0;
        {   /* ¿era más grande? */
            char dir[PATH_MAX], leaf[16];
            u32 dc, lba, off;
            u8 name[11], e[32];
            if (fat_split(disk_rel(path), dir, leaf) && fat_walk(dir, &dc) && fat_make83(leaf, name) &&
                fat_find(dc, name, e, &lba, &off) && rd32(e + 28) > FS_TEXT_MAX)
                disk_big = 1;
        }
        if (disk_buf) kfree(disk_buf);
        disk_buf = (char *)buf;
        u32 k = 0;
        while (path[k] && k < PATH_MAX - 1) { disk_cache_path[k] = path[k]; k++; }
        disk_cache_path[k] = 0;
        disk_file.path = disk_cache_path;
        disk_file.data = disk_buf;
        disk_file.img = 0;
        disk_file.heap = 0;
        disk_cache_ok = 1;
        return &disk_file;
    }
    fs_init();
    for (u32 i = 0; i < RAMFS_COUNT; i++)
        if (str_eq(ramfs[i].path, path)) return &ramfs[i];
    return 0;
}

/* Guarda 'text' en el archivo 'path' (lo crea si no existe). Devuelve 1 si lo logró, 0 si no cabe o no hay memoria. */
static int fs_write(const char *path, const char *text)
{
    fs_init();
    u32 n = slen(text);
    if (n > FS_TEXT_MAX || slen(path) >= PATH_MAX) return 0;
    if (is_disk(path)) {
        int ok = fat_ready && fat_write_file(disk_rel(path), (const u8 *)text, n);
        disk_forget();
        return ok;
    }
    struct ramfs_file *f = 0;
    for (u32 i = 0; i < ramfs_n; i++)
        if (str_eq(ramfs[i].path, path)) f = &ramfs[i];
    if (f && f->img) return 0;
    if (!f && ramfs_n >= RAMFS_MAX) return 0;

    char *buf = (char *)kmalloc(n + 1);
    if (!buf) return 0;
    for (u32 i = 0; i <= n; i++) buf[i] = text[i];

    if (f) {
        if (f->heap) kfree((void *)f->data);       /* el texto viejo vuelve al montón */
        f->data = buf;
        f->heap = 1;
        return 1;
    }
    u32 pl = slen(path);
    char *pc = (char *)kmalloc(pl + 1);
    if (!pc) { kfree(buf); return 0; }
    for (u32 i = 0; i <= pl; i++) pc[i] = path[i];
    f = &ramfs[ramfs_n++];
    f->path = pc;
    f->data = buf;
    f->img = 0;
    f->heap = 1;
    return 1;
}

/* Entrada número 'idx' de una carpeta: primero las carpetas y luego los archivos, en el orden
 * en que se declararon. Devuelve 1 y rellena 'name' (hasta 47 caracteres) e 'is_dir'; 0 si no hay tantas. */
static int ram_entry(const char *dir, u32 idx, char *name, int *is_dir)
{
    fs_init();
    u32 base = dir[0] ? slen(dir) + 1 : 0;
    u32 seen = 0;

    for (int pass = 0; pass < 2; pass++) {                 /* 0 = carpetas, 1 = archivos */
        for (u32 i = 0; i < RAMFS_COUNT; i++) {
            if (!in_dir(ramfs[i].path, dir)) continue;
            const char *rest = ramfs[i].path + base;
            u32 n = 0;
            while (rest[n] && rest[n] != '/') n++;
            int sub = rest[n] == '/';
            if (sub != (pass == 0)) continue;
            if (sub) {                                     /* una carpeta se cuenta una sola vez */
                int dup = 0;
                for (u32 j = 0; j < i && !dup; j++) {
                    if (!in_dir(ramfs[j].path, dir)) continue;
                    const char *r2 = ramfs[j].path + base;
                    u32 k = 0;
                    while (k < n && r2[k] == rest[k]) k++;
                    if (k == n && r2[n] == '/') dup = 1;
                }
                if (dup) continue;
            }
            if (seen == idx) {
                u32 k = 0;
                while (k < n && k < 47) { name[k] = rest[k]; k++; }
                name[k] = 0;
                *is_dir = sub;
                return 1;
            }
            seen++;
        }
    }
    return 0;
}

/* Lo que se ve de verdad en una carpeta: los archivos en memoria y, en la raíz, la carpeta "disco";
 * dentro de "disco", lo que hay en el disco. */
static int fs_entry(const char *dir, u32 idx, char *name, int *is_dir)
{
    if (is_disk(dir)) return fat_ready && fat_entry(disk_rel(dir), idx, name, is_dir);
    if (!dir[0] && fat_ready) {
        u32 nf = 0;
        while (ram_entry(dir, nf, name, is_dir) && *is_dir) nf++;
        if (idx == nf) {
            const char *d = "disco";
            u32 k = 0;
            while (d[k]) { name[k] = d[k]; k++; }
            name[k] = 0;
            *is_dir = 1;
            return 1;
        }
        if (idx > nf) idx--;
    }
    return ram_entry(dir, idx, name, is_dir);
}

/* ¿Es una imagen? (los archivos del disco no se abren para averiguarlo) */
static const struct img *fs_img(const char *path)
{
    if (is_disk(path)) return 0;
    const struct ramfs_file *f = fs_file(path);
    return f ? f->img : 0;
}

/* Carpeta que contiene a 'path' (se cambia en el sitio) */
static void fs_parent(char *path)
{
    u32 n = slen(path);
    while (n > 0 && path[n - 1] != '/') n--;
    if (n > 0) n--;
    path[n] = 0;
}

/* out = dir + "/" + name */
static void fs_join(const char *dir, const char *name, char *out)
{
    u32 o = 0;
    if (dir[0]) {
        for (u32 i = 0; dir[i] && o < PATH_MAX - 2; i++) out[o++] = dir[i];
        out[o++] = '/';
    }
    for (u32 i = 0; name[i] && o < PATH_MAX - 1; i++) out[o++] = name[i];
    out[o] = 0;
}

#endif
