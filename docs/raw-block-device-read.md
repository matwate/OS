# Raw Block Device Read: Loading MNIST Weights into a Protected-Mode Kernel

> **Bug Fix Log (2026-05-27):** The classifier always returned `0` regardless of the drawn digit.
> **Root cause:** `grid_to_pixels()` in `kernel.c` iterated `out[row*28+col] = grid_get(row, col)`.
> The `grid[]` array is indexed as `grid[x][y]` (first index = X horizontal axis), but MNIST format
> expects `out[row*28+col]` where `row` = Y (vertical). Passing `grid_get(row, col)` = `grid[Y][X]`
> sent a **transposed image** to the network. Fixed to `grid_get(col, row)` = `grid[X][Y]`.
> The ATA PIO read chunking logic was verified correct and required no changes.


> This tutorial is written for a bare-metal x86 project with a BIOS bootloader, a 32-bit protected-mode C kernel, and the need to load large binary data (MNIST neural network weights) from disk at runtime.

---

## 1. The Problem

Your bootloader (`boot/boot.asm`) uses `int 0x13` — the BIOS disk read interrupt — to load the kernel into memory at `0x7E00`. That works fine because the CPU is still in **16-bit real mode** when the bootloader runs.

However, your kernel switches to **32-bit protected mode** in `elevate.asm`. Once that switch happens, **all BIOS interrupts stop working**. `int 0x13` will crash or hang the machine.

Your MNIST weights are huge:

| File | Size |
|------|------|
| `kernel/mnist/weights.bin` | ~3.2 MB (float32) |
| `kernel/mnist/weights_int8.bin` | ~800 KB (int8 quantized) |

Compiling them into C arrays (`mnist_params.h`) bloats the kernel and pushes it past practical limits. The bootloader only loads a fixed number of sectors.

**Solution:** Keep the weights as raw binary on the disk image, and have the kernel read them directly from disk hardware after entering protected mode.

---

## 2. What Is "Raw Block Device Read"?

It means talking **directly to the disk controller** via CPU I/O ports, bypassing the BIOS completely.

On a PC, the primary ATA/IDE controller is accessed through ports `0x1F0`–`0x1F7`. The oldest and simplest protocol is **ATA PIO Mode** (Programmed I/O). Your kernel writes command bytes to the controller, waits for the controller to signal "data ready," then reads 16-bit words from the data port into RAM.

This works in real mode, protected mode, and even long mode — it is independent of the CPU mode because it is just port I/O.

---

## 3. The Hardware Interface: ATA PIO Ports

| Port | Name | Direction | Purpose |
|------|------|-----------|---------|
| `0x1F0` | Data | Read/Write | Read or write 16-bit words to/from the disk |
| `0x1F1` | Error | Read | Error flags after a command |
| `0x1F2` | Sector Count | Write | How many sectors to transfer (0 = 256) |
| `0x1F3` | LBA Low | Write | LBA address bits 0–7 |
| `0x1F4` | LBA Mid | Write | LBA address bits 8–15 |
| `0x1F5` | LBA High | Write | LBA address bits 16–23 |
| `0x1F6` | Drive / Head | Write | Bits 5–7 must be `0b1110` for LBA mode; bit 4 = drive (0=master); bits 0–3 = LBA bits 24–27 |
| `0x1F7` | Status | Read | Controller status byte |
| `0x1F7` | Command | Write | Command byte (e.g., `0x20` = READ SECTORS) |

### Status Register (`0x1F7`) — Bits you care about

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | ERR | Error occurred |
| 3 | DRQ | Data Request — controller has data ready to read (or is ready to accept data) |
| 6 | DRDY | Drive is ready for commands |
| 7 | BSY | Controller is busy — **do not touch any port while this is set** |

---

## 4. The ATA PIO Read Algorithm

To read `N` sectors starting at LBA address `L` into a buffer at physical address `dest`:

