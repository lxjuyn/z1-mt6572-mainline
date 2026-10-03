/* Read-only Android early-boot evidence; no system binaries required. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <poll.h>
#include <stdint.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <termios.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int logfd = -1;

/* At most 89 bytes before newline, including the visible prefix. */
static void logmsg(const char *fmt, ...)
{
    char msg[1024], line[96];
    va_list ap;
    size_t off, len;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    len = strlen(msg);
    for (off = 0; off < len; ) {
        size_t count = len - off;
        if (count > 80) count = 80;
        int n = snprintf(line, sizeof(line), "[z1diag] %.*s\n", (int)count, msg + off);
        ssize_t written = write(logfd >= 0 ? logfd : STDERR_FILENO, line, (size_t)n);
        if (written < 0) return;
        off += count;
    }
}

static void dumptext(const char *path)
{
    char buf[256], row[512];
    size_t used = 0;
    ssize_t n;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    logmsg("text %s", path);
    if (fd < 0) { logmsg("open errno=%d", errno); return; }
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t i;
        for (i = 0; i < n; ++i) {
            if (buf[i] == '\n' || used == sizeof(row) - 1) {
                row[used] = 0;
                logmsg("%s", row);
                used = 0;
            }
            if (buf[i] != '\n') row[used++] = buf[i];
        }
    }
    if (n < 0) logmsg("read errno=%d", errno);
    if (used) { row[used] = 0; logmsg("%s", row); }
    close(fd);
}

static void pathstat(const char *path)
{
    struct stat s;
    char target[256];
    ssize_t n;
    logmsg("path %s", path);
    if (lstat(path, &s) < 0) logmsg("lstat errno=%d", errno);
    else {
        logmsg("lstat mode=%#o size=%lld", (unsigned)s.st_mode, (long long)s.st_size);
        if (S_ISLNK(s.st_mode)) {
            n = readlink(path, target, sizeof(target) - 1);
            if (n < 0) logmsg("readlink errno=%d", errno);
            else { target[n] = 0; logmsg("link=%s", target); }
        }
    }
    if (stat(path, &s) < 0) logmsg("stat errno=%d", errno);
    else logmsg("stat mode=%#o size=%lld", (unsigned)s.st_mode, (long long)s.st_size);
}

static void ext4magic(const char *path)
{
    unsigned char magic[2];
    struct stat s;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    ssize_t n;
    logmsg("ext4 %s offset=1080", path);
    if (fd < 0) { logmsg("open errno=%d", errno); return; }
    if (fstat(fd, &s) < 0) logmsg("fstat errno=%d", errno);
    else if (!S_ISBLK(s.st_mode)) logmsg("not a block device; skip read");
    else {
        n = pread(fd, magic, sizeof(magic), 1080);
        if (n < 0) logmsg("pread errno=%d", errno);
        else if (n != 2) logmsg("short read=%ld", (long)n);
        else logmsg("magic=%02x%02x expected=53ef match=%d", magic[0], magic[1],
                    magic[0] == 0x53 && magic[1] == 0xef);
    }
    close(fd);
}


/* The ACM path deliberately depends only on ramdisk and kernel interfaces. */
#define GADGET_ROOT "/config/usb_gadget"
#define DIAG_GADGET GADGET_ROOT "/z1diag"
#define HISTORY_SIZE (2U * 1024U * 1024U)
#ifndef DIAG_FIFO
#define DIAG_FIFO "/dev/z1_diag_stream"
#endif
#ifndef TOMBSTONE_DIR
#define TOMBSTONE_DIR "/data/tombstones"
#endif
#define TEXT_CHUNK 3000U
#define TOMBSTONE_LIMIT (4U * 1024U * 1024U)
#define TOMBSTONE_TOTAL_LIMIT (16U * 1024U * 1024U)
#ifndef GETPROP_PATH
#define GETPROP_PATH "/system/bin/getprop"
#endif
#ifndef PROC_MOUNTS_PATH
#define PROC_MOUNTS_PATH "/proc/mounts"
#endif
#ifndef POWER_SUPPLY_PATH
#define POWER_SUPPLY_PATH "/sys/class/power_supply"
#endif
#define STATUS_TIMEOUT_MS 15000U
#define PROPERTY_TIMEOUT_MS 1000U

