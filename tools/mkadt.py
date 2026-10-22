#!/usr/bin/env python3
"""Build the minimal Apple Device Tree that the xnu needs to boot on the
Galaxy A30s (SM-A307FN, Exynos 7904).

Only what the early boot path actually reads (see NOTES.md, A30S.md); every
MMIO address comes from the A30s's live FDT / stock device tree.
Usage:  mkadt.py OUT.adt [--dump] [--qemu]

--qemu: the same tree for QEMU's virt board with gic-version=2 (its GICD/GICC,
CNTFRQ and RAM), for tools/qemu-sweet.py.
"""
import hashlib, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

# Kernel-managed RAM. The SM-A307FN (3 GB) has System RAM 0x80000000-0xba5fffff,
# 0xc0000000-0xffffffff and 0x8_80000000-0x8_bfffffff (/proc/iomem); xnu wants
# one range, so the upper bank: 1 GB with no carve-out in it (the low banks hold
# the secure, framebuffer and modem regions). DMA above 4 GiB is then a must.
PHYS_BASE = 0x880000000
MEM_SIZE = 0x40000000
# QEMU virt: RAM from 0x40000000; this range plus the framebuffer below fit in -m 4G
QEMU_PHYS_BASE = 0x100000000
# /arm-io children are relative to this; it must not be 0 (serial_init bails)
SOC_BASE = 0x01000000

# GIC-400 (GICv2): interrupt-controller@12301000, GICD 4K + GICC 8K
GICD, GICD_SIZE = 0x12301000, 0x1000
GICC, GICC_SIZE = 0x12302000, 0x2000
# fb_handover: S-BOOT's lit panel (the shim releases DECON's software trigger)
FB, FB_SIZE = 0xec000000, 0x1400000
SWEET_ECID = 0x4133305300000001  # 'A30S' + 1, synthetic
FB_W, FB_H, FB_STRIDE = 720, 1560, 720 * 4
# dwmmc0 (mmc@13500000): the eMMC S-BOOT boots from, with the SMU (FMP) block
MMC, MMC_SIZE = 0x13500000, 0x2000
# pinctrl alive bank GPA1 (CON, DAT at +4): keys, active low (bsd/dev/sweet_keys.c)
GPIO_KEYS, GPIO_KEYS_SIZE = 0x11cb0060, 0x20
# i2c_4 (s3c2440-type): the IST4050 touch controller at 0x50 (bsd/dev/sweet_ist.c)
IST_I2C, IST_I2C_SIZE = 0x13870000, 0x100
# ARM Mali-G71 MP2 (bsd/dev/sweet_mali.c, boot-arg sweetgpu=1)
MALI, MALI_SIZE = 0x11500000, 0x5000
# arm,armv8-timer: CNTFRQ is 26 MHz (OSCCLK); the virtual timer is PPI 11
TIMEBASE = 26000000
VTIMER_INTID = 27
# QEMU virt (-M virt,gic-version=2 -cpu max): GICD/GICC and the generic timer's CNTFRQ
QEMU_GICD, QEMU_GICC, QEMU_TIMEBASE = 0x08000000, 0x08010000, 62500000
QEMU_GIC_SIZE = 0x10000

BOOT_MANIFEST_HASH = hashlib.sha384(b'a30s boot manifest').digest()

# The iOS userland is the iPhone X (iPhone10,3 / D22AP) IPSW. MobileGestalt
# derives product type, device class, home-button-less UI, artwork etc. from
# the root identity and /product of the real D22AP device tree. That tree is
# an Apple artifact (kept out of git): extract it from the IPSW with
#   tools/remotezip.py URL DeviceTree.d22ap.im4p DIR
#   ipsw img4 im4p extract DIR/Firmware/all_flash/DeviceTree.d22ap.im4p -o d22ap.adt
D22AP_ADT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../research/ipsw-x/d22ap.adt')