```
For each sector group (ATA can do multi-sector reads):
    1. Wait until BSY=0 and DRDY=1
    2. Write 0xE0 | (drive << 4) | ((L >> 24) & 0x0F) to port 0x1F6
    3. Write N to port 0x1F2
    4. Write (L >>  0) & 0xFF to port 0x1F3
    5. Write (L >>  8) & 0xFF to port 0x1F4
    6. Write (L >> 16) & 0xFF to port 0x1F5
    7. Write 0x20 to port 0x1F7 (READ SECTORS command)
    8. For each sector in the group:
        a. Poll port 0x1F7 until BSY=0 and DRQ=1
        b. Read 256 words (512 bytes) from port 0x1F0 into dest
        c. dest += 512
    9. Read port 0x1F7 one more time to finish the command
```

**Key detail:** You must read **256 16-bit words** (not 512 bytes) from port `0x1F0` for each sector. The x86 `insw` instruction is perfect for this: it reads a word from port `dx` into `[es:di]` and increments `di` by 2.

---

## 5. Why the Kernel Cannot Use `int 0x13`

When `elevate.asm` runs, it does roughly this:

1. Loads a Global Descriptor Table (GDT)
2. Sets the PE bit in CR0
3. Does a far jump to a 32-bit code segment

At that moment, the CPU switches from real mode to protected mode. BIOS services were designed for real mode — they rely on 16-bit segmented addressing and the interrupt vector table at the bottom of memory. In protected mode, the interrupt descriptor table (IDT) is empty (or different), and segment registers use selectors instead of real-mode segments.

Calling `int 0x13` in protected mode will likely triple-fault or reboot the machine because the IDT entry for `0x13` does not point to a valid handler.

**Therefore:** Any disk access after `elevate.asm` must go through hardware ports.

---

## 6. Step-by-Step Implementation Plan for Your Project

### Step 6.1: Place the weights on the disk image

Your current `build.sh` does this:

```
[ boot.bin (512 bytes) ] [ kernel.bin (padded to N sectors) ]
```

You need to append the weights file after the kernel:

```
[ boot.bin (1 sector) ] [ kernel.bin (N sectors) ] [ weights_int8.bin (M sectors) ]
         LBA 0                 LBA 1..N                LBA N+1..N+M
```

The kernel must know `N` (the first LBA of the weights) at compile time or at runtime.

**How to compute N:**
- After padding `kernel.bin` to a sector boundary in `build.sh`, capture its size in sectors:
  ```bash
  KERNEL_SECTORS=$((PADDING / 512))
  ```
- Pass `KERNEL_SECTORS` into the kernel as a constant, or read it from a known location.

The simplest approach is a **fixed offset** — pad the kernel to a known sector boundary (e.g., 128 sectors = 64 KB) and place the weights at a fixed LBA. Alternatively, embed the offset in the first sector of the kernel image.

### Step 6.2: Remove compiled-in weights from the kernel

Instead of:
```c
#include "mnist_params_int8.h"   // contains massive const arrays
```

You will have:
```c
// Reserve uninitialized space for weights in RAM
// Place this in a specific section or just use a global array in .bss
static int8_t weights_buf[WEIGHTS_SIZE];
static float  biases_buf[NUM_BIASES];
```

Then at kernel startup, call `ata_read_sectors(LBA_START, SECTOR_COUNT, weights_buf)` to load them from disk.

### Step 6.3: Write the ATA PIO read function

In your kernel (which is plain C with inline assembly), add a function like this:

```c
// ata_pio_read: read `count` sectors starting at LBA `lba` into `dst`
// dst must be a physical address (your kernel runs with identity mapping)
void ata_pio_read(uint32_t lba, uint8_t count, void *dst) {
    // 1. Wait for controller to be not busy and drive ready
    while (inb(0x1F7) & 0x80);        // while BSY
    while (!(inb(0x1F7) & 0x40));     // until DRDY

    // 2. Send drive select + high LBA nibble
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));

    // 3. Send sector count
    outb(0x1F2, count);

    // 4. Send LBA bytes
    outb(0x1F3, (uint8_t)(lba >>  0));
    outb(0x1F4, (uint8_t)(lba >>  8));
    outb(0x1F5, (uint8_t)(lba >> 16));

    // 5. Issue READ SECTORS command
    outb(0x1F7, 0x20);

    // 6. Read each sector
    uint16_t *buf = (uint16_t *)dst;
    for (int s = 0; s < count; s++) {
        // Wait for DRQ (data request)
        while (1) {
            uint8_t status = inb(0x1F7);
            if (status & 0x01) { /* handle error */ return; }
            if (status & 0x08) break;  // DRQ set
            if (!(status & 0x80)) break; // no longer busy
        }

        // Read 256 words (512 bytes) from data port
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(0x1F0);
        }
    }

    // 7. Final status read to clear the command
    inb(0x1F7);
}
```