static uint64_t monotonic_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000U + (unsigned)t.tv_nsec / 1000000U;
}

/* FIFO records fit PIPE_BUF (4096), so concurrent writers cannot interleave.
 * This transport never writes logcat into kmsg: printk_devkmsg ratelimiting
 * silently discarded the old relay's crash records. A full FIFO backpressures
 * writers instead. The ACM reader still works without /system or logd. */
static int stream_open(void)
{
    struct stat st;
    if (lstat(DIAG_FIFO, &st) < 0 || !S_ISFIFO(st.st_mode)) {
        errno = ENODEV;
        return -1;
    }
    return open(DIAG_FIFO, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
}

static int stream_write(int fd, const char *data, size_t size)
{
    ssize_t n;
    do n = write(fd, data, size); while (n < 0 && errno == EINTR);
    if (n == (ssize_t)size) return 0;
    if (n >= 0) errno = EIO;
    return -1;
}

/* '+' means another fragment of the same original line follows; '=' ends
 * that line. Strip prefix/tag and concatenate fragments to reconstruct it.
 * Byte content is preserved, including UTF-8 and tabs; no 145-byte cropping. */
static int stream_line(int fd, const char *label, const char *line,
                       size_t count, int continues)
{
    char msg[TEXT_CHUNK + 80];
    int prefix = snprintf(msg, sizeof(msg), "[%s]%c ", label, continues ? '+' : '=');
    if (prefix < 0 || prefix > 70 || count > TEXT_CHUNK) { errno = EINVAL; return -1; }
    memcpy(msg + prefix, line, count);
    msg[prefix + count] = '\n';
    return stream_write(fd, msg, (size_t)prefix + count + 1);
}

static int stream_text(int fd, const char *label, int input, uint64_t limit,
                       uint64_t *total)
{
    char chunk[4096], row[TEXT_CHUNK];
    size_t used = 0;
    uint64_t read_bytes = 0;
    for (;;) {
        ssize_t n, i;
        size_t want = sizeof(chunk);
        if (limit && read_bytes >= limit) break;
        if (limit && want > limit - read_bytes) want = (size_t)(limit - read_bytes);
        do n = read(input, chunk, want); while (n < 0 && errno == EINTR);
        if (n < 0) return -1;
        if (!n) break;
        read_bytes += (uint64_t)n;
        for (i = 0; i < n; ++i) {
            if (chunk[i] == '\n') {
                if (stream_line(fd, label, row, used, 0)) return -1;
                used = 0;
            } else {
                if (used == sizeof(row)) {
                    if (stream_line(fd, label, row, used, 1)) return -1;
                    used = 0;
                }
                row[used++] = chunk[i];
            }
        }
    }
    if (used && stream_line(fd, label, row, used, 0)) return -1;
    if (total) *total = read_bytes;
    return 0;
}

/* T is a fixed, read-only request, never a shell. Snapshot file sizes before
 * reading, refuse symlinks/nonregular files and report the explicit size cap.
 * A child keeps filesystem reads/backpressure out of the early ACM loop. */
static int tombstone_dump(void)
{
    int stream, dirfd, index;
    unsigned found = 0;
    uint64_t exported = 0;
    alarm(120);
    stream = stream_open();
    if (stream < 0) return 1;
    dirfd = open(TOMBSTONE_DIR, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dirfd < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "directory unavailable errno=%d", errno);
        stream_line(stream, "z1tombstone", msg, strlen(msg), 0);
        close(stream);
        return 1;
    }
    for (index = 0; index < 32; ++index) {
        char name[32], msg[192];
        struct stat st;
        uint64_t limit, bytes = 0;
        int fd, result;
        snprintf(name, sizeof(name), "tombstone_%02d", index);
        fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) { close(fd); continue; }
        ++found;
        limit = (uint64_t)st.st_size;
        if (limit > TOMBSTONE_LIMIT) limit = TOMBSTONE_LIMIT;
        if (limit > TOMBSTONE_TOTAL_LIMIT - exported)
            limit = TOMBSTONE_TOTAL_LIMIT - exported;
        snprintf(msg, sizeof(msg), "BEGIN %s size=%lld cap=%u", name,
                 (long long)st.st_size, TOMBSTONE_LIMIT);
        if (stream_line(stream, "z1tombstone", msg, strlen(msg), 0)) { close(fd); break; }
        result = limit ? stream_text(stream, "z1tombstone", fd, limit, &bytes) : 0;
        exported += bytes;
        close(fd);
        snprintf(msg, sizeof(msg), "END %s read=%llu status=%s capped=%d", name,
                 (unsigned long long)bytes, result ? "ERROR" : "OK",
                 (uint64_t)st.st_size > limit);
        if (stream_line(stream, "z1tombstone", msg, strlen(msg), 0)) break;
        if (exported >= TOMBSTONE_TOTAL_LIMIT) {
            static const char capped[] = "TOTAL LIMIT reached (16 MiB); remaining files not exported";
            stream_line(stream, "z1tombstone", capped, sizeof(capped) - 1, 0);
            break;
        }
    }
    {
        char msg[80];
        snprintf(msg, sizeof(msg), "DONE files=%u", found);
        stream_line(stream, "z1tombstone", msg, strlen(msg), 0);
    }
    close(dirfd);
    close(stream);
    return 0;
}

