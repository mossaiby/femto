default rel
section .text

global __builtin_alloc
global __builtin_free
global __builtin_realloc
global __builtin_memcpy
global __builtin_memset
global __builtin_print_str
global __builtin_print_int
global __builtin_print_int128
global __builtin_print_float
global __builtin_print_nl
global __builtin_print_char
global __builtin_panic
global __builtin_exit
global c_abs

extern malloc
extern free
extern realloc
extern putchar
extern exit

; -----------------------------------------------------------------------------
; c_abs(ecx = int32 val) -> eax = int32
; -----------------------------------------------------------------------------
c_abs:
    mov eax, ecx
    cdq
    xor eax, edx
    sub eax, edx
    ret

; -----------------------------------------------------------------------------
; __builtin_alloc(rcx = uint64 bytes) -> rax = void*
; -----------------------------------------------------------------------------
__builtin_alloc:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    call malloc
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_free(rcx = void* ptr)
; -----------------------------------------------------------------------------
__builtin_free:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    call free
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_realloc(rcx = void* ptr, rdx = uint64 new_size) -> rax = void*
; -----------------------------------------------------------------------------
__builtin_realloc:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    call realloc
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_memcpy(rcx = void* dest, rdx = const void* src, r8 = uint64 count) -> rax = dest
; -----------------------------------------------------------------------------
__builtin_memcpy:
    push rdi
    push rsi
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    mov rax, rdi
    cld
    rep movsb
    pop rsi
    pop rdi
    ret

; -----------------------------------------------------------------------------
; __builtin_memset(rcx = void* dest, edx = int32 val, r8 = uint64 count) -> rax = dest
; -----------------------------------------------------------------------------
__builtin_memset:
    push rdi
    mov rdi, rcx
    mov al, dl
    mov rcx, r8
    mov r8, rdi
    cld
    rep stosb
    mov rax, r8
    pop rdi
    ret

; -----------------------------------------------------------------------------
; __builtin_print_char(ecx = int32 c)
; -----------------------------------------------------------------------------
__builtin_print_char:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    call putchar
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_print_nl()
; -----------------------------------------------------------------------------
__builtin_print_nl:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov ecx, 10
    call putchar
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_print_str(rcx = const char* ptr)
; -----------------------------------------------------------------------------
__builtin_print_str:
    push rbp
    push rbx
    push rsi
    mov rbp, rsp
    sub rsp, 32
    mov rbx, rcx
    test rbx, rbx
    jz .str_done
.str_loop:
    movzx ecx, byte [rbx]
    test cl, cl
    jz .str_done
    call putchar
    inc rbx
    jmp .str_loop
.str_done:
    mov rsp, rbp
    pop rsi
    pop rbx
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_int(rcx = int64 val)
; -----------------------------------------------------------------------------
__builtin_print_int:
    push rbp
    push rbx
    push r12
    push r13
    mov rbp, rsp
    sub rsp, 48

    mov rax, rcx
    lea r12, [rbp - 1]
    mov byte [r12], 0

    xor r13d, r13d      ; is_negative = 0
    test rax, rax
    jns .pos
    neg rax
    mov r13d, 1

.pos:
    mov rbx, 10
.loop:
    xor rdx, rdx
    div rbx
    add dl, '0'
    dec r12
    mov [r12], dl
    test rax, rax
    jnz .loop

    test r13d, r13d
    jz .print
    dec r12
    mov byte [r12], '-'

.print:
    mov rbx, r12
.print_loop:
    movzx ecx, byte [rbx]
    test cl, cl
    jz .done
    call putchar
    inc rbx
    jmp .print_loop

.done:
    mov rsp, rbp
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_int128(rcx = uint64 low, rdx = int64 high)
; -----------------------------------------------------------------------------
__builtin_print_int128:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rbp, rsp
    sub rsp, 80

    mov r12, rcx
    mov r13, rdx

    test r13, r13
    jns .pos_128

    mov ecx, '-'
    call putchar

    not r12
    not r13
    add r12, 1
    adc r13, 0

.pos_128:
    lea r15, [rbp - 1]
    mov byte [r15], 0
    mov rbx, 10

    mov rax, r12
    or rax, r13
    jnz .div_loop

    dec r15
    mov byte [r15], '0'
    jmp .print_digits

.div_loop:
    mov rax, r12
    or rax, r13
    jz .print_digits

    mov rax, r13
    xor rdx, rdx
    div rbx
    mov r13, rax

    mov rax, r12
    div rbx
    mov r12, rax

    add dl, '0'
    dec r15
    mov [r15], dl
    jmp .div_loop

.print_digits:
    mov rbx, r15
.p_loop:
    movzx ecx, byte [rbx]
    test cl, cl
    jz .p_done
    call putchar
    inc rbx
    jmp .p_loop

.p_done:
    mov rsp, rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_float(xmm0 = float64 val)
; -----------------------------------------------------------------------------
__builtin_print_float:
    push rbp
    push rbx
    push r12
    push r13
    mov rbp, rsp
    sub rsp, 48

    movsd [rbp - 40], xmm0

    xorpd xmm1, xmm1
    ucomisd xmm0, xmm1
    jae .pos_float

    mov ecx, '-'
    call putchar

    movsd xmm0, [rbp - 40]
    xorpd xmm1, xmm1
    subsd xmm1, xmm0
    movsd xmm0, xmm1
    movsd [rbp - 40], xmm0

.pos_float:
    cvttsd2si r12, xmm0
    mov rcx, r12
    call __builtin_print_int

    mov ecx, '.'
    call putchar

    cvtsi2sd xmm1, r12
    movsd xmm0, [rbp - 40]
    subsd xmm0, xmm1

    mov rax, 1000000
    cvtsi2sd xmm1, rax
    mulsd xmm0, xmm1
    cvttsd2si r13, xmm0

    mov rax, r13
    mov rbx, 10
    lea r12, [rbp - 1]
    mov byte [r12], 0
    mov r8d, 6
.frac_loop:
    xor rdx, rdx
    div rbx
    add dl, '0'
    dec r12
    mov [r12], dl
    dec r8d
    jnz .frac_loop

    mov rbx, r12
.frac_print:
    movzx ecx, byte [rbx]
    test cl, cl
    jz .flt_done
    call putchar
    inc rbx
    jmp .frac_print

.flt_done:
    mov rsp, rbp
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_panic(rcx = const char* msg, rdx = uint64 len)
; -----------------------------------------------------------------------------
__builtin_panic:
    push rbp
    push rbx
    push rsi
    mov rbp, rsp
    sub rsp, 32
    mov rbx, rcx

    lea rsi, [rel panic_msg]
.panic_loop1:
    movzx ecx, byte [rsi]
    test cl, cl
    jz .panic_body
    call putchar
    inc rsi
    jmp .panic_loop1

.panic_body:
    test rbx, rbx
    jz .panic_nl
.panic_loop2:
    movzx ecx, byte [rbx]
    test cl, cl
    jz .panic_nl
    call putchar
    inc rbx
    jmp .panic_loop2

.panic_nl:
    mov ecx, 10
    call putchar
    mov ecx, 1
    call exit
    mov rsp, rbp
    pop rsi
    pop rbx
    pop rbp
    ret

section .rdata
panic_msg: db "Femto panic: ", 0