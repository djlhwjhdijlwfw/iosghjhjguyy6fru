#pragma once
#include <stdint.h>

/* Boot the SWEETXNU payload found in the ABL ramdisk; returns only on error. */
const char *loader_boot(uintptr_t dtb);
/* The payload build.sh appended behind the shim, or 0. */
uintptr_t loader_payload(void);
