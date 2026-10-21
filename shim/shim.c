/*
 * a30s milestone 1: one boot answers the questions that decide the XNU port
 * on the Galaxy A30s (Exynos 7904), before any kernel work:
 *   - which exception level S-BOOT enters at, where it puts us, the DTB
 *   - per core (2x Cortex-A73, 6x Cortex-A53, each woken by PSCI CPU_ON):
 *     the 16K translation granule (xnu needs it), LSE atomics, RCpc, PAN,
 *     and a rough clock
 *   - the GIC-400 (GICv2): virtual timer PPI 27 and a self-SGI as Group 1
 *     IRQs, the way xnu will have to take them
 *   - the keys on GPA1 and how long the phone stays up (watchdog)
 * Results stay on screen. Volume Up held for a second restarts into TWRP,
 * Volume Down into download mode; any exception paints a blue panic screen
 * and waits for the same keys.
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"

/* Exynos 7885 / 7904 */
#define DECON_F_BASE		0x14860000UL
#define HW_SW_TRIG_CONTROL	0x70
#define PMU_BASE		0x11c80000UL
#define PMU_SWRESET		(PMU_BASE + 0x400)	/* bit 0, syscon-reboot */
#define PMU_INFORM2		(PMU_BASE + 0x808)
#define PMU_INFORM3		(PMU_BASE + 0x80c)
#define INFORM2_REBOOT		0x12345678U	/* Samsung sec_reboot: a software reset */
#define INFORM3_RECOVERY	0x12345674U
#define INFORM3_DOWNLOAD	0x12345671U
#define GPA1_CON		0x11cb0060UL	/* pinctrl alive: etc0, etc1, gpa0, gpa1 */
#define GPA1_DAT		0x11cb0064UL
#define KEY_VOLUP		(1U << 5)	/* active low */
#define KEY_VOLDOWN		(1U << 6)
#define KEY_POWER		(1U << 7)

#define GICD_BASE		0x12301000UL
#define GICC_BASE		0x12302000UL
#define GICD_CTLR		0x000
#define GICD_TYPER		0x004
#define GICD_IIDR		0x008
#define GICD_ISENABLER		0x100
#define GICD_ICENABLER		0x180
#define GICD_ICPENDR		0x280
#define GICD_IPRIORITYR		0x400
#define GICD_SGIR		0xf00
#define GICD_PIDR2		0xfe8
#define GICC_CTLR		0x000
#define GICC_PMR		0x004
#define GICC_BPR		0x008
#define GICC_IAR		0x00c
#define GICC_EOIR		0x010
#define GICC_IIDR		0x0fc
#define VTIMER_INTID		27	/* DT: <GIC_PPI 11 ...> */
#define SGI_INTID		1

/* PSCI 0.1: only the function IDs the DT names */
#define PSCI_CPU_OFF		0x84000002UL
#define PSCI_CPU_ON		0xc4000003UL

#define NR_CPUS			8
#define SPEED_LOOPS		2000000UL

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")
#define dsb() __asm__ volatile("dsb sy" ::: "memory")

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

extern char secondary_entry[];

static const char *stage;	/* set at runtime: no absolute pointers in data */

/* a30s: CPU0-5 are the A53s at 0x100-0x103 and 0x200-0x201, CPU6-7 the A73s at 0x0-0x1 */
static uint64_t cpu_mpidr(int i)
{
	if (i < 4)
		return 0x100 | i;
	if (i < 6)
		return 0x200 | (i - 4);
	return i - 6;
}

/* What each core found about itself; written with the MMU (and so caches) off. */
struct cpu_report {
	uint64_t done, midr, mpidr, mmfr0, mmfr1, isar0, isar1, el, ticks;
};
static volatile struct cpu_report rep[NR_CPUS];

static volatile uint32_t irq_count, irq_other, irq_last;
static volatile int irq_src;

static uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return x0;
}

static uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

static void delay_ms(unsigned ms)
{
	uint64_t end = now() + rsr("cntfrq_el0") / 1000 * ms;
	while (now() < end)
		;
}

/* ---- one line of text, built up piece by piece ---- */

struct line {
	char b[48];
	int n;
};

static void lstart(struct line *l)
{
	l->n = 0;
	l->b[0] = 0;
}

static void ls(struct line *l, const char *s)
{
	while (*s && l->n < (int)sizeof(l->b) - 1)
		l->b[l->n++] = *s++;
	l->b[l->n] = 0;
}

static void lx(struct line *l, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	char buf[17];
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
	ls(l, buf);
}

static void ld(struct line *l, uint64_t v)
{
	char buf[21];
	int i = 20;
	buf[i] = 0;
	do
		buf[--i] = '0' + v % 10;
	while (v /= 10);
	ls(l, buf + i);
}

