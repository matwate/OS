[bits 32]

; Draw a single pixel
;   bx = x coordinate (0-319)
;   cx = y coordinate (0-199)
;   al = color index (0-255)
draw_pixel:
    push edi
    push edx

    movzx edx, cx
    imul dx, vga_width
    add dx, bx
    movzx edx, dx
    mov edi, vga_start
    mov byte [edi + edx], al

    pop edx
    pop edi
    ret

; Draw a simple test pattern to prove graphics mode works
draw_test_pattern:
    pusha

    ; White horizontal line at y=100
    mov cx, 100
    mov bx, 50
    mov al, 15
.h_loop:
    call draw_pixel
    inc bx
    cmp bx, 270
    jl .h_loop

    ; Red vertical line at x=160
    mov bx, 160
    mov cx, 50
    mov al, 4
.v_loop:
    call draw_pixel
    inc cx
    cmp cx, 150
    jl .v_loop

    ; Green rectangle at (120,120) to (199,139)
    mov cx, 120
.box_y:
    mov bx, 120
.box_x:
    mov al, 2
    call draw_pixel
    inc bx
    cmp bx, 200
    jl .box_x
    inc cx
    cmp cx, 140
    jl .box_y

    popa
    ret
