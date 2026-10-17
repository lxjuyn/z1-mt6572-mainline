/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Z1 Android 9 内核能力探针 —— 无 libc（-nostdlib），裸 ARM EABI syscall。
 *
 * 目的：在没有 AOSP userspace 的前提下，仅靠已验证的 mainline 启动链（initramfs + busybox
 * /init）取得内核 Android 能力面的板级真值，逐行输出：
 *     RESULT_STEP_<名>=PASS|FAIL(<负errno>) [细节]
 * 让"A9 前置是否满足"变成可 grep 的证据而不是推测。
 *
 * 环境约束（本机实测）：交叉工具链无 armhf 静态 libc（缺 libc.a 与 crti.o），故自带
 * syscall 包装、字符串与数字格式化；主机没有 qemu，因此同一份源码再编一个 x86_64
 * "原生孪生"在主机真跑一遍，用以验证格式化/解析/errno 打印路径（binder 一类在主机
 * 必然 FAIL 属预期），syscall 号与 ioctl 宏则由内核 uapi 头在编译期校验。
 *
 * 编译（先 make headers_install）：
 *   arm-linux-gnueabihf-gcc -ffreestanding -nostdlib -nostartfiles -static -Os -Wall \
 *       -I$HDR/include -o z1_a9_probe z1_a9_probe.c
 *   gcc -O1 -Wall -o /tmp/a9_probe_native z1_a9_probe.c   # 原生孪生，仅用于主机自测
 */

#include <linux/types.h>
#include <asm/unistd.h>

#if __has_include(<linux/ioctl.h>)
#include <linux/ioctl.h>
#endif

/* ashmem ioctl：ARM 构建用内核 uapi 头；主机缺该头时用等价常量（仅影响原生孪生） */
#if __has_include(<linux/ashmem.h>)
#include <linux/ashmem.h>
#else
#define __ASHMEMIOC		0x77
#define ASHMEM_SET_NAME		_IOW(__ASHMEMIOC, 1, char[32])
#define ASHMEM_GET_NAME		_IOR(__ASHMEMIOC, 2, char[32])
#define ASHMEM_SET_SIZE		_IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE		_IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK	_IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK	_IO(__ASHMEMIOC, 6)
#endif

/* binder 协议版本 ioctl（来源：include/uapi/linux/android/binder.h，_IOWR('b',9,...)） */
#if __has_include(<linux/android/binder.h>)
#include <linux/android/binder.h>
#else
struct binder_version { int protocol_version; };
#define BINDER_VERSION _IOWR('b', 9, struct binder_version)
#endif

/* dma-heap 分配 ioctl（来源：include/uapi/linux/dma-heap.h） */
#if __has_include(<linux/dma-heap.h>)
#include <linux/dma-heap.h>
#else
struct dma_heap_allocation_data {
	unsigned long long len;
	unsigned int fd;
	unsigned int fd_flags;
	unsigned long long heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0, struct dma_heap_allocation_data)
#endif

/* ---- 常量（值取自本树 uapi 头，注释标明来源） ---- */
#define MY_CLOCK_REALTIME       0
#define MY_CLOCK_BOOTTIME       7   /* include/uapi/linux/time.h: CLOCK_BOOTTIME = 7 */
#define MY_CLOCK_BOOTTIME_ALARM 9   /* CLOCK_BOOTTIME_ALARM = 9 */

#define MY_O_RDONLY  0
#define MY_O_WRONLY  1
#define MY_O_RDWR    2
#define MY_O_CREAT   0100
#define MY_EBUSY     16

#define MY_PR_GET_SECCOMP     21
#define MY_PR_GET_NO_NEW_PRIVS 38

#ifndef NULL
#define NULL ((void *)0)
#endif

struct my_timespec { long tv_sec; long tv_nsec; };
struct my_itimerspec { struct my_timespec it_interval; struct my_timespec it_value; };

/* ---------------- syscall 层 ----------------
 * ARM：裸 svc（无 libc 可用）。x86_64：借 glibc syscall() 做原生孪生。
 */
#ifdef __arm__
static long sc1(long n, long a)
{
	register long r7 asm("r7") = n;
	register long r0 asm("r0") = a;
	asm volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
	return r0;
}

static long sc2(long n, long a, long b)
{
	register long r7 asm("r7") = n;
	register long r0 asm("r0") = a;
	register long r1 asm("r1") = b;
	asm volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory", "cc");
	return r0;
}