/* ---- restart, keys ---- */

static void __attribute__((noreturn)) restart_into(uint32_t inform3)
{
	wr32(PMU_INFORM2, INFORM2_REBOOT);
	wr32(PMU_INFORM3, inform3);
	dsb();
	wr32(PMU_SWRESET, rd32(PMU_SWRESET) | 1);
	for (;;)
		__asm__ volatile("wfe");
}

/*
 * Redraw the uptime and key state every 100 ms forever. A key held for a
 * second restarts: Volume Up into TWRP, Volume Down into download mode.
 */
static void __attribute__((noreturn)) key_loop(void)
{
	int r = con_row();
	uint64_t f = rsr("cntfrq_el0"), t0 = now(), held_since = 0;
	uint32_t held = 0;

	con_set_row(r + 3);
	con_str("VOL+", "hold 1s: restart into TWRP");
	con_str("VOL-", "hold 1s: restart into download mode");
	for (;;) {
		uint32_t dat = rd32(GPA1_DAT);
		uint32_t down = ~dat & (KEY_VOLUP | KEY_VOLDOWN | KEY_POWER);
		struct line l;

		con_set_row(r);
		lstart(&l);
		ld(&l, (now() - t0) / f);
		ls(&l, " s since the results    ");
		con_str("UPTIME", l.b);
		lstart(&l);
		ls(&l, "dat=");
		lx(&l, dat & 0xff, 2);
		ls(&l, down & KEY_VOLUP ? " VOL+" : "     ");
		ls(&l, down & KEY_VOLDOWN ? " VOL-" : "     ");
		ls(&l, down & KEY_POWER ? " POWER" : "      ");
		con_str("KEYS", l.b);

		uint32_t k = down & (KEY_VOLUP | KEY_VOLDOWN);
		if (k != held) {
			held = k;
			held_since = now();
		} else if (k && now() - held_since > f) {
			con_set_row(r + 6);
			con_strc("RESTART", k & KEY_VOLUP ? "into TWRP" : "into download mode", GREEN);
			delay_ms(300);
			restart_into(k & KEY_VOLUP ? INFORM3_RECOVERY : INFORM3_DOWNLOAD);
		}
		delay_ms(100);
	}
}

/* ---- exceptions ---- */

static void irq_handler(void)
{
	uint32_t iar = rd32(GICC_BASE + GICC_IAR);
	uint32_t id = iar & 0x3ff;

	if (id >= 1020)
		return;	/* spurious */
	irq_last = id;
	if (id == (uint32_t)irq_src)
		irq_count++;
	else
		irq_other++;
	if (id == VTIMER_INTID)
		wsr("cntv_tval_el0", rsr("cntfrq_el0") / 100);	/* 10 ms */
	wr32(GICC_BASE + GICC_EOIR, iar);
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	if (slot == 5) {	/* IRQ from current EL, SPx */
		irq_handler();
		return;
	}
	con_clear(BLUE);
	switch (slot & 3) {
	case 0: con_str("PANIC", "SYNC"); break;
	case 1: con_str("PANIC", "IRQ"); break;
	case 2: con_str("PANIC", "FIQ"); break;
	default: con_str("PANIC", "SERROR"); break;
	}
	con_hex2("SLOT", "n=", slot, "cpu=", rsr("mpidr_el1"));
	con_str("STAGE", stage);
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	con_hex("SPSR", rsr("spsr_el1"));
	con_hex("X0", regs[0]);
	con_hex("X1", regs[1]);
	con_hex("X30", regs[29]);	/* stashed lr is just above the frame */
	con_skip();
	key_loop();
}

/* ---- cores ---- */

/* Timer ticks for a fixed count of one-cycle loop iterations, I-cache on. */
static uint64_t speed_ticks(void)
{
	uint64_t sctlr = rsr("sctlr_el1"), n = SPEED_LOOPS;
	wsr("sctlr_el1", sctlr | (1UL << 12));	/* I-cache on (fine with MMU off) */
	isb();
	uint64_t t = now();
	__asm__ volatile("1: subs %0, %0, #1\n\tb.ne 1b" : "+r"(n));
	t = now() - t;
	wsr("sctlr_el1", sctlr);
	isb();
	return t;
}

static void report_self(int idx, uint64_t el)
{
	volatile struct cpu_report *c = &rep[idx];
	c->midr = rsr("midr_el1");
	c->mpidr = rsr("mpidr_el1");
	c->mmfr0 = rsr("id_aa64mmfr0_el1");
	c->mmfr1 = rsr("id_aa64mmfr1_el1");
	c->isar0 = rsr("id_aa64isar0_el1");
	c->isar1 = rsr("id_aa64isar1_el1");
	c->el = el;
	c->ticks = speed_ticks();
	dsb();
	c->done = 1;
	dsb();
}

