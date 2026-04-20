#!/bin/bash
set -e

echo "Assembling bootloader..."
nasm -f bin boot.asm -o boot.bin

echo "Compiling kernel..."
gcc -ffreestanding -nostdlib -m32 -fno-stack-protector -fno-pie -c kernel.c -o kernel.o

echo "Linking kernel at 0x7E00..."
ld -m elf_i386 -T linker.ld -o kernel.bin kernel.o

# Determine actual kernel size and warn if it exceeds 64 sectors
KERNEL_SIZE=$(stat -c%s kernel.bin)
MAX_SIZE=$((64 * 512))
if [ "$KERNEL_SIZE" -gt "$MAX_SIZE" ]; then
    echo "WARNING: Kernel is $KERNEL_SIZE bytes, but bootloader only loads $MAX_SIZE bytes!"
    echo "Edit boot.asm and increase mov cx, 0x0040 to a larger value."
    exit 1
fi

echo "Padding kernel to 64 sectors (32 KB)..."
truncate -s $MAX_SIZE kernel.bin

echo "Building disk image..."
cat boot.bin kernel.bin > os.img

echo "Done. Kernel is $KERNEL_SIZE bytes. Run with: qemu-system-i386 -fda os.img"