You already have `inpb` and `outpb` in `kernel.c`. Add 16-bit variants:

```c
unsigned short inpw(unsigned short port) {
    unsigned short val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void outpw(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
```

### Step 6.4: Call it at kernel startup

In `kernel_main()`, before the main loop:

```c
void kernel_main(void) {
    clear_screen(BLACK);
    init_mouse();

    // Load weights from disk
    // Assuming weights start at LBA 65 (kernel is padded to 64 sectors = 32KB)
    ata_pio_read(65, WEIGHT_SECTORS, weights_buffer);

    while (1) { ... }
}
```

### Step 6.5: Parse the loaded binary into layer arrays

`weights_int8.bin` is a flat binary blob. You need to know the layout. The current Python script (`quantize_int8.py`) generates it — you should modify it to emit a header with offsets, or simply define the offsets in C:

```c
// After loading weights_int8.bin into weights_buf at runtime:
#define LAYER1_W_SIZE (784 * 512)
#define LAYER2_W_SIZE (512 * 512)
// etc.

int8_t *layer1_w = (int8_t *)weights_buf;
int8_t *layer2_w = (int8_t *)weights_buf + LAYER1_W_SIZE;
// ...
```

### Step 6.6: Update the build script

Your `build.sh` needs to:

1. Build `boot.bin` and `kernel.bin` as before
2. Build `weights_int8.bin` (already exists)
3. Pad `kernel.bin` to a sector boundary
4. Append `weights_int8.bin` to the disk image
5. Compute and pass the starting LBA to the kernel (see options below)

```bash
#!/bin/bash
set -e

nasm -f bin boot/boot.asm -o boot.bin
gcc -ffreestanding -nostdlib -m32 -fno-stack-protector -fno-pie \
    -c kernel/kernel.c -o kernel.o \
    -DWEIGHTS_LBA=65   # pass LBA as compile-time constant
ld -m elf_i386 -T kernel/kernel.ld -o kernel.bin kernel.o

KERNEL_SIZE=$(stat -c%s kernel.bin)
PADDING=$(( (KERNEL_SIZE + 511) / 512 * 512 ))
truncate -s $PADDING kernel.bin

cat boot.bin kernel.bin weights_int8.bin > os.img.tmp
# Create a 1.44MB floppy image
dd if=/dev/zero of=os.img bs=1024 count=1440 2>/dev/null
dd if=os.img.tmp of=os.img conv=notrunc 2>/dev/null
rm -f os.img.tmp
```

---

## 7. Important Implementation Details

### 7.1 Identity Mapping

Your kernel is linked to run at `0x7E00`. In protected mode, if your segment descriptors use a base of 0 (flat model), then virtual addresses equal physical addresses. This is called **identity mapping**. It means when you pass a pointer like `weights_buffer` to `ata_pio_read`, the numeric value of that pointer is also the physical address on the bus. This is required for PIO to work correctly.

If your GDT sets non-zero segment bases, you would need to add the segment base to get the physical address.

### 7.2 Buffer Alignment

ATA PIO reads into any aligned or unaligned buffer, but it is simplest to ensure your destination buffer is at least 2-byte aligned (for `inw`). In practice, any global array in `.bss` will be properly aligned by the linker.

### 7.3 QEMU Disk Geometry

When you run `qemu-system-i386 -fda os.img`, QEMU treats `os.img` as a 1.44 MB floppy disk with CHS geometry:
- 80 cylinders
- 2 heads
- 18 sectors per track
- 512 bytes per sector

When using **ATA PIO** (not BIOS), QEMU emulates an IDE controller. The first floppy (`-fda`) maps to IDE port `0x1F0` in many QEMU configurations, but be aware:

