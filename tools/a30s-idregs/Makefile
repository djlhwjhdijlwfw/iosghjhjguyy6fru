# Out-of-tree module against ztOS's kernel build:
#   make KDIR=~/ztos-build/a30s/linux KOUT=~/ztos-build/a30s/linux/out CROSS_COMPILE=.../aarch64-none-linux-gnu-
obj-m := a30s_idregs.o a30s_cpuon.o
a30s_cpuon-y := a30s_cpuon_main.o a30s_stub.o

KDIR ?= $(HOME)/ztos-build/a30s/linux
KOUT ?= $(KDIR)/out

all:
	$(MAKE) -C $(KDIR) O=$(KOUT) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) KBUILD_MODPOST_WARN=1 modules

clean:
	$(MAKE) -C $(KDIR) O=$(KOUT) ARCH=arm64 M=$(CURDIR) clean
