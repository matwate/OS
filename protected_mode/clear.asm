[bits 32]

clear_protected:
    pusha
    mov edi, vga_start
    mov ecx, vga_width * vga_height
    xor eax, eax
    rep stosb
    popa
    ret
