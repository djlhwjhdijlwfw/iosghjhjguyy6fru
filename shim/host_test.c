/* Render the console layout on the host: ./out/host_test && view out/host.ppm */
#include <stdio.h>
#include "console.h"

static uint32_t host_fb[FB_H * FB_STRIDE / 4];

int main(void)
{
	con_fb = host_fb;
	con_clear(RED);
	con_str("SHIM", "a30s milestone 1: diagnostics");
	con_hex2("ENTRY", "el=", 1, "cntfrq=", 26000000);
	con_hex("IMAGE", 0x80008000);
	con_hex("X0 DTB", 0x8a000000);
	con_hex2("DTB", "magic=", 0xd00dfeed, "size=", 0x2e000);
	con_hex("SCTLR", 0x30d00800);
	con_skip();
	con_hex2("BOOTCPU", "mpidr=", 0x100, "midr=", 0x410fd034);
	con_str("C100", "A53 midr=410FD034 16K:no  ~1586MHz");
	con_strc("C000", "A73 midr=411FD090 16K:YES ~1768MHz", GREEN);
	con_strc("C001", "no answer, CPU_ON=FFFFFFFC", YELLOW);
	con_str("A73", "EL1 lse=0 rcpc=0 pan=0 m0=00101122");
	con_skip();
	con_strc("VERDICT", "A73 has 16K pages: XNU can run there", GREEN);
	con_skip();
	con_hex2("GICD", "ctlr=", 1, "typer=", 0xfc0b);
	con_strc("VTIMER", "irqs=30 other=0 last=27", GREEN);
	con_skip();
	con_str("UPTIME", "123 s since the results");
	con_str("KEYS", "dat=DF VOL+ VOL- POWER");
	con_skip();
	con_str("VOL+", "hold 1s: restart into TWRP");
	con_str("VOL-", "hold 1s: restart into download mode");
	FILE *f = fopen("out/host.ppm", "wb");
	fprintf(f, "P6 %d %d 255\n", FB_W, FB_H);
	for (int i = 0; i < FB_W * FB_H; i++) {
		unsigned char px[3] = { host_fb[i] >> 16, host_fb[i] >> 8, host_fb[i] };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
	return 0;
}
