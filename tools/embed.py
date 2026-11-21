#!/usr/bin/env python3
"""Genera kernel/progs.h con los programas de usuario (user/*.elf) incluidos dentro del kernel.

Uso:  make -C user && python3 tools/embed.py
"""
import glob
import os

out = "kernel/progs.h"
progs = sorted(glob.glob("user/*.elf"))
with open(out, "w") as f:
    f.write("/* Programas de usuario incluidos en el kernel, generado por tools/embed.py (no editar a mano). */\n\n"
            "#ifndef LIRIO_PROGS_H\n#define LIRIO_PROGS_H\n\n"
            "struct builtin_prog { const char *name; const u8 *data; u32 len; };\n\n")
    names = []
    for p in progs:
        name = os.path.basename(p)[:-4]
        data = open(p, "rb").read()
        f.write(f"static const u8 prog_{name}[{len(data)}] = {{\n")
        for i in range(0, len(data), 24):
            f.write("    " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
        f.write("};\n\n")
        names.append((name, len(data)))
    f.write("static const struct builtin_prog builtin_progs[] = {\n")
    for n, l in names:
        f.write(f'    {{ "{n}", prog_{n}, {l} }},\n')
    f.write("};\n#define BUILTIN_PROGS_COUNT (sizeof(builtin_progs) / sizeof(builtin_progs[0]))\n\n#endif\n")
print("escrito", out, [n for n, _ in names])
