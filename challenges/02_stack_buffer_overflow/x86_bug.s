"square":
        push    rbp
        mov     rbp, rsp
        mov     DWORD PTR [rbp-4], edi
        mov     eax, DWORD PTR [rbp-4]
        imul    eax, eax
        pop     rbp
        ret
"tri_index":
        push    rbp
        mov     rbp, rsp
        mov     DWORD PTR [rbp-4], edi
        mov     DWORD PTR [rbp-8], esi
        mov     eax, DWORD PTR [rbp-4]
        add     eax, 1
        imul    eax, DWORD PTR [rbp-4]
        mov     edx, eax
        shr     edx, 31
        add     eax, edx
        sar     eax
        mov     edx, eax
        mov     eax, DWORD PTR [rbp-8]
        add     eax, edx
        pop     rbp
        ret
"build_pascal":
        push    rbp
        mov     rbp, rsp
        sub     rsp, 48
        mov     QWORD PTR [rbp-40], rdi
        mov     DWORD PTR [rbp-44], esi
        mov     DWORD PTR [rbp-4], 0
        jmp     .L6
.L12:
        mov     DWORD PTR [rbp-8], 0
        jmp     .L7
.L11:
        mov     edx, DWORD PTR [rbp-8]
        mov     eax, DWORD PTR [rbp-4]
        mov     esi, edx
        mov     edi, eax
        call    "tri_index"
        mov     DWORD PTR [rbp-12], eax
        cmp     DWORD PTR [rbp-8], 0
        je      .L8
        mov     eax, DWORD PTR [rbp-8]
        cmp     eax, DWORD PTR [rbp-4]
        jne     .L9
.L8:
        mov     eax, DWORD PTR [rbp-12]
        cdqe
        lea     rdx, [0+rax*4]
        mov     rax, QWORD PTR [rbp-40]
        add     rax, rdx
        mov     DWORD PTR [rax], 1
        jmp     .L10
.L9:
        mov     eax, DWORD PTR [rbp-8]
        lea     edx, [rax-1]
        mov     eax, DWORD PTR [rbp-4]
        sub     eax, 1
        mov     esi, edx
        mov     edi, eax
        call    "tri_index"
        mov     DWORD PTR [rbp-16], eax
        mov     eax, DWORD PTR [rbp-4]
        lea     edx, [rax-1]
        mov     eax, DWORD PTR [rbp-8]
        mov     esi, eax
        mov     edi, edx
        call    "tri_index"
        mov     DWORD PTR [rbp-20], eax
        mov     eax, DWORD PTR [rbp-16]
        cdqe
        lea     rdx, [0+rax*4]
        mov     rax, QWORD PTR [rbp-40]
        add     rax, rdx
        mov     ecx, DWORD PTR [rax]
        mov     eax, DWORD PTR [rbp-20]
        cdqe
        lea     rdx, [0+rax*4]
        mov     rax, QWORD PTR [rbp-40]
        add     rax, rdx
        mov     edx, DWORD PTR [rax]
        mov     eax, DWORD PTR [rbp-12]
        cdqe
        lea     rsi, [0+rax*4]
        mov     rax, QWORD PTR [rbp-40]
        add     rax, rsi
        add     edx, ecx
        mov     DWORD PTR [rax], edx
.L10:
        add     DWORD PTR [rbp-8], 1
.L7:
        mov     eax, DWORD PTR [rbp-8]
        cmp     eax, DWORD PTR [rbp-4]
        jle     .L11
        add     DWORD PTR [rbp-4], 1
.L6:
        mov     eax, DWORD PTR [rbp-4]
        cmp     eax, DWORD PTR [rbp-44]
        jl      .L12
        nop
        nop
        leave
        ret
"row_sum":
        push    rbp
        mov     rbp, rsp
        sub     rsp, 32
        mov     QWORD PTR [rbp-24], rdi
        mov     DWORD PTR [rbp-28], esi
        mov     QWORD PTR [rbp-8], 0
        mov     DWORD PTR [rbp-12], 0
        jmp     .L14
.L15:
        mov     edx, DWORD PTR [rbp-12]
        mov     eax, DWORD PTR [rbp-28]
        mov     esi, edx
        mov     edi, eax
        call    "tri_index"
        cdqe
        lea     rdx, [0+rax*4]
        mov     rax, QWORD PTR [rbp-24]
        add     rax, rdx
        mov     eax, DWORD PTR [rax]
        cdqe
        add     QWORD PTR [rbp-8], rax
        add     DWORD PTR [rbp-12], 1
.L14:
        mov     eax, DWORD PTR [rbp-12]
        cmp     eax, DWORD PTR [rbp-28]
        jle     .L15
        mov     rax, QWORD PTR [rbp-8]
        leave
        ret
.LC0:
        .string "row %2d:"
.LC1:
        .string " %d"
.LC2:
        .string "   (sum=%ld)\n"
"print_row":
        push    rbp
        mov     rbp, rsp
        sub     rsp, 32
        mov     QWORD PTR [rbp-24], rdi
        mov     DWORD PTR [rbp-28], esi
        mov     eax, DWORD PTR [rbp-28]
        mov     esi, eax
        mov     edi, OFFSET FLAT:.LC0
        mov     eax, 0
        call    "printf"
        mov     DWORD PTR [rbp-4], 0
        jmp     .L18
.L19:
        mov     edx, DWORD PTR [rbp-4]
        mov     eax, DWORD PTR [rbp-28]
        mov     esi, edx
        mov     edi, eax
        call    "tri_index"
        cdqe
        lea     rdx, [0+rax*4]
        mov     rax, QWORD PTR [rbp-24]
        add     rax, rdx
        mov     eax, DWORD PTR [rax]
        mov     esi, eax
        mov     edi, OFFSET FLAT:.LC1
        mov     eax, 0
        call    "printf"
        add     DWORD PTR [rbp-4], 1
.L18:
        mov     eax, DWORD PTR [rbp-4]
        cmp     eax, DWORD PTR [rbp-28]
        jle     .L19
        mov     edx, DWORD PTR [rbp-28]
        mov     rax, QWORD PTR [rbp-24]
        mov     esi, edx
        mov     rdi, rax
        call    "row_sum"
        mov     rsi, rax
        mov     edi, OFFSET FLAT:.LC2
        mov     eax, 0
        call    "printf"
        nop
        leave
        ret
.LC3:
        .string "SIZE = %d\n"
"main":
        push    rbp
        mov     rbp, rsp
        sub     rsp, 432
        lea     rax, [rbp-432]
        mov     esi, 14
        mov     rdi, rax
        call    "build_pascal"
        mov     DWORD PTR [rbp-4], 0
        jmp     .L21
.L22:
        mov     edx, DWORD PTR [rbp-4]
        lea     rax, [rbp-432]
        mov     esi, edx
        mov     rdi, rax
        call    "print_row"
        add     DWORD PTR [rbp-4], 1
.L21:
        cmp     DWORD PTR [rbp-4], 13
        jle     .L22
        mov     esi, 105
        mov     edi, OFFSET FLAT:.LC3
        mov     eax, 0
        call    "printf"
        mov     eax, 0
        leave
        ret