#pragma once
#include <stdint.h>

#define FB_BASE		0xec000000UL	/* reserved-memory fb_handover: S-BOOT's lit panel */
#define FB_W		720
#define FB_H		1560
#define FB_STRIDE	(FB_W * 4)	/* a8r8g8b8, as uniLoader's simplefb */

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

extern volatile uint32_t *con_fb;

void con_clear(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_strc(const char *label, const char *s, uint32_t fg);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
/* the row the next line goes to: lets a status line be redrawn in place */
int con_row(void);
void con_set_row(int r);
