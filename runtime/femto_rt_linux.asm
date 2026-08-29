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

; -----------------------------------------------------------------------------
; c_abs(edi = int32 val) -> eax = int32
; -----------------------------------------------------------------------------
c_abs:
    push rbp
    mov rbp, rsp
    mov eax, edi
    cdq
    xor eax, edx
    sub eax, edx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_alloc(rdi = uint64 bytes) -> rax = void*
; -----------------------------------------------------------------------------
__builtin_alloc:
    push rbp
    mov rbp, rsp
    push rbx
    push r12

    mov r12, rdi
    add r12, 16
    add r12, 4095
    and r12, -4096

    mov rax, 9          ; sys_mmap
    xor rdi, rdi        ; addr = NULL
    mov rsi, r12        ; length
    mov rdx, 3          ; PROT_READ | PROT_WRITE
    mov r10, 0x22       ; MAP_PRIVATE | MAP_ANONYMOUS
    mov r8, -1          ; fd = -1
    xor r9, r9          ; offset = 0
    syscall

    cmp rax, -4096
    ja .alloc_fail

    mov [rax], r12
    add rax, 16
    jmp .alloc_done

.alloc_fail:
    xor rax, rax

.alloc_done:
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_free(rdi = void* ptr)
; -----------------------------------------------------------------------------
__builtin_free:
    push rbp
    mov rbp, rsp
    push rbx
    push r12

    test rdi, rdi
    jz .free_done

    sub rdi, 16
    mov rsi, [rdi]
    mov rax, 11         ; sys_munmap
    syscall

.free_done:
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_realloc(rdi = void* ptr, rsi = uint64 new_size) -> rax = void*
; -----------------------------------------------------------------------------
__builtin_realloc:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14

    mov r12, rdi        ; old ptr
    mov r13, rsi        ; new size

    test r12, r12
    jnz .have_old_ptr

    mov rdi, r13
    call __builtin_alloc
    jmp .realloc_exit

.have_old_ptr:
    mov r14, [r12 - 16] ; old total length with header
    sub r14, 16         ; old usable length

    mov rdi, r13
    call __builtin_alloc
    test rax, rax
    jz .realloc_exit

    mov rdx, r14
    cmp rdx, r13
    jbe .copy_len_ok
    mov rdx, r13
.copy_len_ok:
    mov rdi, rax
    mov rsi, r12
    push rax
    call __builtin_memcpy
    pop rax

    push rax
    mov rdi, r12
    call __builtin_free
    pop rax

.realloc_exit:
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_memcpy(rdi = void* dest, rsi = const void* src, rdx = uint64 count) -> rax = dest
; -----------------------------------------------------------------------------
__builtin_memcpy:
    push rbp
    mov rbp, rsp
    push rdi
    push rsi
    push rcx

    mov rax, rdi
    mov rcx, rdx
    rep movsb

    pop rcx
    pop rsi
    pop rdi
    mov rax, rdi
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_memset(rdi = void* dest, esi = int32 val, rdx = uint64 count) -> rax = dest
; -----------------------------------------------------------------------------
__builtin_memset:
    push rbp
    mov rbp, rsp
    push rdi
    push rcx
    push rax

    mov r8, rdi
    mov al, sil
    mov rcx, rdx
    mov rdi, r8
    rep stosb

    pop rax
    pop rcx
    pop rdi
    mov rax, rdi
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_str(rdi = const char* ptr)
; -----------------------------------------------------------------------------
__builtin_print_str:
    push rbp
    mov rbp, rsp
    push rbx
    push r12

    mov r12, rdi
    test r12, r12
    jz .str_done

    xor rcx, rcx
.str_loop:
    cmp byte [r12 + rcx], 0
    je .str_len_done
    inc rcx
    jmp .str_loop

.str_len_done:
    test rcx, rcx
    jz .str_done

    mov rax, 1          ; sys_write
    mov rdi, 1          ; stdout
    mov rsi, r12        ; buf
    mov rdx, rcx        ; count
    syscall