static long sc3(long n, long a, long b, long c)
{
	register long r7 asm("r7") = n;
	register long r0 asm("r0") = a;
	register long r1 asm("r1") = b;
	register long r2 asm("r2") = c;
	asm volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory", "cc");
	return r0;
}

static long sc4(long n, long a, long b, long c, long d)
{
	register long r7 asm("r7") = n;
	register long r0 asm("r0") = a;
	register long r1 asm("r1") = b;
	register long r2 asm("r2") = c;
	register long r3 asm("r3") = d;
	asm volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3) : "memory", "cc");
	return r0;
}

static long sc5(long n, long a, long b, long c, long d, long e)
{
	register long r7 asm("r7") = n;
	register long r0 asm("r0") = a;
	register long r1 asm("r1") = b;
	register long r2 asm("r2") = c;
	register long r3 asm("r3") = d;
	register long r4 asm("r4") = e;
	asm volatile("svc #0" : "+r"(r0)
		     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "memory", "cc");
	return r0;
}
#else
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
/* 约定对齐：ARM 裸 svc 直接返回 -errno；glibc syscall() 返回 -1 并把错误放 errno。
 * 原生孪生必须归一化成 -errno，否则 FAIL(-1) 不带任何信息量。 */
static long nrm(long r)
{
	return r == -1 ? -(long)errno : r;
}
static long sc1(long n, long a) { return nrm(syscall(n, a)); }
static long sc2(long n, long a, long b) { return nrm(syscall(n, a, b)); }
static long sc3(long n, long a, long b, long c) { return nrm(syscall(n, a, b, c)); }
static long sc4(long n, long a, long b, long c, long d) { return nrm(syscall(n, a, b, c, d)); }
static long sc5(long n, long a, long b, long c, long d, long e)
{
	return nrm(syscall(n, a, b, c, d, e));
}
#endif

/* open 的架构差异：ARM EABI 有 __NR_open；x86_64 没有（只有 openat）。
 * 原生孪生必须走 openat，否则编译期就没有这个符号。 */
#ifdef __arm__
#define my_open3(path, flags, mode) sc3(__NR_open, (long)(path), (long)(flags), (long)(mode))
#else
#include <fcntl.h>
#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
/* openat 有 4 个参数，必须用 sc4 */
#define my_open3(path, flags, mode) \
	sc4(__NR_openat, (long)AT_FDCWD, (long)(path), (long)(flags), (long)(mode))
#endif

#define fd_open(p)    my_open3(p, MY_O_RDONLY, 0)
#define fd_open_rw(p) my_open3(p, MY_O_RDWR, 0)
#define fd_create(p)  my_open3(p, MY_O_WRONLY | MY_O_CREAT, 0600)
#define fd_close(f)   sc1(__NR_close, (f))
#define rd(f, b, n)   sc3(__NR_read, (long)(f), (long)(b), (long)(n))
#define ioc(f, c, a)  sc3(__NR_ioctl, (long)(f), (long)(c), (long)(a))

/* -nostdlib 链接桩：libgcc 的 __aeabi_ldiv0 引用 raise()，该路径运行时不会走到。
 * 仅为满足链接，永不返回正常语义。 */
#ifndef __arm__
int raise(int sig) { (void)sig; return -1; }
#else
int raise(int sig) { (void)sig; sc1(__NR_exit_group, 137); for (;;) { } return -1; }
#endif

/* ---------------- 行缓冲输出：'\n' 立即 write，防早期崩溃丢日志 ---------------- */
static unsigned int outpos;
static char outbuf[512];

static void flush(void)
{
	if (outpos) {
		long rc = sc3(__NR_write, 1, (long)outbuf, (long)outpos);
		(void)rc;
		outpos = 0;
	}
}

static void putch(char c)
{
	outbuf[outpos++] = c;
	if (c == '\n' || outpos >= sizeof(outbuf) - 1)
		flush();
}

static void ps(const char *s)
{
	while (*s)
		putch(*s++);
}

static void pd(long v)
{
	char t[24];
	int i = 0;
	if (v < 0) {
		putch('-');
		v = -v;
	}
	do {
		t[i++] = (char)('0' + (v % 10));
		v /= 10;
	} while (v && i < (int)sizeof(t));
	while (i--)
		putch(t[i]);
}

static void pass(const char *name, const char *tail)
{
	ps("RESULT_STEP_");
	ps(name);
	ps("=PASS");
	if (tail) {
		putch(' ');
		ps(tail);
	}
	putch('\n');
}

