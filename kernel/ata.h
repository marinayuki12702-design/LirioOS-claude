/* Disco duro ATA (IDE) por el método más simple: PIO con direcciones LBA de 28 bits, sin interrupciones.
 * Prueba los cuatro sitios posibles (canal primario y secundario, maestro y esclavo) y usa el primer disco
 * que encuentre; las unidades de CD (ATAPI) se ignoran.
 *
 * Con -DLIRIO_HOST las pruebas aportan host_disk_read / host_disk_write sobre una imagen en memoria. */

#ifndef LIRIO_ATA_H
#define LIRIO_ATA_H

static int  disk_present;               /* 1 = hay un disco ATA utilizable */
static u32  disk_sectors;               /* tamaño en sectores de 512 bytes */
static char disk_model[41];             /* nombre que da el disco */

#ifdef LIRIO_HOST

static int host_disk_read(u32 lba, void *buf);
static int host_disk_write(u32 lba, const void *buf);
static int disk_read(u32 lba, void *buf)        { return host_disk_read(lba, buf); }
static int disk_write(u32 lba, const void *buf) { return host_disk_write(lba, buf); }
static int disk_init(void) { return disk_present; }

#else

static u16 ata_base, ata_ctl;           /* puertos del canal elegido */
static u8  ata_drive;                   /* 0 = maestro, 1 = esclavo */

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_SR_DF  0x20

static void ata_delay(u16 ctl)           /* 400 ns: cuatro lecturas del registro de estado alterno */
{
    inb(ctl); inb(ctl); inb(ctl); inb(ctl);
}

/* Espera a que el disco termine; devuelve el estado, o 0xFF si se agotó el tiempo */
static u8 ata_wait(u16 base, u16 ctl, int need_drq)
{
    ata_delay(ctl);
    for (u32 i = 0; i < 4000000; i++) {
        u8 s = inb(base + 7);
        if (s & ATA_SR_BSY) continue;
        if (s & (ATA_SR_ERR | ATA_SR_DF)) return s;
        if (!need_drq || (s & ATA_SR_DRQ)) return s;
    }
    return 0xFF;
}

static int ata_try(u16 base, u16 ctl, u8 drive)
{
    outb(base + 6, (u8)(0xA0 | (drive << 4)));
    ata_delay(ctl);
    outb(base + 2, 0); outb(base + 3, 0); outb(base + 4, 0); outb(base + 5, 0);
    outb(base + 7, 0xEC);                                    /* IDENTIFY */
    u8 s = inb(base + 7);
    if (s == 0 || s == 0xFF) return 0;                        /* no hay nada */
    for (u32 i = 0; i < 4000000 && (inb(base + 7) & ATA_SR_BSY); i++) { }
    if (inb(base + 4) || inb(base + 5)) return 0;             /* ATAPI (CD) o SATA: no es un disco ATA */
    s = ata_wait(base, ctl, 1);
    if (s == 0xFF || (s & (ATA_SR_ERR | ATA_SR_DF)) || !(s & ATA_SR_DRQ)) return 0;

    u16 id[256];
    insw(base, id, 256);
    u32 n = (u32)id[60] | ((u32)id[61] << 16);               /* sectores direccionables con LBA28 */
    if (n == 0) return 0;
    for (int i = 0; i < 20; i++) {                            /* el nombre viene con los bytes cambiados de orden */
        disk_model[i * 2] = (char)(id[27 + i] >> 8);
        disk_model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    disk_model[40] = 0;
    for (int i = 39; i >= 0 && disk_model[i] == ' '; i--) disk_model[i] = 0;
    disk_sectors = n;
    ata_base = base;
    ata_ctl = ctl;
    ata_drive = drive;
    return 1;
}

static int disk_init(void)
{
    static const u16 bases[2] = { 0x1F0, 0x170 };
    static const u16 ctls[2] = { 0x3F6, 0x376 };
    disk_present = 0;
    for (int ch = 0; ch < 2 && !disk_present; ch++)
        for (u8 d = 0; d < 2 && !disk_present; d++)
            if (ata_try(bases[ch], ctls[ch], d)) disk_present = 1;
    return disk_present;
}

static void ata_setup(u32 lba, u8 cmd)
{
    outb(ata_base + 6, (u8)(0xE0 | (ata_drive << 4) | ((lba >> 24) & 0x0F)));
    outb(ata_base + 2, 1);
    outb(ata_base + 3, (u8)lba);
    outb(ata_base + 4, (u8)(lba >> 8));
    outb(ata_base + 5, (u8)(lba >> 16));
    outb(ata_base + 7, cmd);
}

static int disk_read(u32 lba, void *buf)
{
    if (!disk_present || lba >= disk_sectors) return 0;
    if (ata_wait(ata_base, ata_ctl, 0) == 0xFF) return 0;
    ata_setup(lba, 0x20);
    u8 s = ata_wait(ata_base, ata_ctl, 1);
    if (s == 0xFF || (s & (ATA_SR_ERR | ATA_SR_DF))) return 0;
    insw(ata_base, buf, 256);
    return 1;
}

static int disk_write(u32 lba, const void *buf)
{
    if (!disk_present || lba >= disk_sectors) return 0;
    if (ata_wait(ata_base, ata_ctl, 0) == 0xFF) return 0;
    ata_setup(lba, 0x30);
    u8 s = ata_wait(ata_base, ata_ctl, 1);
    if (s == 0xFF || (s & (ATA_SR_ERR | ATA_SR_DF))) return 0;
    outsw(ata_base, buf, 256);
    outb(ata_base + 7, 0xE7);                                 /* vaciar la caché del disco */
    s = ata_wait(ata_base, ata_ctl, 0);
    return s != 0xFF && !(s & (ATA_SR_ERR | ATA_SR_DF));
}

#endif

#endif