/* S has fixed property keys and paths, no shell, no user-controlled argv.
 * At most 6 properties x 256 bytes, 16 KiB mounts and 8 supplies x 4 x
 * 256 bytes plus metadata (<32 KiB total). ACM supervises this child for
 * 15 seconds and each getprop child has its own 1-second read deadline. */
static int status_emit(int stream, const char *fmt, ...)
{
    char row[768];
    va_list ap;
    size_t i, size;
    va_start(ap, fmt);
    vsnprintf(row, sizeof(row), fmt, ap);
    va_end(ap);
    size = strlen(row);
    for (i = 0; i < size; ++i)
        if ((unsigned char)row[i] < ' ' || row[i] == 127) row[i] = ' ';
    return stream_line(stream, "z1status", row, size, 0);
}

static int status_property(int stream, const char *key)
{
    int channels[2], status = 0, error = 0, finished = 0, limited, reaped = 0;
    pid_t child;
    char value[257];
    size_t used = 0;
    uint64_t deadline = monotonic_ms() + PROPERTY_TIMEOUT_MS, reap_deadline;
    if (pipe2(channels, O_CLOEXEC))
        return status_emit(stream, "PROP key=%s result=PIPE_ERROR errno=%d", key, errno);
    child = fork();
    if (child < 0) {
        error = errno;
        close(channels[0]); close(channels[1]);
        return status_emit(stream, "PROP key=%s result=FORK_ERROR errno=%d", key, error);
    }
    if (!child) {
        close(channels[0]);
        if (dup2(channels[1], STDOUT_FILENO) < 0 || dup2(channels[1], STDERR_FILENO) < 0)
            _exit(126);
        close(channels[1]);
        execl(GETPROP_PATH, "getprop", key, (char *)NULL);
        _exit(127);
    }
    close(channels[1]);
    (void)fcntl(channels[0], F_SETFL, O_NONBLOCK);
    while (monotonic_ms() < deadline && used < sizeof(value) - 1) {
        ssize_t n = read(channels[0], value + used, sizeof(value) - 1 - used);
        if (n > 0) { used += (size_t)n; continue; }
        if (!n) { finished = 1; break; }
        if (errno != EAGAIN && errno != EINTR) { error = errno; break; }
        {
            struct pollfd p = { .fd = channels[0], .events = POLLIN | POLLHUP };
            (void)poll(&p, 1, 20);
        }
    }
    close(channels[0]);
    limited = used == sizeof(value) - 1;
    if (!finished) (void)kill(child, SIGKILL);
    reap_deadline = monotonic_ms() + 100;
    /* Never block ACM or even the snapshot child indefinitely on waitpid. */
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) { reaped = 1; break; }
        if (monotonic_ms() >= (finished ? deadline : reap_deadline)) {
            (void)kill(child, SIGKILL);
            if (finished) {
                finished = 0;
                reap_deadline = monotonic_ms() + 100;
                continue;
            }
            break;
        }
        (void)poll(NULL, 0, 10);
    }
    value[used] = 0;
    while (used && (value[used - 1] == '\n' || value[used - 1] == '\r')) value[--used] = 0;
    return status_emit(stream, "PROP key=%s result=%s wait_status=%d errno=%d value=%s", key,
                       error ? "READ_ERROR" : limited ? "LIMIT" :
                       (finished && reaped ? (status ? "EXIT_ERROR" : "OK") : "TIMEOUT"), status, error, value);
}

