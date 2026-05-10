#!/bin/bash
set -e

echo "Assembling bootloader..."
nasm -f bin boot/boot.asm -o boot.bin

echo "Compiling kernel..."
gcc -ffreestanding -nostdlib -m32 -fno-stack-protector -fno-pie -fno-PIC \
    -c kernel/ata.c -o kernel/ata.o
gcc -ffreestanding -nostdlib -m32 -fno-stack-protector -fno-pie -fno-PIC \
    -c kernel/mnist.c -o kernel/mnist.o
gcc -ffreestanding -nostdlib -m32 -fno-stack-protector -fno-pie -fno-PIC \
    -c kernel/kernel.c -o kernel.o

echo "Linking kernel at 0x7E00..."
ld -m elf_i386 -T kernel/kernel.ld -o kernel.bin kernel/ata.o kernel/mnist.o kernel.o

# Kernel size in sectors
KERNEL_SIZE=$(stat -c%s kernel.bin)
echo "Kernel size: $KERNEL_SIZE bytes"

# Round up to nearest sector (512 bytes)
PADDING=$(( (KERNEL_SIZE + 511) / 512 * 512 ))
echo "Padding kernel to $PADDING bytes ($(( PADDING / 512 )) sectors)..."
truncate -s $PADDING kernel.bin

# Weights LBA = kernel sectors + 1 (boot sector is LBA 0)
WEIGHTS_LBA=$(( PADDING / 512 + 1 ))
echo "Weights LBA: $WEIGHTS_LBA"

# Build again with the computed LBA
echo "Recompiling with WEIGHTS_LBA=$WEIGHTS_LBA..."
gcc -ffreestanding -nostdlib -m32 -fno-stack-protector -fno-pie -fno-PIC \
    -DWEIGHTS_LBA=$WEIGHTS_LBA \
    -c kernel/kernel.c -o kernel.o
ld -m elf_i386 -T kernel/kernel.ld -o kernel.bin kernel/ata.o kernel/mnist.o kernel.o

PADDING=$(( ( $(stat -c%s kernel.bin) + 511) / 512 * 512 ))
truncate -s $PADDING kernel.bin
WEIGHTS_LBA=$(( PADDING / 512 + 1 ))
echo "Final kernel: $PADDING bytes, weights at LBA $WEIGHTS_LBA"

# Build disk image
cat boot.bin kernel.bin kernel/mnist/weights.bin > os.img

# Verify sizes
echo ""
echo "=== Layout ==="
echo "Boot sector:    LBA 0  (512 bytes)"
echo "Kernel:         LBA 1..$((PADDING/512-1))  ($PADDING bytes, $((PADDING/512)) sectors)"
echo "Weights:        LBA $WEIGHTS_LBA..$((WEIGHTS_LBA+6238))  (3,193,896 bytes, 6239 sectors)"
echo ""
echo "Done. Run with: qemu-system-i386 -hda os.img"
echo "Note: Use -hda (not -fda) for ATA PIO to work."
echo "      Floppy (-fda) uses FDC at 0x3F0, not ATA at 0x1F0."