void secondary_main(uint64_t idx, uint64_t el)
{
	if (idx < NR_CPUS)
		report_self(idx, el);
	smc(PSCI_CPU_OFF, 0, 0, 0);
	for (;;)
		__asm__ volatile("wfi");
}

static int field(uint64_t reg, int shift)
{
	return (reg >> shift) & 0xf;
}

/* "C100  A53 midr=410FD034 16K:no  ~1586MHz"; the other features below */
static void show_cpu(int i, int64_t ret, uint64_t f)
{
	volatile struct cpu_report *c = &rep[i];
	struct line l;
	char label[] = "C000";

	lstart(&l);
	label[1] = '0' + ((cpu_mpidr(i) >> 8) & 0xf);
	label[3] = '0' + (cpu_mpidr(i) & 0xf);
	if (!c->done) {
		ls(&l, "no answer, CPU_ON=");
		lx(&l, (uint64_t)ret, 8);
		con_strc(label, l.b, YELLOW);
		return;
	}
	uint32_t part = (c->midr >> 4) & 0xfff;
	ls(&l, part == 0xd09 ? "A73 " : part == 0xd03 ? "A53 " : "??? ");
	ls(&l, "midr=");
	lx(&l, (uint32_t)c->midr, 8);
	ls(&l, field(c->mmfr0, 20) ? " 16K:YES " : " 16K:no  ");
	ls(&l, "~");
	ld(&l, c->ticks ? SPEED_LOOPS * f / c->ticks / 1000000 : 0);
	ls(&l, "MHz");
	con_strc(label, l.b, field(c->mmfr0, 20) ? GREEN : WHITE);
}

static void show_features(const char *label, int i)
{
	volatile struct cpu_report *c = &rep[i];
	struct line l;

	lstart(&l);
	ls(&l, "EL");
	ld(&l, c->el);
	ls(&l, " lse=");
	ld(&l, field(c->isar0, 20));
	ls(&l, " rcpc=");
	ld(&l, field(c->isar1, 20));
	ls(&l, " pan=");
	ld(&l, field(c->mmfr1, 20));
	ls(&l, " m0=");
	lx(&l, (uint32_t)c->mmfr0, 8);
	con_str(label, l.b);
}

static void test_smp(int self_idx)
{
	uint64_t f = rsr("cntfrq_el0");
	int64_t ret[NR_CPUS];

	stage = "smp";
	for (int i = 0; i < NR_CPUS; i++)
		ret[i] = i == self_idx ? 0 :
			 (int64_t)smc(PSCI_CPU_ON, cpu_mpidr(i), (uintptr_t)secondary_entry, i);
	/* every core runs its speed loop before it reports: give the slow ones time */
	for (int t = 0; t < 30; t++) {
		int all = 1;
		for (int i = 0; i < NR_CPUS; i++)
			all &= rep[i].done != 0;
		if (all)
			break;
		delay_ms(100);
	}

	int a73 = -1, a53 = -1;
	for (int i = 0; i < NR_CPUS; i++) {
		show_cpu(i, ret[i], f);
		uint32_t part = (rep[i].midr >> 4) & 0xfff;
		if (rep[i].done && part == 0xd09 && a73 < 0)
			a73 = i;
		if (rep[i].done && part == 0xd03 && a53 < 0)
			a53 = i;
	}
	if (a73 >= 0)
		show_features("A73", a73);
	if (a53 >= 0)
		show_features("A53", a53);

	con_skip();
	if (a73 < 0)
		con_strc("VERDICT", "no A73 answered: see CPU_ON codes", YELLOW);
	else if (field(rep[a73].mmfr0, 20))
		con_strc("VERDICT", "A73 has 16K pages: XNU can run there", GREEN);
	else
		con_strc("VERDICT", "A73 has NO 16K pages: 4K XNU needed", YELLOW);
}

/* ---- GIC-400 (GICv2) + timer ---- */

/* Run one interrupt source for ~300 ms and report what the GIC delivered. */
static void irq_window(const char *label, int src)
{
	uint64_t f = rsr("cntfrq_el0");
	struct line l;

	lstart(&l);
	irq_count = irq_other = 0;
	irq_last = 0x3ff;
	irq_src = src;
	if (src == VTIMER_INTID) {
		wsr("cntv_tval_el0", f / 100);
		wsr("cntv_ctl_el0", 1);
	}
	isb();
	__asm__ volatile("msr daifclr, #2" ::: "memory");
	uint64_t t = now() + f * 3 / 10, next = now();
	/* SGI: send 10, 10 ms apart -> expect 10; NSATT so a Group 1 SGI goes out */
	for (int sent = 0; now() < t;)
		if (src == SGI_INTID && sent < 10 && now() >= next) {
			wr32(GICD_BASE + GICD_SGIR, (2U << 24) | (1U << 15) | SGI_INTID);
			sent++;
			next += f / 100;
		}
	__asm__ volatile("msr daifset, #2" ::: "memory");
	wsr("cntv_ctl_el0", 0);
	isb();

	ls(&l, "irqs=");
	ld(&l, irq_count);
	ls(&l, " other=");
	ld(&l, irq_other);
	ls(&l, " last=");
	ld(&l, irq_last);
	con_strc(label, l.b, irq_count ? GREEN : YELLOW);
}

