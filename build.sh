#!/bin/bash
set -e

NASM=${NASM:-nasm}

if command -v i686-elf-gcc >/dev/null 2>&1; then
    CC=${CC:-i686-elf-gcc}
    LD=${LD:-i686-elf-ld}
    OBJDUMP=${OBJDUMP:-i686-elf-objdump}
else
    CC=${CC:-gcc}
    LD=${LD:-ld}
    OBJDUMP=${OBJDUMP:-objdump}
fi

if ! command -v "$OBJDUMP" >/dev/null 2>&1 && [ -x /opt/homebrew/opt/llvm/bin/llvm-objdump ]; then
    OBJDUMP=/opt/homebrew/opt/llvm/bin/llvm-objdump
fi

KERNEL_CFLAGS=(
    -ffreestanding
    -nostdlib
    -m32
    -fno-stack-protector
    -fno-pie
    -fno-PIC
    -mno-80387
    -msoft-float
    -mno-mmx
    -mno-sse
    -mno-sse2
)

file_size() {
    if stat -c%s "$1" >/dev/null 2>&1; then
        stat -c%s "$1"
    else
        stat -f%z "$1"
    fi
}

scan_for_x87() {
    local disasm
    disasm=$("$OBJDUMP" -d kernel/ata.o kernel/mnist.o kernel.o)

    if printf '%s\n' "$disasm" | grep -E '	(fld|fst|fadd|fmul|fdiv|fsub|fcom|fild|fist)[a-z]*([[:space:]]|$)' >/dev/null; then
        echo "Error: x87 instruction found in kernel objects:"
        printf '%s\n' "$disasm" | grep -E '	(fld|fst|fadd|fmul|fdiv|fsub|fcom|fild|fist)[a-z]*([[:space:]]|$)'
        exit 1
    fi

    echo "Verified: no x87 instructions in kernel objects."
}

echo "Assembling bootloader..."
"$NASM" -f bin boot/boot.asm -o boot.bin

echo "Compiling kernel..."
"$CC" "${KERNEL_CFLAGS[@]}" -c kernel/ata.c -o kernel/ata.o
"$CC" "${KERNEL_CFLAGS[@]}" -c kernel/mnist.c -o kernel/mnist.o
"$CC" "${KERNEL_CFLAGS[@]}" -c kernel/kernel.c -o kernel.o

echo "Linking kernel at 0x7E00..."
"$LD" -m elf_i386 -T kernel/kernel.ld -o kernel.bin kernel/ata.o kernel/mnist.o kernel.o

# Kernel size in sectors
KERNEL_SIZE=$(file_size kernel.bin)
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
"$CC" "${KERNEL_CFLAGS[@]}" -DWEIGHTS_LBA=$WEIGHTS_LBA -c kernel/kernel.c -o kernel.o
"$LD" -m elf_i386 -T kernel/kernel.ld -o kernel.bin kernel/ata.o kernel/mnist.o kernel.o

scan_for_x87

PADDING=$(( ( $(file_size kernel.bin) + 511) / 512 * 512 ))
truncate -s $PADDING kernel.bin
WEIGHTS_LBA=$(( PADDING / 512 + 1 ))
echo "Final kernel: $PADDING bytes, weights at LBA $WEIGHTS_LBA"

# Build disk image
cat boot.bin kernel.bin kernel/mnist/weights_int8.bin > os.img

# Verify sizes
echo ""
echo "=== Layout ==="
echo "Boot sector:    LBA 0  (512 bytes)"
echo "Kernel:         LBA 1..$((PADDING/512-1))  ($PADDING bytes, $((PADDING/512)) sectors)"
echo "Weights:        LBA $WEIGHTS_LBA..$((WEIGHTS_LBA+1567))  (802,360 bytes, 1,568 sectors, int8)"
echo ""
echo "Done. Run with: qemu-system-i386 -hda os.img"
echo "Note: Use -hda (not -fda) for ATA PIO to work."
echo "      Floppy (-fda) uses FDC at 0x3F0, not ATA at 0x1F0."
