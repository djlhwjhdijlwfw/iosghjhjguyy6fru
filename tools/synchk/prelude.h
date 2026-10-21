/* Just enough of xnu's kernel API to syntax-check bsd/dev/sweet_ufs.c with clang (tools/synchk/run.sh). */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#define ARM64_BOARD_CONFIG_SWEET 1
#define assert(x) ((void)(x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TRUE 1
#define NSEC_PER_SEC 1000000000ull
#define NSEC_PER_MSEC 1000000ull
#define NSEC_PER_USEC 1000ull
#define PAGE_MASK 0x3fffu
#define PAGE_SIZE 0x4000u
#define DEV_BSIZE 512
#define PZERO 22
#define FWRITE 2
#define B_READ 1
#define B_WRITE 0
#define D_DISK 2
#define DEVFS_BLOCK 0
#define DEVFS_CHAR 1
#define UID_ROOT 0
#define GID_OPERATOR 5

typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
typedef uint32_t ppnum_t;
typedef uint64_t addr64_t;
typedef struct pmap *pmap_t;
extern pmap_t kernel_pmap;
typedef int kern_return_t;
#define KERN_SUCCESS 0
typedef struct thread *thread_t;
typedef int wait_result_t;
typedef void (*thread_continue_t)(void *, wait_result_t);
typedef void *event_t;
#define THREAD_UNINT 0
#define THREAD_WAITING (-1)
#define THREAD_CONTINUE_NULL ((thread_continue_t)0)
#define TIMEOUT_URGENCY_SYS_CRITICAL 2
typedef int32_t dev_t;
typedef struct buf *buf_t;
typedef char *caddr_t;
typedef unsigned long u_long;
struct proc;
struct uio;
typedef struct vm_map *vm_map_t;
extern vm_map_t kernel_map;
#define KMA_DATA 1
#define KMA_ZERO 2
#define KMA_PERMANENT 4
#define VM_KERN_MEMORY_IOKIT 1
#define ptoa_64(x) ((uint64_t)(x) << 14)

#define DKIOCGETBLOCKSIZE 1
#define DKIOCSETBLOCKSIZE 2
#define DKIOCGETPHYSICALBLOCKSIZE 3
#define DKIOCGETBLOCKCOUNT 4
#define DKIOCISWRITABLE 5
#define DKIOCSYNCHRONIZE 6
#define DKIOCSYNCHRONIZECACHE 7
#define DKIOCGETMAXBLOCKCOUNTREAD 8
#define DKIOCGETMAXBLOCKCOUNTWRITE 9
#define DKIOCGETMAXSEGMENTCOUNTREAD 10
#define DKIOCGETMAXSEGMENTCOUNTWRITE 11

typedef const void *DTEntry;
#define kSuccess 0
int SecureDTLookupEntry(DTEntry, const char *, DTEntry *);
int SecureDTGetProperty(DTEntry, const char *, void const **, unsigned int *);
bool PE_parse_boot_argn(const char *, void *, int);

int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
void panic(const char *, ...) __attribute__((noreturn, format(printf, 1, 2)));
size_t strlcpy(char *, const char *, size_t);
void bzero(void *, size_t);
void delay(int);
uint64_t mach_absolute_time(void);
void nanoseconds_to_absolutetime(uint64_t, uint64_t *);
void absolutetime_to_nanoseconds(uint64_t, uint64_t *);
void clock_interval_to_deadline(uint32_t, uint32_t, uint64_t *);
wait_result_t assert_wait_deadline(event_t, int, uint64_t);
wait_result_t assert_wait_deadline_with_leeway(event_t, int, int, uint64_t, uint64_t);
wait_result_t thread_block(thread_continue_t);
int tsleep(void *, int, const char *, int);
kern_return_t kmem_alloc_contig(vm_map_t, vm_offset_t *, vm_size_t, vm_offset_t, ppnum_t, ppnum_t, int, int);
void flush_dcache64(addr64_t, unsigned, int);
void clean_dcache64(addr64_t, unsigned, int);
bool ml_get_interrupts_enabled(void);
bool ml_at_interrupt_context(void);

uint64_t buf_blkno(buf_t);
uint32_t buf_count(buf_t);
int buf_flags(buf_t);
void buf_setresid(buf_t, uint32_t);
int buf_map(buf_t, caddr_t *);
int buf_unmap(buf_t);
void buf_seterror(buf_t, int);
void buf_biodone(buf_t);
typedef void strategy_fcn_t(buf_t);
int physio(strategy_fcn_t *, buf_t, dev_t, int, void (*)(buf_t), struct uio *, int);
void minphys(buf_t);

typedef int open_close_fcn_t(dev_t, int, int, struct proc *);
typedef int ioctl_fcn_t(dev_t, u_long, caddr_t, int, struct proc *);
typedef int psize_fcn_t(dev_t);
typedef int read_write_fcn_t(dev_t, struct uio *, int);
int eno_dump(void);
int eno_stop(void);
int eno_reset(void);
int eno_select(void);
int eno_mmap(void);
int eno_strat(void);
int eno_getc(void);
int eno_putc(void);
struct bdevsw {
	open_close_fcn_t *d_open, *d_close;
	strategy_fcn_t *d_strategy;
	ioctl_fcn_t *d_ioctl;
	int (*d_dump)(void);
	psize_fcn_t *d_psize;
	int d_type;
};
struct cdevsw {
	open_close_fcn_t *d_open, *d_close;
	read_write_fcn_t *d_read, *d_write;
	ioctl_fcn_t *d_ioctl;
	int (*d_stop)(void), (*d_reset)(void);
	void *d_ttys;
	int (*d_select)(void), (*d_mmap)(void), (*d_strategy)(void);
	int (*d_reserved_1)(void), (*d_reserved_2)(void);
	int d_type;
};
int bdevsw_add(int, const struct bdevsw *);
int cdevsw_add_with_bdev(int, const struct cdevsw *, int);
#define makedev(x, y) ((dev_t)(((x) << 24) | (y)))
#define major(x) ((int32_t)(((uint32_t)(x) >> 24) & 0xff))
#define minor(x) ((int32_t)((x) & 0xffffff))
void *devfs_make_node(dev_t, int, int, int, int, const char *, ...);

typedef struct lck_grp lck_grp_t;
typedef struct { int x; } lck_mtx_t;
#define LCK_GRP_DECLARE(n, s) __attribute__((unused)) lck_grp_t *n
#define LCK_MTX_DECLARE(n, g) lck_mtx_t n
void lck_mtx_lock(lck_mtx_t *);
void lck_mtx_unlock(lck_mtx_t *);
#define os_atomic_add_orig(p, v, m) __c11_atomic_fetch_add((p), (v), __ATOMIC_RELAXED)
#define os_atomic_load(p, m) __c11_atomic_load((p), __ATOMIC_RELAXED)
#define __unused __attribute__((unused))
kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
#define EPERM 1
#define ENOENT 2
#define EIO 5
#define ENXIO 6
#define EAGAIN 35
#define ENOMEM 12
#define EBUSY 16
#define ENODEV 19
#define EINVAL 22
#define ENOTTY 25
#define ENOSPC 28
#define EROFS 30
#define ETIMEDOUT 60
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
size_t strlen(const char *);
char *strchr(const char *, int);
void thread_deallocate(thread_t);
