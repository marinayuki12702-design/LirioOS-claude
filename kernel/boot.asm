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

; Interrupción 0 del PIC = temporizador: guarda los registros, avisa al kernel (que puede cambiar de proceso) y vuelve
global irq0_stub
extern timer_irq
irq0_stub:
    pushad
    push ds
    push es
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    cld
    push esp
    call timer_irq
    add esp, 4
    pop es
    pop ds
    popad
    iret

; Interrupción 1 = teclado y interrupción 12 = ratón: guardan el byte recibido en una cola
global irq1_stub
extern kbd_irq
irq1_stub:
    pushad
    cld
    call kbd_irq
    popad
    iret

global irq12_stub
extern mouse_irq
irq12_stub:
    pushad
    cld
    call mouse_irq
    popad
    iret

; ---------- Modo usuario: GDT, TSS, excepciones y llamadas al sistema ----------

; Carga la GDT que prepara el kernel en C y recarga los registros de segmento
global gdt_flush
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload
.reload:
    ret

; Carga el registro de tarea (TSS): dice al procesador qué pila usar al pasar de modo usuario a kernel
global tss_flush
tss_flush:
    mov ax, 0x28
    ltr ax
    ret

; Excepciones del procesador 0-31. Las que no traen código de error meten un 0 para que todas dejen la misma pila.
%macro EXC_NOERR 1
exc%1:
    push dword 0
    push dword %1
    jmp exc_common
%endmacro
%macro EXC_ERR 1
exc%1:
    push dword %1
    jmp exc_common
%endmacro

%assign i 0
%rep 32
  %if i == 8 || i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 17 || i == 21 || i == 30
    EXC_ERR %[i]
  %else
    EXC_NOERR %[i]
  %endif
  %assign i i+1
%endrep

extern exception_handler
exc_common:
    pushad
    push ds
    push es
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    cld
    push esp
    call exception_handler
    add esp, 4
    pop es
    pop ds
    popad
    add esp, 8
    iret

section .data
global exc_table
exc_table:
%assign i 0
%rep 32
    dd exc %+ i
%assign i i+1
%endrep

section .text

; int 0x80: llamadas al sistema desde modo usuario
global syscall_stub
extern syscall_handler
syscall_stub:
    push dword 0
    push dword 0x80
    pushad
    push ds
    push es
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    cld
    push esp
    call syscall_handler
    add esp, 4
    pop es
    pop ds
    popad
    add esp, 8
    iret

; void switch_to(u32 *guarda_esp, u32 nuevo_esp, u32 nuevo_cr3): cambia de proceso. Guarda los registros del
; actual en su pila de kernel, apunta a la pila del siguiente (y a su espacio de memoria) y sigue por donde
; ese se quedó. Se llama siempre con las interrupciones desactivadas.
global switch_to
switch_to:
    mov eax, [esp + 4]
    mov ecx, [esp + 8]
    mov edx, [esp + 12]
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, ecx
    mov cr3, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; Primera vez que se ejecuta un proceso nuevo: la pila trae [entrada][esp de usuario]; salta al anillo 3
global task_start
task_start:
    pop eax
    pop ecx
    mov bx, 0x23
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    push dword 0x23         ; ss
    push ecx                ; esp del programa
    push dword 0x202        ; eflags con las interrupciones activas
    push dword 0x1B         ; cs de usuario
    push eax                ; eip
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