static void fail(const char *name, long rc)
{
	ps("RESULT_STEP_");
	ps(name);
	ps("=FAIL(");
	pd(rc);
	ps(")\n");
}

static void info(const char *name, long v)
{
	ps("RESULT_STEP_");
	ps(name);
	ps("=INFO value=");
	pd(v);
	putch('\n');
}

/* ---------------- 字符串小工具 ---------------- */
static int find_sub(const char *hay, int hlen, const char *needle)
{
	int nl = 0, i, j;
	while (needle[nl])
		nl++;
	if (nl == 0 || hlen < nl)
		return -1;
	for (i = 0; i + nl <= hlen; i++) {
		for (j = 0; j < nl; j++)
			if (hay[i + j] != needle[j])
				break;
		if (j == nl)
			return i;
	}
	return -1;
}

/* /proc/cgroups 每控制器 enabled 位：1=启用 0=出现但 hierarchy 为 0 -1=未出现 */
static int cgroup_enabled(const char *buf, int len, const char *ctrl)
{
	int p = find_sub(buf, len, ctrl);
	int e = p;
	if (p < 0)
		return -1;
	while (e < len && buf[e] != '\n')
		e++;
	while (e > p && (buf[e - 1] == '\n' || buf[e - 1] == ' ' || buf[e - 1] == '\t'))
		e--;
	if (e <= p)
		return -1;
	return buf[e - 1] == '1' ? 1 : 0;
}

static int slurp(const char *path, char *buf, int cap, int *out_len)
{
	long fd = fd_open(path);
	long n;
	if (fd < 0)
		return (int)fd;
	n = rd(fd, buf, cap - 1);
	fd_close(fd);
	if (n < 0)
		return (int)n;
	buf[n] = 0;
	*out_len = (int)n;
	return 0;
}

/* ---------------- 具体探测 ---------------- */
static void probe_binder(const char *name, const char *path, int want_version)
{
	long fd = fd_open_rw(path);
	if (fd < 0) {
		fail(name, fd);
		return;
	}
	if (want_version) {
		struct binder_version bv;
		long rc;
		bv.protocol_version = -1;
		rc = ioc(fd, BINDER_VERSION, &bv);
		ps("RESULT_STEP_");
		ps(name);
		if (rc == 0) {
			ps("=PASS protocol_version=");
			pd(bv.protocol_version);
		} else {
			ps("=PASS(open) ioctl=");
			pd(rc);
		}
		putch('\n');
	} else {
		pass(name, path);
	}
	fd_close(fd);
}

static void probe_ashmem(void)
{
	long fd = fd_open_rw("/dev/ashmem");
	long rc;
	if (fd < 0) {
		fail("ashmem_open", fd);
		return;
	}
	/* ASHMEM_SET_PROT_MASK 的参数按值传：
	 * drivers/android/ashmem.c 里 case ASHMEM_SET_PROT_MASK: ret = set_prot_mask(asma, arg)
	 */
	rc = ioc(fd, ASHMEM_SET_PROT_MASK, 3 /* READ|WRITE */);
	if (rc == 0) {
		ps("RESULT_STEP_ashmem=PASS set_prot_mask=0 get_prot_mask=");
		pd(ioc(fd, ASHMEM_GET_PROT_MASK, 0));
		putch('\n');
	} else {
		ps("RESULT_STEP_ashmem=PASS(open) set_prot_mask=");
		pd(rc);
		putch('\n');
	}
	{
		static char nm[256] = "z1-a9-probe"; /* static 避免 -nostdlib 下触发 memset */
		long r2 = ioc(fd, ASHMEM_SET_NAME, &nm);
		long r3 = ioc(fd, ASHMEM_SET_SIZE, 4096);
		ps("RESULT_STEP_ashmem_name_size=INFO set_name=");
		pd(r2);
		ps(" set_size=");
		pd(r3);
		putch('\n');
	}
	fd_close(fd);
}

