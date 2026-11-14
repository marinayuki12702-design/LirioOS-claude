# LirioOS

Sistema operativo con escritorio morado lila, destellos y dock inferior.

**Estado:** versión 0.1, base de la distro. Aún no está probada.

## Qué incluye

- Base Debian 13 (trixie) con escritorio XFCE y dock Plank.
- Fondo de pantalla y logo originales de LirioOS (`branding/`).
- Configuración de `live-build` para generar una ISO arrancable en VirtualBox.
- Flujo de GitHub Actions que compila la ISO en la nube.

## Cómo obtener la ISO

1. Ve a la pestaña **Actions** de este repositorio.
2. Abre **Compilar ISO de LirioOS** y pulsa **Run workflow**.
3. Cuando termine (puede tardar 20-40 minutos), descarga `LirioOS-iso` desde la sección *Artifacts* de esa ejecución.
4. En VirtualBox crea una máquina nueva (Linux, Debian 64-bit, 4 GB de RAM, EFI activado si quieres) y arráncala con la ISO.

Usuario del modo live: `lirio` (contraseña `live`).

## Estructura

| Ruta | Contenido |
|------|-----------|
| `branding/` | Fondo (`wallpaper.svg`) y logo (`logo.svg`) |
| `auto/config` | Configuración de la ISO |
| `config/package-lists/` | Paquetes instalados |
| `config/hooks/` | Identidad del sistema (`os-release`, pantalla de inicio de sesión) |
| `config/includes.chroot/` | Ajustes del escritorio (fondo, dock) |
| `.github/workflows/` | Compilación automática de la ISO |

## Siguientes pasos

- Tema de colores morado propio para ventanas y paneles.
- Configurar el dock con los iconos de las apps.
- Kernel propio en C con interfaz gráfica y teclado español: ya hay una versión en `kernel/` (ver `kernel/README.md`).
