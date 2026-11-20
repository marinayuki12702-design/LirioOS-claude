#!/usr/bin/env python3
"""Verificador independiente de imágenes FAT32 (no usa nada del kernel).
Comprueba el sector de arranque, que las dos tablas FAT sean iguales, que cada cadena de clusters sea
válida, que el tamaño de cada archivo case con su cadena, que ningún cluster se use dos veces y que no
queden clusters ocupados sin dueño. Con -t imprime el árbol de archivos.

Uso: python3 tools/check_fat32.py imagen.img [-t]"""
import struct
import sys


def main():
    path = sys.argv[1]
    tree = "-t" in sys.argv
    d = open(path, "rb").read()
    errors = []

    bps, spc, res, nf = struct.unpack_from("<HBHB", d, 11)
    fatsz = struct.unpack_from("<I", d, 36)[0]
    total = struct.unpack_from("<I", d, 32)[0]
    root = struct.unpack_from("<I", d, 44)[0]
    sig = struct.unpack_from("<H", d, 510)[0]
    if bps != 512 or sig != 0xAA55 or d[82:87] != b"FAT32":
        errors.append("sector de arranque no válido")
    fat_off = res * 512
    fats = [d[fat_off + i * fatsz * 512: fat_off + (i + 1) * fatsz * 512] for i in range(nf)]
    for i in range(1, nf):
        if fats[i] != fats[0]:
            errors.append("la FAT %d no es igual a la FAT 0" % i)
    data_start = res + nf * fatsz
    clusters = (total - data_start) // spc
    fat = struct.unpack("<%dI" % (fatsz * 128), fats[0])
    if fat[0] & 0x0FFFFFFF < 0x0FFFFFF8 or fat[1] & 0x0FFFFFFF < 0x0FFFFFF8:
        errors.append("entradas 0 y 1 de la FAT incorrectas")

    def chain(c):
        out = []
        while 2 <= c < 0x0FFFFFF8:
            if c in out or len(out) > clusters:
                errors.append("cadena con bucle en %d" % c)
                break
            out.append(c)
            c = fat[c] & 0x0FFFFFFF
        else:
            if c != 0x0FFFFFF8 and c < 0x0FFFFFF8 and c != 0x0FFFFFFF and c < 2:
                errors.append("cadena rota (cluster %d)" % c)
        return out

    def read_chain(cl):
        return b"".join(d[(data_start + (c - 2) * spc) * 512: (data_start + (c - 1) * spc) * 512] for c in cl)

    owner = {}
    files = {}

    def claim(cl, who):
        for c in cl:
            if c in owner:
                errors.append("cluster %d usado por %s y por %s" % (c, owner[c], who))
            owner[c] = who

    def walk(cluster, name, depth):
        cl = chain(cluster)
        claim(cl, name)
        raw = read_chain(cl)
        for o in range(0, len(raw), 32):
            e = raw[o:o + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue
            nm = e[:8].decode("latin1").rstrip() + ("." + e[8:11].decode("latin1").rstrip() if e[8:11].strip() else "")
            first = (struct.unpack_from("<H", e, 20)[0] << 16) | struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            full = name.rstrip("/") + "/" + nm
            if nm in (".", ".."):
                continue
            if e[11] & 0x10:
                if tree:
                    print("  " * depth + nm + "/")
                walk(first, full, depth + 1)
            else:
                fc = chain(first) if first else []
                need = -(-size // (512 * spc))
                if len(fc) != need:
                    errors.append("%s: %d clusters para %d bytes (esperaba %d)" % (full, len(fc), size, need))
                claim(fc, full)
                content = read_chain(fc)[:size]
                files[full] = content
                if tree:
                    print("  " * depth + "%s  (%d bytes) %r" % (nm, size, content[:40]))

    walk(root, "/", 0)
    used = {c for c in range(2, clusters + 2) if fat[c] & 0x0FFFFFFF != 0}
    for c in sorted(used - set(owner)):
        errors.append("cluster %d ocupado en la FAT pero sin dueño" % c)
        break
    for c in sorted(set(owner) - used):
        errors.append("cluster %d tiene dueño pero está libre en la FAT" % c)
        break
    fsinfo_sec = struct.unpack_from("<H", d, 48)[0]
    if fsinfo_sec:
        fi = d[fsinfo_sec * 512:(fsinfo_sec + 1) * 512]
        if struct.unpack_from("<I", fi, 0)[0] != 0x41615252 or struct.unpack_from("<I", fi, 484)[0] != 0x61417272:
            errors.append("el sector FSInfo no tiene las firmas correctas")
        else:
            free_info = struct.unpack_from("<I", fi, 488)[0]
            real_free = clusters - len(used)
            if free_info != 0xFFFFFFFF and free_info != real_free:
                errors.append("FSInfo dice %d clusters libres, pero hay %d" % (free_info, real_free))
    label = d[71:82].decode("latin1").rstrip()
    root_label = None
    raw = read_chain(chain(root))
    for o in range(0, len(raw), 32):
        e = raw[o:o + 32]
        if e[0] == 0:
            break
        if e[0] != 0xE5 and e[11] == 0x08:
            root_label = e[:11].decode("latin1").rstrip()
    if label != "NO NAME" and label != (root_label or ""):
        errors.append("etiqueta del sector de arranque '%s' distinta de la de la raíz '%s'" % (label, root_label))
    print("FAT32: %d clusters de %d bytes, %d usados, %d archivos" % (clusters, spc * 512, len(used), len(files)))
    if errors:
        for e in errors:
            print("ERROR:", e)
        sys.exit(1)
    print("la imagen es correcta")


main()
