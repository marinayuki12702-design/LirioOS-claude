; Punto de entrada de LirioOS. GRUB carga este archivo mediante Multiboot
; y le pide un modo gráfico lineal de 1024x768 con 32 bits de color.
MBALIGN  equ 1 << 0
MEMINFO  equ 1 << 1
VIDEO    equ 1 << 2
FLAGS    equ MBALIGN | MEMINFO | VIDEO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    dd 0, 0, 0, 0, 0    ; campos de dirección (no se usan con ELF)
    dd 0                ; modo gráfico lineal
    dd 1024             ; ancho
    dd 768              ; alto
    dd 32               ; bits por píxel

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kmain

_start:
    mov esp, stack_top
    push ebx            ; puntero a la información de Multiboot
    push eax            ; número mágico del cargador
    call kmain
.hang:
    cli
    hlt
    jmp .hang

; Carga la tabla de interrupciones (IDT) que prepara el kernel en C
global idt_load
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; Interrupción 0 del PIC = temporizador: guarda los registros, avisa al kernel y vuelve
global irq0_stub
extern timer_irq
irq0_stub:
    pushad
    cld
    call timer_irq
    popad
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
