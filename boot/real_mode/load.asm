[bits 16]

; load_bios: Load multiple sectors from floppy
;   bx = starting logical sector (0-based)
;   cx = number of sectors
;   dx = destination address
load_bios:
  pusha

  ; Save params in stack-safe registers
  mov word [lb_count], cx
  mov word [lb_lba], bx
  mov word [lb_dest], dx

.next:
  mov cx, word [lb_count]
  cmp cx, 0
  je .done

  mov ax, word [lb_lba]

  ; LBA -> CHS: sector = (lba % 18) + 1, head = (lba / 18) % 2, cyl = (lba / 18) / 2
  xor dx, dx
  mov bx, 18
  div bx              ; ax = lba/18, dx = lba%18
  mov bx, ax          ; bx = lba/18

  mov cl, dl
  inc cl              ; CL = sector (1-based)

  mov ax, bx
  xor dx, dx
  mov bx, 2
  div bx              ; ax = cylinder, dx = head
  mov ch, al          ; CH = cylinder
  mov dh, dl          ; DH = head

  mov dl, byte [boot_drive]
  mov ah, 0x02        ; read
  mov al, 1           ; 1 sector
  mov bx, word [lb_dest]
  int 0x13
  jc bios_disk_error

  ; Advance: dest += 512, lba += 1, count -= 1
  mov ax, word [lb_dest]
  add ax, 512
  mov word [lb_dest], ax

  mov ax, word [lb_lba]
  inc ax
  mov word [lb_lba], ax

  mov ax, word [lb_count]
  dec ax
  mov word [lb_count], ax

  jmp .next

.done:
  popa
  mov bx, success_msg
  call print_bios
  ret

bios_disk_error:
    popa
    mov bx, error_msg
    call print_bios
    shr ax, 8
    mov bx, ax
    call print_hex_bios
    jmp $

; BSS-like storage (placed after .text, safe in our memory layout)
lb_count: dw 0
lb_lba:   dw 0
lb_dest:  dw 0

error_msg:              db `\r\nERROR Loading Sectors. Code: `, 0
success_msg:            db `\r\nAdditional Sectors Loaded Successfully!\r\n`, 0