static void test_gic(void)
{
	stage = "gic regs";
	uint32_t typer = rd32(GICD_BASE + GICD_TYPER);
	con_hex2("GICD", "ctlr=", rd32(GICD_BASE + GICD_CTLR), "typer=", typer);
	con_hex2("GICD", "iidr=", rd32(GICD_BASE + GICD_IIDR), "pidr2=", rd32(GICD_BASE + GICD_PIDR2));
	con_hex2("GICC", "ctlr=", rd32(GICC_BASE + GICC_CTLR), "iidr=", rd32(GICC_BASE + GICC_IIDR));

	stage = "gic setup";
	/* Whatever S-BOOT left enabled stays quiet: only our two sources run. */
	int nregs = (typer & 0x1f) + 1;
	for (int i = 0; i < nregs; i++) {
		wr32(GICD_BASE + GICD_ICENABLER + 4 * i, 0xffffffff);
		wr32(GICD_BASE + GICD_ICPENDR + 4 * i, 0xffffffff);
	}
	wr8(GICD_BASE + GICD_IPRIORITYR + VTIMER_INTID, 0xa0);
	wr8(GICD_BASE + GICD_IPRIORITYR + SGI_INTID, 0xa0);
	wr32(GICD_BASE + GICD_ISENABLER, (1U << VTIMER_INTID) | (1U << SGI_INTID));
	wr32(GICD_BASE + GICD_CTLR, rd32(GICD_BASE + GICD_CTLR) | 1);	/* NS view: EnableGrp1 */
	wr32(GICC_BASE + GICC_PMR, 0xf0);
	wr32(GICC_BASE + GICC_BPR, 0);
	/* EnableGrp1, EOImodeNS = 0: the EOIR write also deactivates */
	wr32(GICC_BASE + GICC_CTLR, (rd32(GICC_BASE + GICC_CTLR) & ~(1U << 9)) | 1);
	con_hex2("GIC ON", "dctlr=", rd32(GICD_BASE + GICD_CTLR), "cctlr=", rd32(GICC_BASE + GICC_CTLR));

	stage = "gic vtimer";
	irq_window("VTIMER", VTIMER_INTID);
	stage = "gic sgi";
	irq_window("SGI1", SGI_INTID);
	wr32(GICD_BASE + GICD_ICENABLER, 0xffffffff);
}

/* ---- main ---- */

void shim_main(uint64_t dtb, uint64_t el, uint64_t image)
{
	/* S-BOOT leaves the DECON on a software trigger: let it scan out memory (uniLoader) */
	wr32(DECON_F_BASE + HW_SW_TRIG_CONTROL, 0x1281);
	dsb();

	stage = "start";
	con_clear(RED);
	con_str("SHIM", "a30s milestone 1: diagnostics");
	con_hex2("ENTRY", "el=", el, "cntfrq=", rsr("cntfrq_el0"));
	con_hex("IMAGE", image);
	con_hex("X0 DTB", dtb);
	/* only look at x0 if it points into the RAM S-BOOT reports */
	if ((dtb & 3) == 0 && ((dtb >= 0x80000000UL && dtb < 0x100000000UL) ||
			       (dtb >= 0x880000000UL && dtb < 0x8c0000000UL))) {
		uint64_t rd_start = 0, rd_end = 0;
		stage = "dtb";
		con_hex2("DTB", "magic=", __builtin_bswap32(rd32(dtb)), "size=", __builtin_bswap32(rd32(dtb + 4)));
		if (fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) &&
		    fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
			con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);
	}
	con_hex("SCTLR", rsr("sctlr_el1"));
	con_skip();

	/* the boot core is one of the eight: find which and report it like the others */
	uint64_t self = rsr("mpidr_el1") & 0xffffff;
	int self_idx = -1;
	for (int i = 0; i < NR_CPUS; i++)
		if (cpu_mpidr(i) == self)
			self_idx = i;
	con_hex2("BOOTCPU", "mpidr=", self, "midr=", rsr("midr_el1"));
	if (self_idx >= 0)
		report_self(self_idx, el);
	test_smp(self_idx);
	con_skip();

	test_gic();
	con_skip();

	stage = "keys";
	con_hex("GPA1CON", rd32(GPA1_CON));
	key_loop();
}