static int status_supply_attribute(int stream, const char *name, const char *attribute)
{
    char path[384], value[257];
    struct stat st;
    ssize_t n;
    int fd, error, checked;
    snprintf(path, sizeof(path), POWER_SUPPLY_PATH "/%s/%s", name, attribute);
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return status_emit(stream, "POWER name=%s attribute=%s valid=0 errno=%d", name, attribute, errno);
    checked = fstat(fd, &st);
    if (checked || !S_ISREG(st.st_mode)) {
        error = checked ? errno : EINVAL;
        close(fd);
        return status_emit(stream, "POWER name=%s attribute=%s valid=0 errno=%d", name, attribute, error);
    }
    n = read(fd, value, sizeof(value) - 1);
    error = n < 0 ? errno : 0;
    close(fd);
    value[n >= 0 ? n : 0] = 0;
    return status_emit(stream, "POWER name=%s attribute=%s valid=%d errno=%d limit_reached=%d value=%s",
                       name, attribute, n >= 0, error, n == sizeof(value) - 1, value);
}

static int status_dump(void)
{
    static const char *const keys[] = { "sys.boot_completed", "init.svc.surfaceflinger",
        "init.svc.zygote", "init.svc.bootanim", "init.svc.netd", "init.svc.health-hal-z1" };
    static const char *const attributes[] = { "temp", "capacity", "present", "status" };
    int stream, fd;
    DIR *directory;
    struct dirent *entry;
    unsigned supplies = 0;
    size_t i;
    alarm(15);
    stream = stream_open();
    if (stream < 0) return 1;
    if (status_emit(stream, "BEGIN schema=1 property_cap=256 mounts_cap=16384 supplies_cap=8 timeout_ms=%u",
                    STATUS_TIMEOUT_MS)) { close(stream); return 1; }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (status_property(stream, keys[i])) { close(stream); return 1; }
    fd = open(PROC_MOUNTS_PATH, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) status_emit(stream, "MOUNTS valid=0 errno=%d", errno);
    else {
        uint64_t bytes = 0;
        int result;
        status_emit(stream, "MOUNTS BEGIN cap=16384");
        result = stream_text(stream, "z1status", fd, 16384, &bytes);
        close(fd);
        if (result) { close(stream); return 1; }
        status_emit(stream, "MOUNTS END bytes=%llu limit_reached=%d",
                    (unsigned long long)bytes, bytes == 16384);
    }
    directory = opendir(POWER_SUPPLY_PATH);
    if (!directory) status_emit(stream, "POWER_DIR valid=0 errno=%d", errno);
    else {
        status_emit(stream, "POWER_DIR valid=1");
        while (supplies < 8 && (entry = readdir(directory))) {
            size_t length = strlen(entry->d_name);
            if (!length || length > 63 || strspn(entry->d_name,
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != length) continue;
            ++supplies;
            for (i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i)
                if (status_supply_attribute(stream, entry->d_name, attributes[i])) {
                    closedir(directory); close(stream); return 1;
                }
        }
        closedir(directory);
    }
    status_emit(stream, "DONE supplies=%u", supplies);
    close(stream);
    return 0;
}

static void acmlog(const char *what, int error)
{
    char line[256];
    int n = snprintf(line, sizeof(line), "[z1acm] %s errno=%d\n", what, error);
    if (n > 0) {
        ssize_t ignored = write(logfd >= 0 ? logfd : STDERR_FILENO, line, (size_t)n);
        (void)ignored;
    }
}

static int folder(const char *path)
{
    struct stat st;
    if (!mkdir(path, 0755)) return 0;
    if (errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode)) return 0;
    return -1;
}

static int attribute(const char *name, const char *value)
{
    char path[512];
    ssize_t n;
    int fd, saved;
    snprintf(path, sizeof(path), DIAG_GADGET "/%s", name);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    n = write(fd, value, strlen(value));
    saved = errno;
    if (close(fd) < 0 && n == (ssize_t)strlen(value)) return -1;
    if (n != (ssize_t)strlen(value)) { errno = n < 0 ? saved : EIO; return -1; }
    return 0;
}

