[bits 16]

print_bios:
  push ax
  push bx

  mov ah, 0x0E

  print_bios_loop:
    cmp byte[bx], 0
    je print_bios_end

    mov al, byte[bx]
    int 0x10

    inc bx
    jmp print_bios_loop

  print_bios_end:
    pop bx
    pop ax

    ret

; Prints the 16-bit value in DX as 4 hex digits via BIOS
print_hex_bios:
  push ax
  push bx
  push cx

  mov ah, 0x0E        ; BIOS teletype output
  mov cx, 4           ; 4 hex digits in a 16-bit value

  .next_nibble:
    rol dx, 4          ; rotate highest nibble into lowest position
    mov al, dl
    and al, 0x0F       ; isolate the nibble

    add al, '0'        ; convert to ASCII: 0-9 first
    cmp al, '9'
    jle .print_char
    add al, 7          ; adjust A-F ('A' = 0x41, '0'+10 = 0x3A, diff = 7)

  .print_char:
    int 0x10

    dec cx
    jnz .next_nibble

  pop cx
  pop bx
  pop ax
  ret
