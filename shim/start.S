/*
 * a30s boot shim entry. S-BOOT jumps to the start of the kernel blob (it
 * does not need the arm64 Linux Image header, which stays for tools), MMU
 * and D-cache off, x0 = DTB pointer as for Linux. Which exception level it
 * enters at is one of the things the diagnostics report: from EL2 we drop
 * to EL1, where xnu runs.
 */
/*
 * x20 = the exception level we were entered at. From EL2, continue at EL1
 * with the MMU off, AArch64, the counters and FP/SIMD untrapped and the
 * real MIDR/MPIDR visible. Clobbers x1.
 */
.macro el1_entry
	mrs	x20, CurrentEL
	lsr	x20, x20, #2
	cmp	x20, #2
	b.ne	9f
	mrs	x1, midr_el1
	msr	vpidr_el2, x1
	mrs	x1, mpidr_el1
	msr	vmpidr_el2, x1
	mov	x1, #(1 << 31)		/* HCR_EL2.RW */
	msr	hcr_el2, x1
	mov	x1, #3			/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x1
	msr	cntvoff_el2, xzr
	mov	x1, #0x33ff
	msr	cptr_el2, x1
	msr	hstr_el2, xzr
	movz	x1, #0x0800
	movk	x1, #0x30d0, lsl #16	/* SCTLR_EL1 RES1 bits, MMU and caches off */
	msr	sctlr_el1, x1
	mov	x1, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x1
	adr	x1, 9f
	msr	elr_el2, x1
	isb
	eret
9:
.endm

/*
 * Clean and invalidate [\lo, \hi) to the point of coherency, 64-byte lines.
 * Works with the MMU off. Clobbers \lo.
 */
.macro civac_range lo, hi
	bic	\lo, \lo, #63
7:	dc	civac, \lo
	add	\lo, \lo, #64
	cmp	\lo, \hi
	b.lo	7b
	dsb	sy
.endm

/*
 * Proof of life straight from assembly, before any memory use by the core:
 * paint the left half of core \idx's 16-pixel bar at the bottom of the
 * screen (y = 1350 + 24 * idx) and push it to memory. Clobbers x1-x6.
 */
.macro life_bar idx
	mov	x1, #24
	mul	x1, \idx, x1
	add	x1, x1, #1350
	mov	x2, #2880		/* FB_STRIDE */
	mul	x1, x1, x2
	movz	x2, #0xec00, lsl #16	/* FB_BASE */
	add	x1, x1, x2
	mov	x6, x1
	movz	w4, #0xffff
	movk	w4, #0xff00, lsl #16	/* cyan */
	mov	x3, #16
8:	mov	x5, x1
	mov	x2, #360
9:	str	w4, [x5], #4
	subs	x2, x2, #1
	b.ne	9b
	add	x1, x1, #2880
	subs	x3, x3, #1
	b.ne	8b
	civac_range x6, x1
.endm

	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* code0 */
	.long	0			/* code1 */
	.quad	0			/* text_offset */
	.quad	_end			/* image_size (linked at 0) */
	.quad	0xa			/* flags: LE, 4K pages, place anywhere */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 (PE offset) */

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x19, x0			/* DTB pointer from S-BOOT */
	el1_entry
	/*
	 * S-BOOT may leave parts of the image dirty in the caches: get it all
	 * to memory now, while nothing of ours is written yet, so that cores
	 * fetching with their caches off see the same code.
	 */
	adr	x1, _head
	adrp	x2, _end
	add	x2, x2, :lo12:_end
	civac_range x1, x2
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, vectors
	msr	vbar_el1, x1
	isb
	/* zero .bss */
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	adr	x1, _bss_start		/* the zeroes too, in case data accesses are cached */
	adr	x2, _bss_end
	civac_range x1, x2
	mov	x0, x19
	mov	x1, x20			/* exception level we were entered at */
	adr	x2, _head		/* where S-BOOT put us */
	bl	shim_main
3:	wfe
	b	3b

/*
 * PSCI CPU_ON target. x0 should be the context id, but Samsung's PSCI 0.1
 * may not hand it over, so the core number comes from MPIDR. a30s: cluster
 * 1 (0x100-0x103) = cpus 0-3, cluster 2 (0x200-0x201) = 4-5, cluster 0
 * (0x0-0x1, the A73s) = 6-7. x0 as it arrived goes to secondary_main.
 */
	.globl secondary_entry
secondary_entry:
	msr	daifset, #0xf
	mov	x21, x0
	el1_entry
	mrs	x1, mpidr_el1
	ubfx	x2, x1, #8, #8
	and	x1, x1, #0xff
	cmp	x2, #0
	b.ne	5f
	add	x1, x1, #6
	b	6f
5:	cmp	x2, #2
	b.ne	6f
	add	x1, x1, #4
6:	and	x19, x1, #7
	life_bar x19
	msr	spsel, #1
	adr	x1, _sec_stacks
	add	x1, x1, x19, lsl #12
	add	x1, x1, #0x1000
	mov	sp, x1
	adr	x1, vectors
	msr	vbar_el1, x1
	isb
	mov	x0, x19
	mov	x1, x20
	mov	x2, x21
	bl	secondary_main
4:	wfe
	b	4b

/*
 * Exception vectors. Each slot stashes x29/x30, loads its slot number
 * and jumps to the common path, which saves x0-x28 and calls
 * exc_handler(slot, regs). IRQs return; everything else ends in the
 * on-screen panic.
 */
.macro vent slot
	.balign	0x80
	stp	x29, x30, [sp, #-16]!
	mov	x29, #\slot
	b	exc_common
.endm

	.balign	0x800
	.globl vectors
vectors:
	vent 0
	vent 1
	vent 2
	vent 3
	vent 4
	vent 5
	vent 6
	vent 7
	vent 8
	vent 9
	vent 10
	vent 11
	vent 12
	vent 13
	vent 14
	vent 15

exc_common:
	sub	sp, sp, #(30 * 8)
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	str	x28, [sp, #224]
	mov	x0, x29
	mov	x1, sp
	bl	exc_handler
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldr	x28, [sp, #224]
	add	sp, sp, #(30 * 8)
	ldp	x29, x30, [sp], #16
	eret
