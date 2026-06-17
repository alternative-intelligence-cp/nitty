/*
 * nitty_serial_shim.c — Serial port C shim implementation
 *
 * v0.1.0: Serial port open/close, configuration, I/O, and enumeration.
 *
 * This shim wraps POSIX termios/serial functions for Nitpick FFI access.
 * All pointer parameters are passed as int64_t and cast internally.
 *
 * Thread safety: Not thread-safe (static buffers).
 * For Nitty's single-threaded terminal model this is fine.
 */

/* _GNU_SOURCE is defined via -D_GNU_SOURCE in compiler flags (build.abc) */

#include "nitty_serial_shim.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Saved termios table
 *
 * We keep a table of up to 64 {fd, original_termios} pairs so we can
 * restore settings on close without requiring the caller to manage them.
 * ═══════════════════════════════════════════════════════════════════════ */

#define SERIAL_TERMIOS_TABLE_SIZE 64

typedef struct {
    int         fd;         /* -1 = slot is free */
    struct termios tio;
} SavedTermios;

static SavedTermios g_saved_termios[SERIAL_TERMIOS_TABLE_SIZE];
static int g_termios_init = 0;

static void termios_table_init(void)
{
    if (!g_termios_init) {
        for (int i = 0; i < SERIAL_TERMIOS_TABLE_SIZE; i++) {
            g_saved_termios[i].fd = -1;
        }
        g_termios_init = 1;
    }
}

/* Save original termios for fd. Returns 0 on success, -1 if table is full. */
static int termios_save(int fd, const struct termios *tio)
{
    termios_table_init();
    for (int i = 0; i < SERIAL_TERMIOS_TABLE_SIZE; i++) {
        if (g_saved_termios[i].fd == -1) {
            g_saved_termios[i].fd  = fd;
            g_saved_termios[i].tio = *tio;
            return 0;
        }
    }
    fprintf(stderr, "SERIAL: termios save table full\n");
    return -1;
}

/* Restore saved termios for fd and free slot. Returns 0 or -1. */
static int termios_restore(int fd)
{
    termios_table_init();
    for (int i = 0; i < SERIAL_TERMIOS_TABLE_SIZE; i++) {
        if (g_saved_termios[i].fd == fd) {
            tcsetattr(fd, TCSANOW, &g_saved_termios[i].tio);
            g_saved_termios[i].fd = -1;
            return 0;
        }
    }
    return -1;  /* not found — fd was not opened by us */
}

/* ═══════════════════════════════════════════════════════════════════════
 * Baud rate mapping
 * ═══════════════════════════════════════════════════════════════════════ */

static speed_t baud_to_termios(int64_t baud)
{
    switch (baud) {
        case 300:    return B300;
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:
            fprintf(stderr, "SERIAL: unknown baud %lld, defaulting to 115200\n",
                    (long long)baud);
            return B115200;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Serial port lifecycle
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_serial_open(const char *path, int64_t baud, int64_t data_bits,
                           int64_t parity, int64_t stop_bits,
                           int64_t flow_control)
{
    if (!path || path[0] == '\0') {
        fprintf(stderr, "SERIAL: open: null or empty path\n");
        return -1;
    }

    /* Open the device in read/write, non-controlling, non-blocking mode */
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "SERIAL: open(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }

    /* Save original termios so we can restore on close */
    struct termios orig_tio;
    if (tcgetattr(fd, &orig_tio) != 0) {
        fprintf(stderr, "SERIAL: tcgetattr(%d) failed: %s\n", fd, strerror(errno));
        close(fd);
        return -1;
    }
    termios_save(fd, &orig_tio);

    /* Start from a clean raw-mode baseline */
    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    cfmakeraw(&tio);

    /* ── Data bits ── */
    tio.c_cflag &= ~CSIZE;
    switch (data_bits) {
        case 5:  tio.c_cflag |= CS5; break;
        case 6:  tio.c_cflag |= CS6; break;
        case 7:  tio.c_cflag |= CS7; break;
        case 8:
        default: tio.c_cflag |= CS8; break;
    }

    /* ── Parity ── */
    tio.c_cflag &= ~(PARENB | PARODD);
    switch (parity) {
        case 1:  /* odd  */ tio.c_cflag |= (PARENB | PARODD); break;
        case 2:  /* even */ tio.c_cflag |=  PARENB;            break;
        case 0:
        default: /* none */ break;
    }

    /* ── Stop bits ── */
    tio.c_cflag &= ~CSTOPB;
    if (stop_bits == 2) {
        tio.c_cflag |= CSTOPB;
    }

    /* ── Flow control ── */
    tio.c_cflag &= ~CRTSCTS;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    switch (flow_control) {
        case 1:  /* RTS/CTS hardware */  tio.c_cflag |= CRTSCTS;         break;
        case 2:  /* XON/XOFF software */ tio.c_iflag |= (IXON | IXOFF);  break;
        case 0:
        default: /* none */ break;
    }

    /* ── Enable receiver, local mode ── */
    tio.c_cflag |= (CLOCAL | CREAD);

    /* ── Baud rate ── */
    speed_t speed = baud_to_termios(baud);
    if (cfsetspeed(&tio, speed) != 0) {
        fprintf(stderr, "SERIAL: cfsetspeed(%d, %lld) failed: %s\n",
                fd, (long long)baud, strerror(errno));
        termios_restore(fd);
        close(fd);
        return -1;
    }

    /* ── Non-blocking read: return immediately if no data ── */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    /* Apply settings immediately */
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "SERIAL: tcsetattr(%d) failed: %s\n", fd, strerror(errno));
        termios_restore(fd);
        close(fd);
        return -1;
    }

    fprintf(stderr, "SERIAL: opened %s at %lld baud (fd=%d)\n",
            path, (long long)baud, fd);

    return (int64_t)fd;
}

