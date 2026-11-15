/* Sistema de archivos en memoria (ramfs) de LirioOS: carpetas y archivos de solo lectura.
 * Lo usan el terminal (ls, cd, cat) y el explorador de archivos gráfico. */

#ifndef LIRIO_FS_H
#define LIRIO_FS_H

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

struct ramfs_file {
    const char *path;      /* sin barra inicial: "docs/lirio.txt" */
    const char *data;      /* texto UTF-8 */
};

static const struct ramfs_file ramfs[] = {
    { "bienvenida.txt",
      "Bienvenida a LirioOS.\nEste es un kernel nuevo, escrito desde cero.\nEscribe 'ayuda' para ver los comandos.\n" },
    { "docs/lirio.txt",
      "El lirio es una flor elegante. Este sistema quiere serlo también.\n" },
    { "docs/pendiente.txt",
      "Siguientes pasos: aplicaciones, FAT32 de solo lectura, disco y procesos.\n" },
    { "docs/kernel.txt",
      "Kernel propio de LirioOS: C y ensamblador, x86 de 32 bits.\nModo gráfico, teclado español, ratón PS/2 y ventanas.\n" },
    { "docs/poema.txt",
      "El lirio abre en silencio\nsu luz de lila y de lluvia.\nNadie lo ve amanecer,\npero el jardín lo adivina.\n" },
    { "notas/ideas.txt",
      "Ideas para LirioOS:\n- Un visor de imágenes.\n- Un editor de texto.\n- Un reproductor de música.\n" },
    { "notas/lista.txt",
      "Hecho:\n- Kernel con modo gráfico.\n- Teclado español y ratón.\n- Dock y ventanas.\n\nPor hacer:\n- Disco y FAT32.\n- Interrupciones.\n" },
};

#define RAMFS_COUNT (sizeof(ramfs) / sizeof(ramfs[0]))

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
    if (path[0] == 0) return 1;
    for (u32 i = 0; i < RAMFS_COUNT; i++)
        if (in_dir(ramfs[i].path, path)) return 1;
    return 0;
}

static const struct ramfs_file *fs_file(const char *path)
{
    for (u32 i = 0; i < RAMFS_COUNT; i++)
        if (str_eq(ramfs[i].path, path)) return &ramfs[i];
    return 0;
}

/* Entrada número 'idx' de una carpeta: primero las carpetas y luego los archivos, en el orden
 * en que se declararon. Devuelve 1 y rellena 'name' (hasta 47 caracteres) e 'is_dir'; 0 si no hay tantas. */
static int fs_entry(const char *dir, u32 idx, char *name, int *is_dir)
{
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
