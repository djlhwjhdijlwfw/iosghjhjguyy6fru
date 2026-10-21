/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Entered by PSCI CPU_ON with the MMU off, from a page the module copied
 * this to. Record what the core arrived with in the page's data block,
 * push it to memory, then hand the core back to the firmware (CPU_OFF) so
 * Linux can online it again.
 */
#include <linux/linkage.h>

	.text
	.align	12
SYM_CODE_START(a30s_stub_start)
	adr	x9, a30s_stub_data
	str	x0, [x9, #8]		/* x0: the context id, if delivered */
	mrs	x10, CurrentEL
	str	x10, [x9, #16]
	mrs	x10, mpidr_el1
	str	x10, [x9, #24]
	mrs	x10, id_aa64mmfr0_el1
	str	x10, [x9, #32]
	mrs	x10, midr_el1
	str	x10, [x9, #40]
	movz	x10, #0x5ea7
	movk	x10, #0xa305, lsl #16	/* "arrived" */
	str	x10, [x9, #0]
	dc	civac, x9
	dsb	sy
	movz	x0, #0x0002
	movk	x0, #0x8400, lsl #16	/* PSCI 0.1 CPU_OFF from the DT */
	smc	#0
1:	wfe
	b	1b
	.align	6
SYM_INNER_LABEL(a30s_stub_data, SYM_L_GLOBAL)
	.fill	8, 8, 0
SYM_INNER_LABEL(a30s_stub_end, SYM_L_GLOBAL)
SYM_CODE_END(a30s_stub_start)