static void probe_dma_heap(void)
{
	long fd = fd_open_rw("/dev/dma_heap/system");
	struct dma_heap_allocation_data ad;
	long rc;
	if (fd < 0) {
		fail("dma_heap_system_open", fd);
		return;
	}
	pass("dma_heap_system_open", NULL);
	ad.len = 4096;
	ad.fd = 0;
	ad.fd_flags = MY_O_RDWR;
	ad.heap_flags = 0;
	rc = ioc(fd, DMA_HEAP_IOCTL_ALLOC, &ad);
	if (rc == 0 && (long)ad.fd > 0) {
		pass("dma_heap_alloc", "/* dma-buf fd 已取回 */");
		fd_close((long)ad.fd);
	} else {
		fail("dma_heap_alloc", rc ? rc : -999);
	}
	fd_close(fd);
}

static void probe_timerfd(void)
{
	long fd = sc2(__NR_timerfd_create, MY_CLOCK_BOOTTIME, 0);
	if (fd < 0) {
		fail("timerfd_boottime", fd);
	} else {
		struct my_itimerspec its;
		long rc;
		pass("timerfd_create_boottime", NULL);
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 1;
		its.it_value.tv_nsec = 0;
		rc = sc3(__NR_timerfd_settime, fd, 0, (long)&its);
		if (rc == 0)
			pass("timerfd_settime_boottime", NULL);
		else
			fail("timerfd_settime_boottime", rc);
		fd_close(fd);
	}
	{
		long fa = sc2(__NR_timerfd_create, MY_CLOCK_BOOTTIME_ALARM, 0);
		if (fa >= 0) {
			pass("timerfd_boottime_alarm", "/* stock 3.4.67 上 A9 回落用的就是它 */");
			fd_close(fa);
		} else {
			fail("timerfd_boottime_alarm", fa);
		}
	}
	{
		long fr = sc2(__NR_timerfd_create, MY_CLOCK_REALTIME, 0);
		if (fr >= 0) {
			pass("timerfd_realtime_control", NULL);
			fd_close(fr);
		} else {
			fail("timerfd_realtime_control", fr);
		}
	}
}

static void cg_line(const char *name, int en)
{
	if (en == 1) {
		pass(name, NULL);
	} else if (en == 0) {
		ps("RESULT_STEP_cgroup_");
		ps(name);
		ps("=FAIL(-1000) /* 出现但 hierarchy=0, 未被挂载支持 */\n");
	} else {
		ps("RESULT_STEP_cgroup_");
		ps(name);
		ps("=FAIL(-1001) /* 未出现在 /proc/cgroups */\n");
	}
}

static void probe_cgroups(void)
{
	static char buf[4096];
	int len = 0, en;
	int rc = slurp("/proc/cgroups", buf, (int)sizeof(buf), &len);
	if (rc != 0) {
		fail("cgroups_proc", rc);
		return;
	}
	ps("RESULT_STEP_cgroups_proc=PASS bytes=");
	pd(len);
	putch('\n');
	/* 注意：控制器名可能同时出现在其它行（如 "cpu,cpuacct"），取整行末位判定 */
	en = cgroup_enabled(buf, len, "freezer"); cg_line("freezer", en);
	en = cgroup_enabled(buf, len, "cpuset"); cg_line("cpuset", en);
	en = cgroup_enabled(buf, len, "cpuacct"); cg_line("cpuacct", en);
	en = cgroup_enabled(buf, len, "memory"); cg_line("memory", en);
	en = cgroup_enabled(buf, len, "devices"); cg_line("devices", en);
	en = cgroup_enabled(buf, len, "pids"); cg_line("pids", en);
}

static void probe_selinux(void)
{
	long rc = sc5(__NR_mount, (long)"none", (long)"/sys/fs/selinux",
		      (long)"securityfs", 0, 0);
	if (rc == 0)
		pass("selinux_securityfs_mount", NULL);
	else if (rc == -MY_EBUSY)
		pass("selinux_securityfs_mount", "/* 已由 init 挂载 */");
	else
		fail("selinux_securityfs_mount", rc);
	{
		static char buf[8];
		int len = 0;
		int e = slurp("/sys/fs/selinux/enforce", buf, (int)sizeof(buf), &len);
		if (e == 0 && len > 0) {
			ps("RESULT_STEP_selinux_enforce=PASS value=");
			putch(buf[0]);
			putch('\n');
		} else {
			fail("selinux_enforce", e);
		}
	}
	{
		long fd = fd_open("/sys/fs/selinux/policy");
		if (fd >= 0) {
			pass("selinux_policy_loaded", NULL);
			fd_close(fd);
		} else {
			fail("selinux_policy_loaded", fd);
		}
	}
}

