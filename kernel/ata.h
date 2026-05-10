#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* ATA PIO read: load 'count' sectors starting at LBA 'lba' into 'dst'.
   Returns 0 on success, non-zero on error. */
int ata_pio_read(uint32_t lba, uint16_t count, void *dst);

#endif /* ATA_H */