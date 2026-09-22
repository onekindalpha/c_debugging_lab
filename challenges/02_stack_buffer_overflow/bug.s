	.arch armv8-a
	.file	"bug.c"
	.text
	.align	2
	.type	tri_index, %function
tri_index:
.LFB39:
	.cfi_startproc
	madd	w0, w0, w0, w0
	add	w0, w0, w0, lsr 31
	add	w0, w1, w0, asr 1
	ret
	.cfi_endproc
.LFE39:
	.size	tri_index, .-tri_index
	.align	2
	.type	build_pascal, %function
build_pascal:
.LFB40:
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
	str	x25, [sp, 64]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	.cfi_offset 25, -16
	mov	x23, x0
	mov	w24, w1
	mov	w20, 0
	b	.L3
.L4:
	sub	w22, w20, #1
	sub	w1, w19, #1
	mov	w0, w22
	bl	tri_index
	mov	w25, w0
	mov	w1, w19
	mov	w0, w22
	bl	tri_index
	ldr	w2, [x23, w25, sxtw 2]
	ldr	w0, [x23, w0, sxtw 2]
	add	w2, w2, w0
	str	w2, [x23, w21, sxtw 2]
.L5:
	add	w19, w19, 1
.L7:
	cmp	w20, w19
	blt	.L10
	mov	w1, w19
	mov	w0, w20
	bl	tri_index
	mov	w21, w0
	cmp	w19, 0
	cset	w2, eq
	cmp	w20, w19
	cset	w0, eq
	orr	w2, w2, w0
	cbz	w2, .L4
	mov	w0, 1
	str	w0, [x23, w21, sxtw 2]
	b	.L5
.L10:
	add	w20, w20, 1
.L3:
	cmp	w20, w24
	bge	.L11
	mov	w19, 0
	b	.L7
.L11:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldr	x25, [sp, 64]
	ldp	x29, x30, [sp], 80
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 25
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
	.cfi_endproc
.LFE40:
	.size	build_pascal, .-build_pascal
	.align	2
	.type	row_sum, %function
row_sum:
.LFB41:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	mov	x22, x0
	mov	w20, w1
	mov	w19, 0
	mov	x21, 0
	b	.L13
.L14:
	mov	w1, w19
	mov	w0, w20
	bl	tri_index
	ldrsw	x1, [x22, w0, sxtw 2]
	add	x21, x21, x1
	add	w19, w19, 1
.L13:
	cmp	w19, w20
	ble	.L14
	mov	x0, x21
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
	.cfi_endproc
.LFE41:
	.size	row_sum, .-row_sum
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align	3
.LC0:
	.string	"row %2d:"
	.align	3
.LC1:
	.string	" %d"
	.align	3
.LC2:
	.string	"   (sum=%ld)\n"
	.text
	.align	2
	.type	print_row, %function
print_row:
.LFB42:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	str	x21, [sp, 32]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	.cfi_offset 21, -16
	mov	x21, x0
	mov	w20, w1
	mov	w2, w1
	adrp	x1, .LC0
	add	x1, x1, :lo12:.LC0
	mov	w0, 2
	bl	__printf_chk
	mov	w19, 0
	b	.L17
.L18:
	mov	w1, w19
	mov	w0, w20
	bl	tri_index
	ldr	w2, [x21, w0, sxtw 2]
	adrp	x1, .LC1
	add	x1, x1, :lo12:.LC1
	mov	w0, 2
	bl	__printf_chk
	add	w19, w19, 1
.L17:
	cmp	w19, w20
	ble	.L18
	mov	w1, w20
	mov	x0, x21
	bl	row_sum
	mov	x2, x0
	adrp	x1, .LC2
	add	x1, x1, :lo12:.LC2
	mov	w0, 2
	bl	__printf_chk
	ldp	x19, x20, [sp, 16]
	ldr	x21, [sp, 32]
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 21
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
	.cfi_endproc
.LFE42:
	.size	print_row, .-print_row
	.section	.rodata.str1.8
	.align	3
.LC3:
	.string	"SIZE = %d\n"
	.text
	.align	2
	.global	main
	.type	main, %function
main:
.LFB43:
	.cfi_startproc
	sub	sp, sp, #464
	.cfi_def_cfa_offset 464
	stp	x29, x30, [sp, 432]
	.cfi_offset 29, -32
	.cfi_offset 30, -24
	add	x29, sp, 432
	str	x19, [sp, 448]
	.cfi_offset 19, -16
	adrp	x0, :got:__stack_chk_guard
	ldr	x0, [x0, :got_lo12:__stack_chk_guard]
	ldr	x1, [x0]
	str	x1, [sp, 424]
	mov	x1, 0
	mov	w1, 14
	mov	x0, sp
	bl	build_pascal
	mov	w19, 0
	b	.L21
.L22:
	mov	w1, w19
	mov	x0, sp
	bl	print_row
	add	w19, w19, 1
.L21:
	cmp	w19, 13
	ble	.L22
	mov	w2, 105
	adrp	x1, .LC3
	add	x1, x1, :lo12:.LC3
	mov	w0, 2
	bl	__printf_chk
	adrp	x0, :got:__stack_chk_guard
	ldr	x0, [x0, :got_lo12:__stack_chk_guard]
	ldr	x2, [sp, 424]
	ldr	x1, [x0]
	subs	x2, x2, x1
	mov	x1, 0
	bne	.L25
	mov	w0, 0
	ldr	x19, [sp, 448]
	ldp	x29, x30, [sp, 432]
	add	sp, sp, 464
	.cfi_remember_state
	.cfi_restore 29
	.cfi_restore 30
	.cfi_restore 19
	.cfi_def_cfa_offset 0
	ret
.L25:
	.cfi_restore_state
	bl	__stack_chk_fail
	.cfi_endproc
.LFE43:
	.size	main, .-main
	.ident	"GCC: (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0"
	.section	.note.GNU-stack,"",@progbits