int64_t nitty_serial_close(int64_t fd)
{
    if (fd < 0) return -1;

    termios_restore((int)fd);

    int result = close((int)fd);
    if (result < 0) {
        fprintf(stderr, "SERIAL: close(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }

    fprintf(stderr, "SERIAL: closed fd=%d\n", (int)fd);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Serial port configuration
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_serial_set_baud(int64_t fd, int64_t baud)
{
    struct termios tio;
    if (tcgetattr((int)fd, &tio) != 0) {
        fprintf(stderr, "SERIAL: set_baud: tcgetattr(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }

    speed_t speed = baud_to_termios(baud);
    if (cfsetspeed(&tio, speed) != 0) {
        fprintf(stderr, "SERIAL: set_baud: cfsetspeed(%d, %lld) failed: %s\n",
                (int)fd, (long long)baud, strerror(errno));
        return -1;
    }

    if (tcsetattr((int)fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "SERIAL: set_baud: tcsetattr(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }

    return 0;
}

int64_t nitty_serial_is_tty(int64_t fd)
{
    return (int64_t)isatty((int)fd);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Serial port I/O
 * ═══════════════════════════════════════════════════════════════════════ */

/* Internal 16KB read buffer + 1 byte for null terminator */
#define SERIAL_READ_BUF_SIZE 16384
static char    g_read_buf[SERIAL_READ_BUF_SIZE + 1];
static int64_t g_read_buf_len = 0;

int64_t nitty_serial_read_buffered(int64_t fd)
{
    g_read_buf_len = 0;

    ssize_t n;
    do {
        n = read((int)fd, g_read_buf, SERIAL_READ_BUF_SIZE);
    } while (n < 0 && errno == EINTR);  /* retry on signal interrupt */

    if (n > 0) {
        g_read_buf_len = (int64_t)n;
        g_read_buf[n] = '\0';  /* null-terminate for string access */
        return (int64_t)n;
    }
    if (n == 0) {
        return 0;   /* EOF / device disconnected */
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return -2;  /* no data available right now */
    }
    fprintf(stderr, "SERIAL: read(%d) failed: %s\n", (int)fd, strerror(errno));
    return -1;
}

int64_t nitty_serial_read_buf_len(void)
{
    return g_read_buf_len;
}

const char *nitty_serial_read_buf_str(void)
{
    g_read_buf[g_read_buf_len] = '\0';
    return g_read_buf;
}

int64_t nitty_serial_write_string(int64_t fd, const char *data)
{
    if (!data) return -1;
    size_t len = strlen(data);
    if (len == 0) return 0;

    size_t total = 0;
    while (total < len) {
        ssize_t n = write((int)fd, data + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;  /* retry on signal */
            fprintf(stderr, "SERIAL: write_string(%d) failed: %s\n",
                    (int)fd, strerror(errno));
            return (total > 0) ? (int64_t)total : -1;
        }
        total += (size_t)n;
    }
    return (int64_t)total;
}

int64_t nitty_serial_write_byte(int64_t fd, int64_t byte_val)
{
    char c = (char)(byte_val & 0xFF);
    ssize_t n;
    do {
        n = write((int)fd, &c, 1);
    } while (n < 0 && errno == EINTR);

    if (n == 1) return 1;
    fprintf(stderr, "SERIAL: write_byte(%d, 0x%02X) failed: %s\n",
            (int)fd, (int)(byte_val & 0xFF), strerror(errno));
    return -1;
}

int64_t nitty_serial_flush(int64_t fd)
{
    int result = tcflush((int)fd, TCIOFLUSH);
    if (result != 0) {
        fprintf(stderr, "SERIAL: tcflush(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_serial_drain(int64_t fd)
{
    int result = tcdrain((int)fd);
    if (result != 0) {
        fprintf(stderr, "SERIAL: tcdrain(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Port enumeration
 * ═══════════════════════════════════════════════════════════════════════ */

#define SERIAL_MAX_PORTS 64

static char g_port_names[SERIAL_MAX_PORTS][320];  /* "/dev/" + NAME_MAX + NUL */
static char g_port_descs[SERIAL_MAX_PORTS][512];
static int  g_port_count = 0;

/* Port type ordering: lower value = earlier in sorted order */
typedef enum {
    PORT_TYPE_USB = 0,
    PORT_TYPE_ACM = 1,
    PORT_TYPE_S   = 2,
    PORT_TYPE_UNK = 3,
} PortType;

typedef struct {
    char     name[320];  /* "/dev/" (5) + NAME_MAX (255) + NUL = 261 max */
    char     desc[512];
    PortType type;
    int      index;  /* numeric suffix for stable sort within a type */
} PortEntry;

static PortType classify_port(const char *name)
{
    if (strncmp(name, "ttyUSB", 6) == 0) return PORT_TYPE_USB;
    if (strncmp(name, "ttyACM", 6) == 0) return PORT_TYPE_ACM;
    if (strncmp(name, "ttyS",   4) == 0) return PORT_TYPE_S;
    return PORT_TYPE_UNK;
}

static int port_entry_cmp(const void *a, const void *b)
{
    const PortEntry *pa = (const PortEntry *)a;
    const PortEntry *pb = (const PortEntry *)b;

    if (pa->type != pb->type) {
        return (int)pa->type - (int)pb->type;
    }
    return pa->index - pb->index;
}

/*
 * Try to read a single-line sysfs attribute file.
 * Trims trailing whitespace/newlines.
 * Returns 1 on success, 0 if the file could not be read.
 */
static int read_sysfs_attr(const char *path, char *out, size_t out_size)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    if (!fgets(out, (int)out_size, f)) {
        fclose(f);
        out[0] = '\0';
        return 0;
    }
    fclose(f);

    /* Trim trailing whitespace */
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' ||
                       out[len - 1] == ' '  || out[len - 1] == '\t')) {
        out[--len] = '\0';
    }

    return (len > 0) ? 1 : 0;
}

/*
 * Build a description for a USB/ACM device by reading sysfs.
 * Tries manufacturer + product strings; falls back to generic label.
 * The sysfs path for a USB serial is:
 *   /sys/class/tty/<name>/device/../../manufacturer   (USB device level)
 *   /sys/class/tty/<name>/device/../../product
 *
 * For ttyACM the layout may be one level deeper on some kernels, but
 * device/../.. usually reaches the USB device node either way.
 */
static void build_usb_desc(const char *devname, const char *label,
                            char *out, size_t out_size)
{
    char mfr[256]  = {0};
    char prod[256] = {0};
    char path[512];

    /* manufacturer */
    snprintf(path, sizeof(path),
             "/sys/class/tty/%s/device/../../manufacturer", devname);
    int got_mfr = read_sysfs_attr(path, mfr, sizeof(mfr));

    /* product */
    snprintf(path, sizeof(path),
             "/sys/class/tty/%s/device/../../product", devname);
    int got_prod = read_sysfs_attr(path, prod, sizeof(prod));

    if (got_mfr && got_prod) {
        snprintf(out, out_size, "%s %s", mfr, prod);
    } else if (got_prod) {
        snprintf(out, out_size, "%s", prod);
    } else if (got_mfr) {
        snprintf(out, out_size, "%s", mfr);
    } else {
        snprintf(out, out_size, "%s", label);
    }
}

int64_t nitty_serial_enumerate(void)
{
    g_port_count = 0;

    static PortEntry entries[SERIAL_MAX_PORTS];
    int entry_count = 0;

    DIR *dir = opendir("/dev");
    if (!dir) {
        fprintf(stderr, "SERIAL: enumerate: opendir(/dev) failed: %s\n",
                strerror(errno));
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && entry_count < SERIAL_MAX_PORTS) {
        const char *name = ent->d_name;
        PortType type = classify_port(name);

        if (type == PORT_TYPE_UNK) continue;

        /* For ttyS, only include ttyS0–ttyS7 to avoid phantom ports */
        if (type == PORT_TYPE_S) {
            /* name[4] is the first digit after "ttyS" */
            if (name[4] < '0' || name[4] > '7') continue;
            if (name[5] != '\0') continue;  /* reject ttyS10, ttyS99, etc. */
        }

        /* Build full device path and verify it exists */
        char devpath[320];  /* "/dev/" (5) + NAME_MAX (255) + NUL = 261 max */
        snprintf(devpath, sizeof(devpath), "/dev/%s", name);

        struct stat st;
        if (stat(devpath, &st) != 0) continue;
        if (!S_ISCHR(st.st_mode)) continue;

        /* Populate entry */
        PortEntry *e = &entries[entry_count];
        snprintf(e->name, sizeof(e->name), "%s", devpath);
        e->type  = type;
        e->index = atoi(name + (type == PORT_TYPE_USB ? 6 :
                                type == PORT_TYPE_ACM ? 6 : 4));

        switch (type) {
            case PORT_TYPE_USB:
                build_usb_desc(name, "USB Serial Device", e->desc, sizeof(e->desc));
                break;
            case PORT_TYPE_ACM:
                build_usb_desc(name, "USB CDC ACM Device", e->desc, sizeof(e->desc));
                break;
            case PORT_TYPE_S:
                snprintf(e->desc, sizeof(e->desc), "Serial port");
                break;
            default:
                snprintf(e->desc, sizeof(e->desc), "Unknown serial device");
                break;
        }

        entry_count++;
    }

    closedir(dir);

    /* Sort: ttyUSB first, ttyACM second, ttyS last; within each type by index */
    qsort(entries, (size_t)entry_count, sizeof(PortEntry), port_entry_cmp);

    /* Copy sorted results into the global static arrays */
    g_port_count = entry_count;
    for (int i = 0; i < entry_count; i++) {
        memcpy(g_port_names[i], entries[i].name, sizeof(g_port_names[i]));
        g_port_names[i][sizeof(g_port_names[i]) - 1] = '\0';
        memcpy(g_port_descs[i], entries[i].desc, sizeof(g_port_descs[i]));
        g_port_descs[i][sizeof(g_port_descs[i]) - 1] = '\0';
    }

    fprintf(stderr, "SERIAL: enumerate found %d port(s)\n", g_port_count);
    return (int64_t)g_port_count;
}

int64_t nitty_serial_port_count(void)
{
    return (int64_t)g_port_count;
}

const char *nitty_serial_port_name(int64_t i)
{
    if (i < 0 || i >= (int64_t)g_port_count) return "";
    return g_port_names[(int)i];
}

const char *nitty_serial_port_desc(int64_t i)
{
    if (i < 0 || i >= (int64_t)g_port_count) return "";
    return g_port_descs[(int)i];
}

/* ═══════════════════════════════════════════════════════════════════════
 * Serial control signals and modem status (v0.9.1)
 * ═══════════════════════════════════════════════════════════════════════ */

#include <sys/ioctl.h>
#include <linux/serial.h>

int64_t nitty_serial_send_break(int64_t fd, int64_t duration_ms)
{
    if (fd < 0) return -1;
    /* tcsendbreak duration=0 is implementation-defined (~250ms per POSIX).
     * For non-zero duration we use tcsendbreak with duration in units of 100ms.
     * Since POSIX doesn't guarantee the unit, we use 0 for the default. */
    int dur = (duration_ms > 0) ? (int)(duration_ms / 100) : 0;
    if (tcsendbreak((int)fd, dur) != 0) {
        fprintf(stderr, "SERIAL: tcsendbreak(%d, %d) failed: %s\n",
                (int)fd, dur, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_serial_set_dtr(int64_t fd, int64_t state)
{
    if (fd < 0) return -1;
    int flag = TIOCM_DTR;
    int req  = state ? TIOCMBIS : TIOCMBIC;
    if (ioctl((int)fd, req, &flag) != 0) {
        fprintf(stderr, "SERIAL: set_dtr(%d, %lld) failed: %s\n",
                (int)fd, (long long)state, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_serial_set_rts(int64_t fd, int64_t state)
{
    if (fd < 0) return -1;
    int flag = TIOCM_RTS;
    int req  = state ? TIOCMBIS : TIOCMBIC;
    if (ioctl((int)fd, req, &flag) != 0) {
        fprintf(stderr, "SERIAL: set_rts(%d, %lld) failed: %s\n",
                (int)fd, (long long)state, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_serial_get_modem_status(int64_t fd)
{
    if (fd < 0) return -1;
    int status = 0;
    if (ioctl((int)fd, TIOCMGET, &status) != 0) {
        fprintf(stderr, "SERIAL: get_modem_status(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }
    return (int64_t)status;
}

int64_t nitty_serial_modem_cts(int64_t status) { return (status & TIOCM_CTS)  ? 1 : 0; }
int64_t nitty_serial_modem_dsr(int64_t status) { return (status & TIOCM_DSR)  ? 1 : 0; }
int64_t nitty_serial_modem_dcd(int64_t status) { return (status & TIOCM_CD)   ? 1 : 0; }
int64_t nitty_serial_modem_ri(int64_t status)  { return (status & TIOCM_RI)   ? 1 : 0; }

int64_t nitty_serial_write_byte_delayed(int64_t fd, int64_t byte_val, int64_t delay_us)
{
    char c = (char)(byte_val & 0xFF);
    ssize_t n;
    do {
        n = write((int)fd, &c, 1);
    } while (n < 0 && errno == EINTR);

    if (n != 1) {
        fprintf(stderr, "SERIAL: write_byte_delayed(%d, 0x%02X) failed: %s\n",
                (int)fd, (int)(byte_val & 0xFF), strerror(errno));
        return -1;
    }

    if (delay_us > 0) {
        usleep((useconds_t)delay_us);
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Byte-level string helpers (v0.9.1)
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_serial_byte_at(const char *s, int64_t i)
{
    if (!s || i < 0) return -1;
    size_t len = strlen(s);
    if ((size_t)i >= len) return -1;
    return (int64_t)((unsigned char)s[i]);
}

/* Internal static hexdump buffer (64 KB). */
static char s_hexdump_buf[65536];
static int64_t s_hexdump_len = 0;

const char *nitty_serial_hexdump(const char *data, int64_t len,
                                  int64_t byte_offset, int64_t max_bytes)
{
    s_hexdump_buf[0] = '\0';
    s_hexdump_len = 0;

    if (!data || len <= 0) return s_hexdump_buf;

    int64_t limit = (max_bytes > 0 && max_bytes < len) ? max_bytes : len;
    char *out = s_hexdump_buf;
    char *end = s_hexdump_buf + sizeof(s_hexdump_buf) - 2;

    for (int64_t pos = 0; pos < limit && out < end - 80; pos += 16) {
        int64_t chunk = limit - pos;
        if (chunk > 16) chunk = 16;

        /* Offset field: 8 hex digits + 2 spaces */
        int written = snprintf(out, (size_t)(end - out), "%08llX  ",
                               (unsigned long long)(byte_offset + pos));
        out += written;

        /* Hex bytes */
        for (int64_t i = 0; i < 16 && out < end - 4; i++) {
            if (i < chunk) {
                written = snprintf(out, (size_t)(end - out), "%02X ",
                                   (unsigned char)data[pos + i]);
                out += written;
            } else {
                memcpy(out, "   ", 3);
                out += 3;
            }
            if (i == 7) { *out++ = ' '; }  /* extra space between groups */
        }

        /* ASCII sidebar */
        *out++ = ' ';
        *out++ = '|';
        for (int64_t i = 0; i < chunk && out < end - 1; i++) {
            unsigned char b = (unsigned char)data[pos + i];
            *out++ = (b >= 0x20 && b <= 0x7e) ? (char)b : '.';
        }
        *out++ = '|';
        *out++ = '\n';
    }
    *out = '\0';
    s_hexdump_len = (int64_t)(out - s_hexdump_buf);
    return s_hexdump_buf;
}

int64_t nitty_serial_hexdump_len(void)
{
    return s_hexdump_len;
}
