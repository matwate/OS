#include "ata.h"

/* ATA primary channel ports */
#define ATA_DATA      0x1F0   /* read/write 16-bit data */
#define ATA_ERROR     0x1F1   /* read: error code; write: feature bits */
#define ATA_SEC_COUNT 0x1F2   /* sector count (0 = 256) */
#define ATA_LBA_LOW   0x1F3   /* LBA bits 0-7 */
#define ATA_LBA_MID   0x1F4   /* LBA bits 8-15 */
#define ATA_LBA_HIGH  0x1F5   /* LBA bits 16-23 */
#define ATA_DRIVE     0x1F6   /* drive select + LBA bits 24-27 */
#define ATA_STATUS    0x1F7   /* read: status; write: command */
#define ATA_COMMAND   0x1F7

/* Status register bits */
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DF    0x20
#define ATA_SR_DSC   0x10
#define ATA_SR_DRQ   0x08
#define ATA_SR_CORR  0x04
#define ATA_SR_IDX   0x02
#define ATA_SR_ERR   0x01

/* Commands */
#define ATA_CMD_READ  0x20
#define ATA_CMD_READ_EXT 0x24
#define ATA_CMD_IDENTIFY 0xEC

/* Helper inline functions */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    /* Reading the status register twice gives ~400ns delay */
    (void)inb(0x1F7);
    (void)inb(0x1F7);
}

/* Poll status register until BSY=0 and DRQ=1, or until timeout */
static int wait_drq(uint32_t *timeout_counter) {
    while (*timeout_counter > 0) {
        uint8_t status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) return -1;      /* error */
        if (status & ATA_SR_DRQ) return 0;       /* data ready */
        if (!(status & ATA_SR_BSY)) return 0;    /* drive ready, no data */
        (*timeout_counter)--;
    }
    return -2;  /* timeout */
}

/* ata_pio_read: read 'count' sectors starting at LBA 'lba' into 'dst'
 *
 * Returns 0 on success.
 * Returns 1 on error.
 *
 * dst must be a valid memory location large enough for count * 512 bytes.
 * count must be > 0.
 *
 * The function breaks large reads into 256-sector chunks because the
 * ATA sector count register is 8 bits (0 = 256).
 *
 * IMPORTANT: This function uses I/O port 0x1F0-0x1F7 (ATA primary channel).
 * On QEMU, ensure the disk image is connected as an ATA device (e.g., -hda).
 * On real hardware, make sure the disk is on the primary ATA channel (master
 * or slave). Using -fda (floppy) with this code will NOT work on real hardware
 * because floppies use the FDC at 0x3F0, not the ATA controller. On QEMU,
 * -hda is recommended.
 */
int ata_pio_read(uint32_t lba, uint16_t count, void *dst) {
    uint8_t *buf = (uint8_t *)dst;

    /* Wait for drive to be ready (BSY=0, DRDY=1) */
    for (int retry = 0; retry < 5; retry++) {
        uint8_t status = inb(ATA_STATUS);
        if ((status & (ATA_SR_BSY | ATA_SR_DRDY)) == ATA_SR_DRDY)
            break;
        io_wait();
    }

    while (count > 0) {
        /* Chunk size: ATA sector count register is 8 bits (0 means 256) */
        uint16_t chunk = (count >= 256) ? 0 : count;

        /* 1. Select drive and send high LBA nibble */
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));

        /* 2. Send sector count */
        outb(ATA_SEC_COUNT, (uint8_t)chunk);

        /* 3. Send LBA bytes */
        outb(ATA_LBA_LOW,  (uint8_t)(lba >>  0));
        outb(ATA_LBA_MID,  (uint8_t)(lba >>  8));
        outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));

        /* 4. Issue READ SECTORS command */
        outb(ATA_COMMAND, ATA_CMD_READ);

        /* 5. Read each sector in this chunk */
        uint16_t sectors_this_chunk = chunk ? chunk : 256;
        for (uint16_t s = 0; s < sectors_this_chunk; s++) {
            /* Poll until DRQ or error/timeout */
            uint32_t timeout = 100000;
            int drq_result = wait_drq(&timeout);
            if (drq_result != 0) {
                /* Read error register */
                uint8_t err = inb(ATA_ERROR);
                (void)err;
                return 1;  /* error */
            }

            /* Read 256 16-bit words (512 bytes) from data port */
            uint16_t *wbuf = (uint16_t *)(buf + s * 512);
            for (int i = 0; i < 256; i++) {
                wbuf[i] = inw(ATA_DATA);
            }
        }

        /* 6. Clear pending interrupt / status */
        (void)inb(ATA_STATUS);

        /* Advance */
        uint16_t done = sectors_this_chunk;
        lba   += done;
        count -= done;
        buf   += done * 512;
    }

    return 0;
}