static void probe_xattr(void)
{
	const char *dir = "/mnt";
	const char *file = "/mnt/a9_probe_xattr";
	static char lbl[] = "u:object_r:a9_probe:s0";
	long rc;

	sc2(__NR_mkdir, (long)dir, 0755);
	rc = sc5(__NR_mount, (long)"tmpfs", (long)dir, (long)"tmpfs", 0, 0);
	if (rc != 0 && rc != -MY_EBUSY) {
		fail("xattr_tmpfs_mount", rc);
		return;
	}
	{
		long fd = fd_create(file);
		if (fd < 0) {
			fail("xattr_create_file", fd);
			return;
		}
		fd_close(fd);
	}
	rc = sc4(__NR_setxattr, (long)file, (long)lbl, (long)sizeof(lbl), 0);
	if (rc == 0)
		pass("xattr_security_selinux", "/* TMPFS_XATTR 可用 */");
	else
		fail("xattr_security_selinux", rc);
	sc1(__NR_unlink, (long)file);
}

static void probe_seccomp(void)
{
	static char buf[256];
	int len = 0;
	int rc = slurp("/proc/sys/kernel/seccomp/actions_avail", buf, (int)sizeof(buf), &len);
	if (rc == 0 && len > 0)
		pass("seccomp_filter", "/* actions_avail 可读, minijail 可用 */");
	else
		fail("seccomp_filter", rc);
	info("prctl_get_seccomp", sc2(__NR_prctl, MY_PR_GET_SECCOMP, 0));
	info("prctl_no_new_privs", sc2(__NR_prctl, MY_PR_GET_NO_NEW_PRIVS, 0));
}

static void probe_misc(void)
{
	long fd = fd_open("/proc/config.gz");
	if (fd >= 0) {
		pass("config_gz_present", "/* 板端可自证编译配置 */");
		fd_close(fd);
	} else {
		fail("config_gz_present", fd);
	}

	fd = fd_open_rw("/dev/ion");   /* 主线已删 ION：预期 FAIL(-2) */
	if (fd >= 0) {
		pass("ion_dev", "/* 意外存在，需记录来源 */");
		fd_close(fd);
	} else {
		fail("ion_dev", fd);
	}

	fd = fd_open_rw("/dev/sw_sync"); /* 旧 staging sync；3.4.67 有，主线预期无 */
	if (fd >= 0) {
		pass("sw_sync_dev", "/* 旧 staging sync */");
		fd_close(fd);
	} else {
		fail("sw_sync_dev", fd);
	}

	{
		static char buf[2048];
		int len = 0;
		int e = slurp("/proc/filesystems", buf, (int)sizeof(buf), &len);
		if (e == 0) {
			if (find_sub(buf, len, "binder") >= 0)
				ps("RESULT_STEP_binderfs_registered=PRESENT\n");
			else
				ps("RESULT_STEP_binderfs_registered=ABSENT /* 与 ANDROID_BINDERFS=n 一致 */\n");
		} else {
			fail("filesystems_list", e);
		}
	}
	{
		long m = fd_open("/dev/loop0");
		if (m >= 0) {
			pass("loop_dev_present", "/* dm-verity 前置 */");
			fd_close(m);
		} else {
			fail("loop_dev_present", m);
		}
	}
	{   /* LMK 通路：A9 低内存管理要写 oom_score_adj */
		long m = my_open3("/proc/self/oom_score_adj", MY_O_WRONLY, 0);
		if (m >= 0) {
			pass("oom_score_adj_writable", NULL);
			fd_close(m);
		} else {
			fail("oom_score_adj_writable", m);
		}
	}
}

#ifdef __arm__
void _start(void)
#else
int main(void)
#endif
{
	ps("[a9probe] ===== Z1 Android 9 内核能力探针开始 =====\n");
	ps("[a9probe] FAIL(-N) 中 N 为内核返回的 -errno 原值; -1000/-1001 为本探针自定义语义\n");

	probe_binder("binder", "/dev/binder", 1);
	probe_binder("hwbinder", "/dev/hwbinder", 0);
	probe_binder("vndbinder", "/dev/vndbinder", 0);
	probe_ashmem();
	probe_dma_heap();
	probe_timerfd();
	probe_cgroups();
	probe_selinux();
	probe_xattr();
	probe_seccomp();
	probe_misc();

	ps("[a9probe] ===== 探针结束 =====\n");
	flush();
#ifndef __arm__
	return 0;
#else
	sc1(__NR_exit_group, 0);
	for (;;)
		;
#endif
}