def d22ap_product(path=D22AP_ADT):
    """/product properties of the real D22AP tree, without the syscfg
    placeholders iBoot would fill in (flag bit set) and the node bookkeeping."""
    if not os.path.exists(path):
        print(f'warning: {path} missing, no /product node', file=sys.stderr)
        return None
    root, _ = adt.parse(open(path, 'rb').read())
    for c in root.children:
        if dict((k, v) for k, f, v in c.props).get('name') == b'product\0':
            return [(k, v) for k, f, v in c.props if not f and k not in ('name', 'AAPL,phandle')]
    raise SystemExit(f'{path}: no /product node')

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def build(seed=None, qemu=False):
    """qemu=True: same tree, but with QEMU virt's GICv2 addresses, CNTFRQ and RAM (tools/qemu-sweet.py)."""
    phys_base, mem_size = (QEMU_PHYS_BASE if qemu else PHYS_BASE), MEM_SIZE
    gicd, gicd_size, gicc, gicc_size, timebase = ((QEMU_GICD, QEMU_GIC_SIZE, QEMU_GICC, QEMU_GIC_SIZE, QEMU_TIMEBASE)
                                                  if qemu else (GICD, GICD_SIZE, GICC, GICC_SIZE, TIMEBASE))
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(phys_base)),
        ('dram-size', u64(mem_size)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('firmware-version', cstr('a30s-shim')),
        ('system-firmware-version', cstr('a30s-shim')),
        # iBoot's SHA-384 of the boot manifest; /sbin/mount -P 1 exits unless
        # it exists (bind mounts /private/preboot/<hex>/usr/standalone/firmware,
        # tolerating a missing directory). Fixed stand-in value.
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        # libimg4 (cryptexd) aborts "failed to get crypto algorithm" without
        # it; real A11+ device trees say sha2-384.
        ('crypto-hash-method', cstr('sha2-384')),
        # MobileGestalt's screen class: display-scale (u32, default 0) comes
        # from /chosen, the pixel size from IOMobileFramebuffer. Scale 0 makes
        # SpringBoard's FBSDisplayMode "infxinf@0x" invalid. 720x1560 @2x =
        # 360x780 points (like iPhone X class devices).
        ('display-scale', u32(2)),
        # XNU derives IOPlatformUUID (gethostuuid) from these on arm; without
        # them CFPreferences cannot use kCFPreferencesCurrentHost (no ByHost
        # files), so e.g. DataMigration never records LastSystemVersion and
        # migrates on every boot. Synthetic ECID (not the phone's); chip-id
        # of the A11 that the iPhone X userland expects.
        ('unique-chip-id', struct.pack('<Q', SWEET_ECID)),
        ('chip-id', u32(0x8015)),
        # Security status as on a dev-fused, undemoted board (tuple 0,0,1,1):
        # MobileGestalt reads these from /chosen, and mobileactivationd's
        # DeviceType then takes its native "Hactivation" branch (state
        # Activated without an activation record; no SEP, no baseband here).
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        # Local boot policy (kern_csr.c): SIP relaxed like `csrutil disable`
        # (lp-sip0: untrusted kexts, unrestricted FS, task_for_pid, kernel
        # debugger, dtrace, nvram), lp-sip1: unauthenticated (unsealed) root.
        # The iOS shared cache is in no trust cache, so dyld can only map it
        # with CSR_ALLOW_UNRESTRICTED_FS (vm_unix.c shared region checks).
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    # a30s SMP: only the two Cortex-A73s (MPIDR 0x0, 0x1, cluster 0): the six
    # A53s have no 16K granule, so the shim wakes A73 0x0 and xnu runs there
    # (the A53 boot core parks). The kernel defaults to one CPU and uses
    # cpus=N to select an initial prefix. QEMU virt has one CPU, MPIDR 0x0
    # like the first A73. qcom,kryo470 stays: SweetCPU's IONameMatch.
    cpu_nodes = []
    for core in range(1 if qemu else 2):
        cpu_nodes.append(node(f'cpu{core}', [
            ph(),
            ('device_type', cstr('cpu')),
            ('compatible', cstr('arm,cortex-a73') + cstr('qcom,kryo470') + cstr('ARM,v8')),
            ('reg', u32(core)),  # actual MPIDR Aff1:Aff0, NOT logical ID
            ('cpu-id', u32(core)),   # descriptive: XNU assigns IDs by DT order
            ('cluster-id', u32(0)),  # descriptive; see ml_parse_cpu_topology
            ('cluster-type', u32(0)),  # homogeneous CLUSTER_TYPE_SMP, not AMP
            ('die-cluster-id', u32(0)),
            ('cluster-core-id', u32(core)),
            ('l2-cache-id', u32(0)),  # the A73 cluster's L2
            ('l3-cache-id', u32(0)),
            ('state', cstr('running' if core == 0 else 'stopped')),
            ('timebase-frequency', u32(timebase)),
            ('clock-frequency', u32(1768000000)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(1866000000)),
            ('peripheral-frequency', u32(timebase)),
            ('fixed-frequency', u32(timebase)),
        ]))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)),
                         ('#size-cells', u32(0))], cpu_nodes)

    # pe_fiq.c takes "arm,gic-400" as GICv2: reg = GICD base/size, GICC base/size
    gic = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-400')),
        ('interrupt-controller', b''),
        ('virtual-timer-intid', u32(VTIMER_INTID)),
        ('reg', u64(gicd - SOC_BASE, gicd_size, gicc - SOC_BASE, gicc_size)),
    ])
    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('sweet-io')),
        ('compatible', cstr('sweet-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, SOC_BASE, 0x100000000 - SOC_BASE)),
    ], [gic,
        # No hardware: watchdogd checks IODeviceTree:/arm-io/wdt for no-pmu and
        # otherwise crashes trying to set the PMU boot stage (customer builds
        # ignore the -wdt_no_pmu boot-arg).
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        # Main display: IOMobileFramebuffer.framework counts /arm-io children
        # with this device_type and reads the canvas size from the node
        # (the presented iPhone X size, see SweetFramebuffer.cpp);
        # the kernel's AppleCLCD matches the compatible string.
        # No "wlan" node: topaz's modem/Wi-Fi driver is Qualcomm hardware.
        # Canvas = the real 720x1560 panel for now; the iPhone X canvas
        # (1125x2436, scaled 0.64 into the panel) comes with the GUI.
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('sweet,display')),
            ('canvas-width', u32(FB_W)),
            ('canvas-height', u32(FB_H)),
        ])])

    fbcon = node('sweet-fbcon', [
        ph(),
        ('reg', u64(FB, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    # bsd/dev/sweet_ufs.c, eMMC transport: the DesignWare MMC host S-BOOT
    # leaves running (absolute physical reg). QEMU virt has none.
    mmc = node('sweet-mmc', [
        ph(),
        ('compatible', cstr('samsung,exynos7-dw-mshc-smu')),
        ('reg', u64(MMC, MMC_SIZE)),
    ])

    # bsd/dev/sweet_keys.c, GPIO keys: GPA1 5 volume up, 6 volume down, 7 power
    gpio_keys = node('sweet-gpio-keys', [
        ph(),
        ('compatible', cstr('samsung,exynos7885-gpa1-keys')),
        ('reg', u64(GPIO_KEYS, GPIO_KEYS_SIZE)),
    ])

    # bsd/dev/sweet_ist.c: IST4050 at 0x50 on i2c_4 (pins, supply and clock in the driver)
    ist_touch = node('sweet-ist-touch', [
        ph(),
        ('compatible', cstr('imagis,ist4050')),
        ('reg', u64(IST_I2C, IST_I2C_SIZE)),
    ])

    # bsd/dev/sweet_mali.c: the GPU (power domain and clocks in the driver)
    mali = node('sweet-mali', [
        ph(),
        ('compatible', cstr('arm,mali-bifrost')),
        ('reg', u64(MALI, MALI_SIZE)),
    ])

    # pmap_compute_max_asids requires the node even when no overrides are set.
    # An absent pmap-max-asids property selects XNU's own MAX_ASIDS default.
    # No effaceable storage: keybagd then reads /private/var/keybags/systembag.kb
    # as a plain plist instead of decrypting it with the effaceable key.
    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])

    # MobileGestalt HomeButtonType = IODeviceTree:/buttons home-button-type
    # (D22AP: 2, no Home button; absent = 0 = Home-button device).
    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(1)),
    ])

    product_props = d22ap_product()
    # Our framebuffer is sRGB 8-bit. With the iPhone X's 'P3' MobileGestalt says
    # HasExtendedColorDisplay and WebKit renders page tiles into 10-bit surfaces
    # ('w30r', 'b3a8') that this kernel does not provide: Safari pages stay blank.
    if product_props:
        product_props = [(k, cstr('sRGB') if k == 'artwork-display-gamut' else v) for k, v in product_props]
    product = [node('product', [ph()] + product_props)] if product_props else []

    # Identity of the iPhone X whose userland runs here: compatible[0] is
    # hw.model/hw.target (D22AP), model is hw.machine/hw.product
    # (iPhone10,3). 'xiaomi,sweet' and qcom,kryo470 stay because the
    # SweetPlatformExpert and CPU IONameMatch still use them.
    return node('device-tree', [
        ph(),
        ('compatible', cstr('D22AP') + cstr('iPhone10,3') + cstr('samsung,a30s') + cstr('xiaomi,sweet') + cstr('samsung,exynos7904') + cstr('AppleARM')),
        ('model', cstr('iPhone10,3')),
        ('target-type', cstr('D22')),
        ('target-sub-type', cstr('D22AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, buttons] + product + ([] if qemu else [mmc, gpio_keys, ist_touch, mali]))


if __name__ == '__main__':
    root = build(qemu='--qemu' in sys.argv)
    blob = adt.serialize(root)
    open(sys.argv[1], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{sys.argv[1]}: {len(blob)} bytes', file=sys.stderr)
