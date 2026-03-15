[BITS 16]
global bios_collect_info

BIOS_DATA_OFF equ 0x8000

bios_collect_info:
    pusha
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov es, ax

    int 0x12            ; conventional memory
    mov [BIOS_DATA_OFF], ax

    mov ah, 0x88        ; extended memory
    int 0x15
    jc skip_ext
    mov [BIOS_DATA_OFF+2], ax
skip_ext:

    mov ah, 0x02        ; RTC time
    int 0x1A
    jc skip_time

    mov al, ch
    call bcd_to_bin
    mov [BIOS_DATA_OFF+4], al

    mov al, cl
    call bcd_to_bin
    mov [BIOS_DATA_OFF+5], al

    mov al, dh
    call bcd_to_bin
    mov [BIOS_DATA_OFF+6], al
skip_time:

    mov ah, 0x0F        ; video mode
    int 0x10
    mov [BIOS_DATA_OFF+7], al

    pop es
    pop ds
    popa
    ret

bcd_to_bin:
    mov ah, al
    and ah, 0x0F
    shr al, 4
    mov bl, 10
    mul bl
    add al, ah
    ret
    