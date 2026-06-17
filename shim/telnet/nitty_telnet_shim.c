/*
 * nitty_telnet_shim.c — Telnet client C shim implementation (v0.9.2)
 *
 * Implements TCP lifecycle, non-blocking read/write, hostname resolution,
 * and a key-input queue so GTK key events can be routed to the Telnet
 * socket from the Nitpick draw tick.
 *
 * All shim functions are called from the GTK main/draw thread.
 * Single-threaded: no locking needed.
 */

/* _GNU_SOURCE for struct addrinfo / getaddrinfo on older glibc */
/* Defined via -D_GNU_SOURCE in compiler flags (build.abc) */

#include "nitty_telnet_shim.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Hostname resolution
 * ═══════════════════════════════════════════════════════════════════════ */

static char g_resolve_buf[64];  /* enough for dotted-quad + NUL */

const char *nitty_telnet_resolve(const char *hostname)
{
    g_resolve_buf[0] = '\0';
    if (!hostname || hostname[0] == '\0') return g_resolve_buf;

    /* First try direct dotted-quad parse — no DNS lookup needed */
    struct in_addr ia;
    if (inet_pton(AF_INET, hostname, &ia) == 1) {
        strncpy(g_resolve_buf, hostname, sizeof(g_resolve_buf) - 1);
        g_resolve_buf[sizeof(g_resolve_buf) - 1] = '\0';
        return g_resolve_buf;
    }

    /* Resolve via getaddrinfo (DNS) */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;      /* IPv4 only */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(hostname, NULL, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "TELNET: resolve(%s) failed: %s\n",
                hostname, gai_strerror(rc));
        return g_resolve_buf;  /* empty string */
    }

    /* Take the first IPv4 result */
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    if (!inet_ntop(AF_INET, &sa->sin_addr, g_resolve_buf, sizeof(g_resolve_buf))) {
        g_resolve_buf[0] = '\0';
    }
    freeaddrinfo(res);

    fprintf(stderr, "TELNET: resolved %s → %s\n", hostname, g_resolve_buf);
    return g_resolve_buf;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Connection lifecycle
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_telnet_connect(const char *ip_str, int64_t port)
{
    if (!ip_str || ip_str[0] == '\0') {
        fprintf(stderr, "TELNET: connect: empty ip_str\n");
        return -1;
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "TELNET: connect: invalid port %lld\n", (long long)port);
        return -1;
    }

    /* Create TCP socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "TELNET: socket() failed: %s\n", strerror(errno));
        return -1;
    }

    /* Set SO_REUSEADDR */
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    /* Build sockaddr_in */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip_str, &addr.sin_addr) != 1) {
        fprintf(stderr, "TELNET: connect: invalid IP string \"%s\"\n", ip_str);
        close(fd);
        return -1;
    }

    /* Blocking connect */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "TELNET: connect(%s:%lld) failed: %s\n",
                ip_str, (long long)port, strerror(errno));
        close(fd);
        return -1;
    }

    /* Set non-blocking so reads return EAGAIN when no data */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) flags = 0;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    fprintf(stderr, "TELNET: connected to %s:%lld (fd=%d)\n",
            ip_str, (long long)port, fd);
    return (int64_t)fd;
}

