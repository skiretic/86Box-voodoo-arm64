; COV3DNOW stage-1 bootloader
; Loads fixed number of sectors from floppy-emulated El Torito image
; into 0000:8000 and jumps there.

bits 16
org 0x7C00

%define STAGE2_SEG      0x0000
%define STAGE2_OFF      0x8000
%define STAGE2_SECTORS  24
%define FLOPPY_SPT      18
%define FLOPPY_HEADS    2

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, msg_stage1
    call print_str

    mov ax, STAGE2_SEG
    mov es, ax
    mov bx, STAGE2_OFF

    mov word [lba_sector], 1
    mov cx, STAGE2_SECTORS

.load_loop:
    push cx
    push bx

    mov ax, [lba_sector]
    xor dx, dx
    mov cx, FLOPPY_SPT
    div cx                      ; ax = temp/18, dx = temp%18
    mov si, dx                  ; sector index (0..17)

    xor dx, dx
    mov cx, FLOPPY_HEADS
    div cx                      ; ax = cyl, dx = head
    mov di, dx                  ; head
    mov bp, ax                  ; cylinder

    mov ch, al                  ; low 8 bits of cylinder
    mov cl, byte si
    inc cl                      ; 1-based sector
    mov al, byte bp
    shr al, 2
    and al, 0xC0
    or cl, al                   ; high cylinder bits in CL[7:6]
    mov dh, byte di             ; head
    mov dl, [boot_drive]

    mov ah, 0x02
    mov al, 1
    int 0x13
    jc disk_error

    pop bx
    add bx, 512
    pop cx

    inc word [lba_sector]
    loop .load_loop

    jmp STAGE2_SEG:STAGE2_OFF

disk_error:
    mov si, msg_disk_err
    call print_str
    jmp $

print_str:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_str
.done:
    ret

boot_drive: db 0
lba_sector: dw 0

msg_stage1 db 'COV3DNOW BOOT STAGE1', 13, 10, 0
msg_disk_err db 'COV3DNOW_ERROR DISK_READ', 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