- On real hardware, floppies use port `0x3F0` (FDC controller), not `0x1F0` (ATA controller).
- On QEMU with `-fda`, the floppy image is also accessible through the ATA controller if QEMU is configured that way.

**For your project:** If you run `qemu-system-i386 -fda os.img -hda os.img`, the same image is mounted as both floppy and hard disk. The hard disk (`-hda`) will definitely respond on ATA ports `0x1F0`–`0x1F7`. Alternatively, use `-drive file=os.img,format=raw,index=0,media=disk`.

If you want to keep using `-fda` and also read via ATA, you may need to switch to `-hda` for the ATA path to work cleanly.

### 7.4 Single-sector vs. Multi-sector reads

The example above sends `count` sectors in one command. The ATA controller will transfer all sectors before clearing DRQ. This is efficient. If `count` is 0, the controller interprets it as 256 sectors.

For very large reads (e.g., 800 KB = ~1600 sectors), you must break them into chunks because the ATA sector count register is only 8 bits (max 256 sectors per command). A loop that calls `ata_pio_read(lba + done, 256, buf + done*512)` until all sectors are read is standard.

### 7.5 Error Handling

Always check the ERR bit (bit 0) in the status register after polling. If set, read port `0x1F1` (Error register) to get the error code. For a first implementation, you can just hang or draw a red pixel.

---

## 8. Quick Reference: Full C ATA Read Function

```c
static inline uint8_t  inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

void ata_read_sectors(uint32_t lba, uint32_t count, void *dst) {
    uint8_t *buf = (uint8_t *)dst;
    while (count > 0) {
        uint8_t sectors = (count > 256) ? 0 : (uint8_t)count;  // 0 means 256

        while (inb(0x1F7) & 0x80);          // wait BSY=0
        while (!(inb(0x1F7) & 0x40));       // wait DRDY=1

        outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
        outb(0x1F2, sectors);
        outb(0x1F3, (uint8_t)(lba >>  0));
        outb(0x1F4, (uint8_t)(lba >>  8));
        outb(0x1F5, (uint8_t)(lba >> 16));
        outb(0x1F7, 0x20);                  // READ SECTORS

        for (int s = 0; s < (sectors ? sectors : 256); s++) {
            while (1) {
                uint8_t st = inb(0x1F7);
                if (st & 0x01) return;      // error
                if (st & 0x08) break;       // DRQ
            }
            for (int i = 0; i < 256; i++) {
                uint16_t word = inw(0x1F0);
                buf[s * 512 + i * 2 + 0] = (uint8_t)(word >> 0);
                buf[s * 512 + i * 2 + 1] = (uint8_t)(word >> 8);
            }
        }

        inb(0x1F7);  // status clear

        uint32_t done = sectors ? sectors : 256;
        lba   += done;
        count -= done;
        buf   += done * 512;
    }
}
```

---

## 9. Summary Checklist

- [ ] Modify `build.sh` to append `weights_int8.bin` to `os.img` after the kernel
- [ ] Compute the LBA offset of the weights (kernel size in sectors + 1)
- [ ] Pass that LBA to the kernel at compile time (`-DWEIGHTS_LBA=...`)
- [ ] Remove `#include "mnist_params_int8.h"` from the kernel
- [ ] Add a `.bss` buffer large enough to hold the weights in RAM
- [ ] Add `ata_read_sectors()` with port I/O using `inb`/`outb`/`inw`
- [ ] Call `ata_read_sectors()` at the top of `kernel_main()` before the GUI loop
- [ ] Point your layer pointers at the loaded buffer instead of `const` arrays
- [ ] Adjust QEMU invocation if needed (consider `-hda` instead of `-fda` for ATA PIO)
- [ ] Test with a small read first (e.g., 1 sector) and verify bytes by drawing them to screen

---

## 10. Further Reading

- [OSDev Wiki: ATA PIO Mode](https://wiki.osdev.org/ATA_PIO_Mode)
- [OSDev Wiki: ATA Command Matrix](https://wiki.osdev.org/ATA/ATAPI_Command_Matrix)
- [Intel ATA/ATAPI Specification](https://wiki.osdev.org/ATA#See_Also)

