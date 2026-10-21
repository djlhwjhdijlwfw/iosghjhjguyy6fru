// SPDX-License-Identifier: GPL-2.0
/*
 * a30s_idregs: print each online core's ID registers to the kernel log, for
 * the XNU port's questions the shim cannot answer while its secondaries do
 * not start: does the Cortex-A73 have the 16K translation granule
 * (ID_AA64MMFR0_EL1.TGran16, bits 23:20, non-zero = yes), and its LSE,
 * RCpc and PAN levels. Built against ztOS's kernel tree; load, read dmesg,
 * unload.
 */
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <asm/cputype.h>
#include <asm/sysreg.h>

static void a30s_show(void *unused)
{
	u64 mmfr0 = read_sysreg(id_aa64mmfr0_el1);
	u64 mmfr1 = read_sysreg(id_aa64mmfr1_el1);
	u64 isar0 = read_sysreg(id_aa64isar0_el1);
	u64 isar1 = read_sysreg(id_aa64isar1_el1);

	pr_info("a30s_idregs: cpu%d mpidr=%#llx midr=%#x mmfr0=%#llx tgran16=%llu tgran4=%llu tgran64=%llu lse=%llu rcpc=%llu pan=%llu el=%llu\n",
		smp_processor_id(), read_cpuid_mpidr() & 0xffffff, read_cpuid_id(), mmfr0,
		(mmfr0 >> 20) & 0xf, (mmfr0 >> 28) & 0xf, (mmfr0 >> 24) & 0xf,
		(isar0 >> 20) & 0xf, (isar1 >> 20) & 0xf, (mmfr1 >> 20) & 0xf,
		(read_sysreg(CurrentEL) >> 2) & 3);
}

static int __init a30s_idregs_init(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		smp_call_function_single(cpu, a30s_show, NULL, 1);
	return 0;
}

static void __exit a30s_idregs_exit(void)
{
}

module_init(a30s_idregs_init);
module_exit(a30s_idregs_exit);
MODULE_DESCRIPTION("Print each core's ID registers (Galaxy A30s XNU port)");
MODULE_LICENSE("GPL");