int64_t nitty_telnet_close(int64_t fd)
{
    if (fd < 0) return -1;
    shutdown((int)fd, SHUT_RDWR);
    int rc = close((int)fd);
    if (rc < 0) {
        fprintf(stderr, "TELNET: close(%d) failed: %s\n", (int)fd, strerror(errno));
        return -1;
    }
    fprintf(stderr, "TELNET: closed fd=%d\n", (int)fd);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Non-blocking read
 * ═══════════════════════════════════════════════════════════════════════ */

#define TELNET_READ_BUF_SIZE 16384
static char    g_read_buf[TELNET_READ_BUF_SIZE + 1];
static int64_t g_read_buf_len = 0;

int64_t nitty_telnet_read(int64_t fd)
{
    g_read_buf_len = 0;
    g_read_buf[0]  = '\0';

    if (fd < 0) return -1;

    ssize_t n;
    do {
        n = recv((int)fd, g_read_buf, TELNET_READ_BUF_SIZE, 0);
    } while (n < 0 && errno == EINTR);

    if (n > 0) {
        g_read_buf_len = (int64_t)n;
        g_read_buf[n]  = '\0';
        return (int64_t)n;
    }
    if (n == 0) {
        return 0;   /* remote closed */
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return -2;  /* no data available this frame */
    }
    fprintf(stderr, "TELNET: read(%d) error: %s\n", (int)fd, strerror(errno));
    return -1;
}

int64_t nitty_telnet_read_buf_len(void)
{
    return g_read_buf_len;
}

const char *nitty_telnet_read_buf_str(void)
{
    g_read_buf[g_read_buf_len] = '\0';
    return g_read_buf;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Write
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_telnet_write(int64_t fd, const char *data, int64_t len)
{
    if (fd < 0 || !data || len <= 0) return 0;

    size_t total = 0;
    while (total < (size_t)len) {
        ssize_t n = send((int)fd, data + total, (size_t)len - total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "TELNET: write(%d) error: %s\n", (int)fd, strerror(errno));
            return (total > 0) ? (int64_t)total : -1;
        }
        total += (size_t)n;
    }
    return (int64_t)total;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Key input queue
 * Key presses from GTK are accumulated here and drained each frame by
 * the Nitpick draw tick, which then calls telnet_write().
 * ═══════════════════════════════════════════════════════════════════════ */

#define TELNET_KEY_BUF_MAX 4096
static char    g_key_buf[TELNET_KEY_BUF_MAX + 1];
static int64_t g_key_buf_len = 0;
static int64_t g_active_telnet_fd = -1;

void nitty_telnet_set_active_fd(int64_t fd)
{
    g_active_telnet_fd = fd;
    /* Clear stale key input from previous session */
    g_key_buf_len = 0;
    g_key_buf[0]  = '\0';
}

int64_t nitty_telnet_get_active_fd(void)
{
    return g_active_telnet_fd;
}

int64_t nitty_telnet_key_input_len(void)
{
    return g_key_buf_len;
}

const char *nitty_telnet_key_input_str(void)
{
    g_key_buf[g_key_buf_len] = '\0';
    return g_key_buf;
}

void nitty_telnet_key_input_consume(void)
{
    g_key_buf_len = 0;
    g_key_buf[0]  = '\0';
}

void nitty_telnet_key_input_push_byte(int64_t byte_val)
{
    if (g_active_telnet_fd < 0) return;
    if (g_key_buf_len >= TELNET_KEY_BUF_MAX) return;  /* drop if full */
    g_key_buf[g_key_buf_len++] = (char)(byte_val & 0xFF);
}

void nitty_telnet_key_input_push_str(const char *str)
{
    if (g_active_telnet_fd < 0 || !str) return;
    size_t slen = strlen(str);
    if (slen == 0) return;
    size_t available = (size_t)(TELNET_KEY_BUF_MAX - g_key_buf_len);
    if (slen > available) slen = available;  /* truncate if needed */
    memcpy(g_key_buf + g_key_buf_len, str, slen);
    g_key_buf_len += (int64_t)slen;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Protocol helpers
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_telnet_write_iac3(int64_t fd, int64_t cmd, int64_t opt)
{
    if (fd < 0) return -1;
    unsigned char buf[3];
    buf[0] = 0xFF;              /* IAC */
    buf[1] = (unsigned char)(cmd & 0xFF);
    buf[2] = (unsigned char)(opt & 0xFF);
    return nitty_telnet_write(fd, (const char *)buf, 3);
}

int64_t nitty_telnet_write_naws(int64_t fd, int64_t cols, int64_t rows)
{
    if (fd < 0) return -1;
    /* IAC SB NAWS cols_hi cols_lo rows_hi rows_lo IAC SE */
    unsigned char buf[9];
    buf[0] = 0xFF; /* IAC */
    buf[1] = 0xFA; /* SB  */
    buf[2] = 31;   /* NAWS */
    buf[3] = (unsigned char)((cols >> 8) & 0xFF);
    buf[4] = (unsigned char)(cols & 0xFF);
    buf[5] = (unsigned char)((rows >> 8) & 0xFF);
    buf[6] = (unsigned char)(rows & 0xFF);
    buf[7] = 0xFF; /* IAC */
    buf[8] = 0xF0; /* SE  */
    return nitty_telnet_write(fd, (const char *)buf, 9);
}

int64_t nitty_telnet_write_ttype_response(int64_t fd)
{
    if (fd < 0) return -1;
    /* IAC SB TTYPE IS "xterm-256color" IAC SE */
    static const char ttype_str[] = "xterm-256color";
    size_t tlen = strlen(ttype_str);
    /* 4 header bytes + tlen + 2 trailer bytes */
    size_t total = 4 + tlen + 2;
    unsigned char *buf = (unsigned char *)malloc(total);
    if (!buf) return -1;
    buf[0] = 0xFF;  /* IAC */
    buf[1] = 0xFA;  /* SB  */
    buf[2] = 24;    /* TTYPE */
    buf[3] = 0;     /* IS   */
    memcpy(buf + 4, ttype_str, tlen);
    buf[4 + tlen]     = 0xFF; /* IAC */
    buf[4 + tlen + 1] = 0xF0; /* SE  */
    int64_t rc = nitty_telnet_write(fd, (const char *)buf, (int64_t)total);
    free(buf);
    return rc;
}

static char g_byte_str_buf[2];

const char *nitty_telnet_byte_to_str(int64_t b)
{
    g_byte_str_buf[0] = (char)(b & 0xFF);
    g_byte_str_buf[1] = '\0';
    return g_byte_str_buf;
}

/* nitty_telnet_escape_iac — internal helper used by Nitpick telnet_write.
 * Returns a static buffer with every 0xFF doubled.
 * The Nitpick FFI wraps this differently: see telnet_ffi.npk. */
#define TELNET_ESCAPE_BUF_SIZE (16384 * 2 + 2)
static char    g_escape_buf[TELNET_ESCAPE_BUF_SIZE];
static int64_t g_escape_buf_len = 0;

/* Nitpick-callable version: input as (data str + len), output via two getters */
void nitty_telnet_escape_iac_prepare(const char *data, int64_t len)
{
    g_escape_buf_len = 0;
    if (!data || len <= 0) return;
    for (int64_t i = 0; i < len; i++) {
        unsigned char b = (unsigned char)data[i];
        if (g_escape_buf_len + 2 >= TELNET_ESCAPE_BUF_SIZE) break; /* safety */
        if (b == 0xFF) {
            g_escape_buf[g_escape_buf_len++] = (char)0xFF;
            g_escape_buf[g_escape_buf_len++] = (char)0xFF;
        } else {
            g_escape_buf[g_escape_buf_len++] = (char)b;
        }
    }
    g_escape_buf[g_escape_buf_len] = '\0';
}

const char *nitty_telnet_escape_iac_str(void)  { return g_escape_buf; }
int64_t     nitty_telnet_escape_iac_len(void)  { return g_escape_buf_len; }