/* A different configured gadget must never be unbound or overwritten. */
static int bound_gadget(char *ours, size_t size)
{
    struct dirent *entry;
    DIR *dir = opendir(GADGET_ROOT);
    int result = 0;
    if (!dir) return -1;
    ours[0] = 0;
    while ((entry = readdir(dir))) {
        char path[512], value[256];
        ssize_t n;
        int fd;
        if (entry->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), GADGET_ROOT "/%s/UDC", entry->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        n = read(fd, value, sizeof(value) - 1);
        close(fd);
        if (n <= 0) continue;
        value[n] = 0;
        value[strcspn(value, "\r\n")] = 0;
        if (!value[0]) continue;
        if (!strcmp(entry->d_name, "z1diag")) snprintf(ours, size, "%s", value);
        else result = 1;
    }
    closedir(dir);
    return result;
}

static int setup_acm(void)
{
    static const char *const dirs[] = {
        DIAG_GADGET, DIAG_GADGET "/strings/0x409", DIAG_GADGET "/configs/c.1",
        DIAG_GADGET "/configs/c.1/strings/0x409", DIAG_GADGET "/functions/acm.usb0"
    };
    char udc[256] = "", ours[256], target[512];
    DIR *dir;
    struct dirent *entry;
    size_t i;
    struct statfs fs;
    int busy;
    if (statfs(GADGET_ROOT, &fs) < 0) return -1;
    if ((unsigned long)fs.f_type != 0x62656570UL) { errno = ENODEV; return -1; }
    busy = bound_gadget(ours, sizeof(ours));
    if (busy < 0) return -1;
    if (busy) { errno = EBUSY; return -1; }
    if (ours[0]) return 0;
    dir = opendir("/sys/class/udc");
    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        if (strstr(entry->d_name, "musb") && !strchr(entry->d_name, '/')) {
            snprintf(udc, sizeof(udc), "%s", entry->d_name);
            break;
        }
    }
    closedir(dir);
    if (!udc[0]) { errno = ENODEV; return -1; }
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        if (folder(dirs[i]) < 0) return -1;
    if (attribute("idVendor", "0x0e8d") || attribute("idProduct", "0x2006") ||
        attribute("bcdUSB", "0x0200") || attribute("bcdDevice", "0x0100") ||
        attribute("strings/0x409/serialnumber", "Z1DIAG20261001") ||
        attribute("strings/0x409/manufacturer", "Z1") ||
        attribute("strings/0x409/product", "Z1 Android9 diagnostic ACM") ||
        attribute("configs/c.1/strings/0x409/configuration", "Kernel log") ||
        attribute("configs/c.1/MaxPower", "100")) return -1;
    if (symlink(DIAG_GADGET "/functions/acm.usb0", DIAG_GADGET "/configs/c.1/acm.usb0") < 0) {
        ssize_t n;
        if (errno != EEXIST) return -1;
        n = readlink(DIAG_GADGET "/configs/c.1/acm.usb0", target, sizeof(target) - 1);
        if (n < 0) return -1;
        target[n] = 0;
        if (strcmp(target, DIAG_GADGET "/functions/acm.usb0")) { errno = EEXIST; return -1; }
    }
    /* Recheck ownership just before binding; kernel also refuses a busy UDC. */
    busy = bound_gadget(ours, sizeof(ours));
    if (busy || ours[0]) { errno = EBUSY; return -1; }
    if (attribute("UDC", udc) < 0) return -1;
    acmlog("ACM bound; connect device USB after boot", 0);
    return 0;
}