.str_done:
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_nl()
; -----------------------------------------------------------------------------
__builtin_print_nl:
    push rbp
    mov rbp, rsp
    lea rsi, [rel nl_char]
    mov rdi, 1          ; stdout
    mov rdx, 1          ; 1 byte
    mov rax, 1          ; sys_write
    syscall
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_char(edi = int32 c)
; -----------------------------------------------------------------------------
__builtin_print_char:
    push rbp
    mov rbp, rsp
    push rdi
    mov rsi, rsp        ; buffer
    mov rdx, 1
    mov rdi, 1          ; stdout
    mov rax, 1          ; sys_write
    syscall
    add rsp, 8
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_int(rdi = int64 val)
; -----------------------------------------------------------------------------
__builtin_print_int:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 32

    mov rax, rdi
    lea rcx, [rbp - 17]
    mov byte [rcx], 0

    mov r8, 0           ; is_negative flag
    cmp rax, 0
    jge .pos
    neg rax
    mov r8, 1

.pos:
    mov rbx, 10
.loop:
    xor rdx, rdx
    div rbx
    add dl, '0'
    dec rcx
    mov [rcx], dl
    test rax, rax
    jnz .loop

    cmp r8, 1
    jne .print
    dec rcx
    mov byte [rcx], '-'

.print:
    lea rdx, [rbp - 17]
    sub rdx, rcx
    mov rsi, rcx
    mov rdi, 1          ; stdout
    mov rax, 1          ; sys_write
    syscall

    add rsp, 32
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_int128(rdi = uint64 low, rsi = int64 high)
; -----------------------------------------------------------------------------
__builtin_print_int128:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    sub rsp, 64

    mov r12, rdi
    mov r13, rsi

    test r13, r13
    jns .pos_128

    push rdi
    push rsi
    push rdx
    mov rdi, '-'
    call __builtin_print_char
    pop rdx
    pop rsi
    pop rdi

    not r12
    not r13
    add r12, 1
    adc r13, 0

.pos_128:
    lea rcx, [rbp - 33]
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
    mov rax, 1          ; sys_write
    mov rdi, 1          ; stdout
    mov rsi, rcx
    mov rdx, r14
    syscall

    add rsp, 64
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_print_float(xmm0 = float64 val)
; -----------------------------------------------------------------------------
__builtin_print_float:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    sub rsp, 24

    movsd [rbp - 32], xmm0

    xorpd xmm1, xmm1
    ucomisd xmm0, xmm1
    jae .pos_float

    mov rdi, '-'
    call __builtin_print_char

    movsd xmm0, [rbp - 32]
    xorpd xmm1, xmm1
    subsd xmm1, xmm0
    movsd xmm0, xmm1
    movsd [rbp - 32], xmm0

.pos_float:
    cvttsd2si r12, xmm0
    mov rdi, r12
    call __builtin_print_int

    mov rdi, '.'
    call __builtin_print_char

    cvtsi2sd xmm1, r12
    movsd xmm0, [rbp - 32]
    subsd xmm0, xmm1

    movsd xmm1, [rel f_scale_1m]
    mulsd xmm0, xmm1
    cvttsd2si r13, xmm0

    lea rcx, [rbp - 41]
    mov byte [rcx], 0
    mov rax, r13
    mov rbx, 10
    mov r8, 6

.frac_loop:
    xor rdx, rdx
    div rbx
    add dl, '0'
    dec rcx
    mov [rcx], dl
    dec r8
    jnz .frac_loop

    mov rax, 1          ; sys_write
    mov rdi, 1          ; stdout
    mov rsi, rcx
    mov rdx, 6
    syscall

    add rsp, 24
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_panic(rdi = const char* msg, rsi = uint64 len)
; -----------------------------------------------------------------------------
__builtin_panic:
    push rbp
    mov rbp, rsp
    push rsi
    push rdi
    lea rsi, [rel panic_prefix]
    mov rdi, 2          ; stderr
    mov rdx, panic_prefix_len
    mov rax, 1          ; sys_write
    syscall

    pop rsi
    pop rdx
    mov rdi, 2
    mov rax, 1
    syscall

    lea rsi, [rel nl_char]
    mov rdi, 2
    mov rdx, 1
    mov rax, 1
    syscall

    mov rax, 60         ; sys_exit
    mov rdi, 1
    syscall
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; __builtin_exit(edi = int32 code)
; -----------------------------------------------------------------------------
__builtin_exit:
    push rbp
    mov rbp, rsp
    mov rax, 60         ; sys_exit
    syscall
    mov rsp, rbp
    pop rbp
    ret

section .rodata
nl_char: db 10
f_scale_1m: dq 1000000.0
panic_prefix: db "Femto panic: "
panic_prefix_len equ $ - panic_prefix