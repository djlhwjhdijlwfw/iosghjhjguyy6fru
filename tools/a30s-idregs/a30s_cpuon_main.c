// SPDX-License-Identifier: GPL-2.0
/*
 * a30s_cpuon: does Samsung's PSCI 0.1 CPU_ON start a core at an entry
 * point of our own while Linux runs? The diagnostic shim's CPU_ONs all
 * return 0 and no core ever arrives. Offline the core first
 * (echo 0 > /sys/devices/system/cpu/cpuN/online), then
 *   insmod a30s_cpuon.ko mpidr=0x1
 * The stub records x0, CurrentEL, MPIDR, MMFR0 and MIDR, and turns the core
 * off again, so it can be onlined afterwards.
 */
#include <linux/module.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/string.h>

#define A30S_ARRIVED	0xa3055ea7UL
#define PSCI_CPU_ON	0xc4000003UL

extern char a30s_stub_start[], a30s_stub_data[], a30s_stub_end[];

static unsigned long mpidr = 0x1;
module_param(mpidr, ulong, 0444);
MODULE_PARM_DESC(mpidr, "MPIDR of the (offline) core to start");

static void civac(void *p, size_t len)
{
	for (unsigned long a = (unsigned long)p & ~63UL; a < (unsigned long)p + len; a += 64)
		asm volatile("dc civac, %0" : : "r"(a) : "memory");
	asm volatile("dsb sy" ::: "memory");
}

static int __init a30s_cpuon_init(void)
{
	size_t len = a30s_stub_end - a30s_stub_start;
	size_t data = a30s_stub_data - a30s_stub_start;
	struct arm_smccc_res res;
	unsigned long page;
	u64 *d;
	int i;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	memcpy((void *)page, a30s_stub_start, len);
	d = (u64 *)(page + data);
	civac((void *)page, len);

	arm_smccc_smc(PSCI_CPU_ON, mpidr, virt_to_phys((void *)page), 0x5a5a, 0, 0, 0, 0, &res);
	pr_info("a30s_cpuon: CPU_ON mpidr=%#lx entry=%#llx ret=%ld\n",
		mpidr, (u64)virt_to_phys((void *)page), (long)res.a0);

	for (i = 0; i < 200; i++) {
		civac(d, 64);
		if (d[0] == A30S_ARRIVED)
			break;
		msleep(10);
	}
	if (d[0] == A30S_ARRIVED)
		pr_info("a30s_cpuon: ARRIVED after %d ms: x0=%#llx el=%llu mpidr=%#llx midr=%#llx mmfr0=%#llx tgran16=%llu\n",
			i * 10, d[1], (d[2] >> 2) & 3, d[3] & 0xffffff, d[5] & 0xffffffff,
			d[4], (d[4] >> 20) & 0xf);
	else
		pr_info("a30s_cpuon: no arrival in 2 s (marker %#llx)\n", d[0]);
	/* the page stays: the core may still be fetching from it */
	return 0;
}

static void __exit a30s_cpuon_exit(void)
{
}

module_init(a30s_cpuon_init);
module_exit(a30s_cpuon_exit);
MODULE_DESCRIPTION("Start an offline core at a stub with PSCI CPU_ON (Galaxy A30s XNU port)");
MODULE_LICENSE("GPL");