static int acm_main(void)
{
    static unsigned char history[HISTORY_SIZE];
    char record[8192];
    uint64_t end = 0, sent = 0;
    int kfd = -1, tty = -1, configured = 0, previous_error = -1, streaming = 0;
    int stream = -1;
    pid_t tombstone_child = -1;
    pid_t status_child = -1;
    uint64_t status_deadline = 0;
    char warning[128];
    size_t warning_size = 0, warning_sent = 0;
    uint64_t warning_target = 0;
    unsigned retry = 0;
    acmlog("starting independent ramdisk ACM log relay; send R to replay", 0);
    for (;;) {
        unsigned reads;
        struct pollfd waitfd;
        if (status_child > 0) {
            int status;
            if (waitpid(status_child, &status, WNOHANG) == status_child) {
                if (status) {
                    (void)kill(-status_child, SIGKILL);
                    if (stream >= 0)
                        status_emit(stream, "END result=FAILED_OR_TIMEOUT wait_status=%d retry=S", status);
                }
                status_child = -1;
            } else if (monotonic_ms() >= status_deadline) {
                /* Includes getprop descendants if snapshot blocked on FIFO/sysfs. */
                (void)kill(-status_child, SIGKILL);
                (void)kill(status_child, SIGKILL);
            }
        }
        if (tombstone_child > 0) {
            int status;
            if (waitpid(tombstone_child, &status, WNOHANG) == tombstone_child) {
                if (status) acmlog("tombstone export failed/timed out; request T to retry", status);
                tombstone_child = -1;
            }
        }
        if (stream < 0) {
            struct stat st;
            if ((!mkfifo(DIAG_FIFO, 0600) || errno == EEXIST) &&
                !lstat(DIAG_FIFO, &st) && S_ISFIFO(st.st_mode))
                stream = open(DIAG_FIFO, O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        }
        if (kfd < 0) {
            kfd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (kfd >= 0 && lseek(kfd, 0, SEEK_SET) < 0) acmlog("kmsg replay seek", errno);
        }
        if (kfd >= 0) for (reads = 0; reads < 256; ++reads) {
            ssize_t n = read(kfd, record, sizeof(record));
            size_t i;
            if (n < 0 && errno == EPIPE) {
                acmlog("kernel ring overrun; earlier records lost", EPIPE);
                continue;
            }
            if (n <= 0) break;
            for (i = 0; i < (size_t)n; ++i) history[(end++) % HISTORY_SIZE] = (unsigned char)record[i];
        }
        if (stream >= 0) for (reads = 0; reads < 256; ++reads) {
            ssize_t n = read(stream, record, sizeof(record));
            size_t i;
            if (n <= 0) break;
            for (i = 0; i < (size_t)n; ++i) history[(end++) % HISTORY_SIZE] = (unsigned char)record[i];
        }
        if (!configured && !retry) {
            if (!setup_acm()) { configured = 1; previous_error = -1; }
            else {
                int error = errno;
                if (error != previous_error) acmlog("waiting for configfs/free MUSB UDC", error);
                previous_error = error;
                retry = 25; /* 5 seconds, no retry spin. */
            }
        }
        if (retry) --retry;
        if (configured && tty < 0 && !retry) {
            tty = open("/dev/ttyGS0", O_RDWR | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
            if (tty >= 0) {
                struct termios term;
                if (!tcgetattr(tty, &term)) {
                    cfmakeraw(&term);
                    (void)tcsetattr(tty, TCSANOW, &term);
                }
                streaming = 0;
                warning_size = warning_sent = 0;
                sent = end > HISTORY_SIZE ? end - HISTORY_SIZE : 0;
            }
            else retry = 5;
        }
        if (tty >= 0) {
            unsigned attempts;
            for (attempts = 0; attempts < 4; ++attempts) {
                unsigned char command[64];
                ssize_t n = read(tty, command, sizeof(command));
                ssize_t i;
                if (n > 0) {
                    for (i = 0; i < n; ++i) if (command[i] == 'R') {
                        sent = end > HISTORY_SIZE ? end - HISTORY_SIZE : 0;
                        streaming = 1;
                        warning_size = warning_sent = 0;
                    } else if (command[i] == 'T' && streaming && tombstone_child < 0) {
                        tombstone_child = fork();
                        if (tombstone_child == 0) {
                            close(tty);
                            if (stream >= 0) close(stream);
                            if (kfd >= 0) close(kfd);
                            _exit(tombstone_dump());
                        }
                    } else if (command[i] == 'S' && streaming && status_child < 0) {
                        status_child = fork();
                        if (status_child == 0) {
                            (void)setpgid(0, 0);
                            close(tty);
                            if (stream >= 0) close(stream);
                            if (kfd >= 0) close(kfd);
                            _exit(status_dump());
                        }
                        if (status_child > 0) {
                            (void)setpgid(status_child, status_child);
                            status_deadline = monotonic_ms() + STATUS_TIMEOUT_MS;
                        } else if (stream >= 0) {
                            status_emit(stream, "END result=FORK_ERROR errno=%d retry=S", errno);
                        }
                    }
                } else {
                    if (n < 0 && errno != EAGAIN && errno != EINTR) {
                        close(tty);
                        tty = -1;
                        streaming = 0;
                        retry = 5;
                    }
                    break;
                }
            }
        }
        if (sent < end && end - sent > HISTORY_SIZE) {
            if (tty >= 0 && streaming) {
                ssize_t n;
                if (!warning_size) {
                    warning_target = end - HISTORY_SIZE;
                    warning_size = (size_t)snprintf(warning, sizeof(warning),
                        "[z1acm] history overrun skipped_bytes=%llu; replay window=%u\n",
                        (unsigned long long)(warning_target - sent), HISTORY_SIZE);
                    warning_sent = 0;
                }
                n = write(tty, warning + warning_sent, warning_size - warning_sent);
                if (n > 0) warning_sent += (size_t)n;
                else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    close(tty);
                    tty = -1;
                    streaming = 0;
                    retry = 5;
                    warning_size = warning_sent = 0;
                }
                if (!streaming || warning_sent != warning_size) {
                    /* Do not silently discard the warning on a blocked USB. */
                    (void)poll(NULL, 0, 200);
                    continue;
                }
                sent = warning_target;
                warning_size = warning_sent = 0;
                if (end - sent > HISTORY_SIZE) continue;
            } else {
                sent = end - HISTORY_SIZE;
            }
        }
        if (tty >= 0 && streaming && sent < end) {
            size_t start = (size_t)(sent % HISTORY_SIZE), count = (size_t)(end - sent);
            ssize_t n;
            if (count > HISTORY_SIZE - start) count = HISTORY_SIZE - start;
            n = write(tty, history + start, count);
            if (n > 0) sent += (uint64_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                close(tty);
                tty = -1;
                streaming = 0;
                retry = 5;
            }
        }
        /* Always yield even under sustained printk; no /proc/kmsg consumption. */
        waitfd.fd = -1;
        waitfd.events = 0;
        (void)poll(&waitfd, 0, 200);
    }
    return 0;
}


/* Only userspace log buffers: kernel is excluded to prevent relay feedback. */
static int logcat_main(void)
{
    for (;;) {
        int channels[2], status, stream;
        stream = stream_open();
        if (stream < 0) { sleep(1); continue; }
        pid_t child;
        if (pipe2(channels, O_CLOEXEC) < 0) {
            acmlog("logcat pipe failed", errno);
            close(stream);
            sleep(5);
            continue;
        }
        child = fork();
        if (child < 0) {
            acmlog("logcat fork failed", errno);
            close(channels[0]);
            close(channels[1]);
            close(stream);
            sleep(5);
            continue;
        }
        if (!child) {
            close(channels[0]);
            if (dup2(channels[1], STDOUT_FILENO) < 0 ||
                dup2(channels[1], STDERR_FILENO) < 0) _exit(126);
            close(channels[1]);
            execl("/system/bin/logcat", "logcat", "-b", "main", "-b", "system",
                  "-b", "crash", "-v", "threadtime", (char *)NULL);
            _exit(127);
        }
        close(channels[1]);
        {
            int result = stream_text(stream, "z1logcat", channels[0], 0, NULL);
            close(channels[0]);
            close(stream);
            if (result) (void)kill(child, SIGTERM);
        }
        while (waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) { status = -1; break; }
        }
        acmlog("logcat exited; retry in 5s", status);
        sleep(5);
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const char *const paths[] = {
        "/system/bin/mkswap", "/system/bin/toybox", "/system/bin/linker",
        "/dev/block/mmcblk0p4", "/dev/mmcblk0p4"
    };
    size_t i;
    logfd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (argc == 2 && !strcmp(argv[1], "--acm")) return acm_main();
    if (argc == 2 && !strcmp(argv[1], "--logcat")) return logcat_main();
    if (argc != 1) return 2;
    logmsg("BEGIN read-only boot evidence pid=%ld", (long)getpid());
    dumptext("/proc/cmdline");
    dumptext("/proc/mounts");
    dumptext("/sys/class/block/mmcblk0p4/start");
    dumptext("/sys/class/block/mmcblk0p4/size");
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) pathstat(paths[i]);
    ext4magic("/dev/block/mmcblk0p4");
    ext4magic("/dev/mmcblk0p4");
    logmsg("END read-only boot evidence");
    if (logfd >= 0) close(logfd);
    return 0;
}
