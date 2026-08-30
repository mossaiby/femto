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
extern printf
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
; __builtin_print_str(rcx = const char* ptr)
; -----------------------------------------------------------------------------
__builtin_print_str:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    test rcx, rcx
    jz .str_done
    mov rdx, rcx
    lea rcx, [rel fmt_str]
    call printf
.str_done:
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
; __builtin_print_int(rcx = int64 val)
; -----------------------------------------------------------------------------
__builtin_print_int:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov rdx, rcx
    lea rcx, [rel fmt_int64]
    call printf
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_print_int128(rcx = uint64 low, rdx = int64 high)
; -----------------------------------------------------------------------------
__builtin_print_int128:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    sub rsp, 96

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
    lea rcx, [rbp - 49]
    mov byte [rcx], 0
    mov rbx, 10
    xor r14, r14

    mov rax, r12
    or rax, r13
    jnz .div_loop

    dec rcx
    mov byte [rcx], '0'
    mov r14, 1
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
    dec rcx
    mov [rcx], dl
    inc r14
    jmp .div_loop

.print_digits:
    mov rdx, rcx
    lea rcx, [rel fmt_str]
    call printf

    add rsp, 96
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_print_float(xmm0 = float64 val)
; -----------------------------------------------------------------------------
__builtin_print_float:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    movaps xmm1, xmm0
    movq rdx, xmm0
    lea rcx, [rel fmt_float]
    call printf
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_panic(rcx = const char* msg, rdx = uint64 len)
; -----------------------------------------------------------------------------
__builtin_panic:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    sub rsp, 32
    mov r12, rcx

    lea rcx, [rel panic_prefix]
    call printf

    lea rcx, [rel fmt_str]
    mov rdx, r12
    call printf

    mov ecx, 10
    call putchar

    mov ecx, 1
    call exit
    add rsp, 32
    pop r12
    pop rbx
    leave
    ret

; -----------------------------------------------------------------------------
; __builtin_exit(ecx = int32 code)
; -----------------------------------------------------------------------------
__builtin_exit:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    call exit
    leave
    ret

section .rdata
fmt_str: db "%s", 0
fmt_int64: db "%lld", 0
fmt_float: db "%f", 0
panic_prefix: db "Femto panic: ", 0