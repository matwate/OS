;
; Protected Mode with VGA Graphics
;
; boot.asm
;

; Set Program Origin
[org 0x7C00]

; 16-bit Mode
[bits 16]

; Initialize the base pointer and the stack pointer
mov bp, 0x0500
mov sp, bp

; Save the ID of the boot drive
mov byte [boot_drive], dl

; Print Message
mov bx, msg_hello_world
call print_bios

; Load the next sector
mov bx, 0x0002
; Load 64 sectors (32 KB) so the kernel has room to grow.
mov cx, 0x0040
mov dx, 0x7E00
call load_bios

; Switch to VGA mode 0x13 (320x200, 256 colors)
mov ax, 0x0013
int 0x10

; Elevate to 32-bit protected mode
call elevate_bios

; Infinite Loop (should never be reached)
bootsector_hold:
jmp $

; INCLUDES
%include "real_mode/print.asm"
%include "real_mode/load.asm"
%include "real_mode/gdt.asm"
%include "real_mode/elevate.asm"

; DATA STORAGE AREA

msg_hello_world:                db `\r\nHello World, from the BIOS!\r\n`, 0
boot_drive:                     db 0x00

; Pad boot sector for magic number
times 510 - ($ - $$) db 0x00

; Magic number
dw 0xAA55


; Bootloader ends here. Sectors 2-65 will be loaded to 0x7E00 by load_bios.
; The C kernel is linked to run at 0x7E00 and takes over from there.
