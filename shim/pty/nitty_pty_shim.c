/*
 * nitty_pty_shim.c — PTY (pseudo-terminal) C shim implementation
 *
 * v0.1.0: PTY allocation, slave access, winsize, and configuration.
 *
 * This shim wraps POSIX PTY functions for Nitpick FFI access.
 * All pointer parameters are passed as int64_t and cast internally.
 *
 * Thread safety: Not thread-safe (static slave_path_buf).
 * For Nitty's single-threaded terminal model this is fine.
 */

#define _GNU_SOURCE

#include "nitty_pty_shim.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdio.h>

/* Static buffer for slave path (ptsname_r needs a buffer).
 * /dev/pts/N is typically < 20 chars, 256 is plenty. */
static char g_slave_path_buf[256];

/* ═══════════════════════════════════════════════════════════════════════
 * PTY allocation
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_openpt(void)
{
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "PTY: posix_openpt failed: %s\n", strerror(errno));
        return -1;
    }
    return (int64_t)fd;
}

int64_t nitty_pty_grantpt(int64_t master_fd)
{
    int result = grantpt((int)master_fd);
    if (result != 0) {
        fprintf(stderr, "PTY: grantpt(%d) failed: %s\n",
                (int)master_fd, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_pty_unlockpt(int64_t master_fd)
{
    int result = unlockpt((int)master_fd);
    if (result != 0) {
        fprintf(stderr, "PTY: unlockpt(%d) failed: %s\n",
                (int)master_fd, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_pty_get_slave_path(int64_t master_fd, int64_t buf_ptr)
{
    /* Use ptsname_r for thread safety (even though we're single-threaded) */
    char tmp[256];
    int result = ptsname_r((int)master_fd, tmp, sizeof(tmp));
    if (result != 0) {
        fprintf(stderr, "PTY: ptsname_r(%d) failed: %s\n",
                (int)master_fd, strerror(errno));
        return -1;
    }

    size_t len = strlen(tmp);

    /* Copy to caller's buffer if provided */
    if (buf_ptr != 0) {
        char *dst = (char *)(uintptr_t)buf_ptr;
        memcpy(dst, tmp, len + 1);  /* include null terminator */
    }

    /* Also save in static buffer for get_slave_path_str */
    memcpy(g_slave_path_buf, tmp, len + 1);

    return (int64_t)len;
}

int64_t nitty_pty_open_slave(int64_t path_ptr)
{
    if (path_ptr == 0) return -1;
    const char *path = (const char *)(uintptr_t)path_ptr;

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "PTY: open_slave(%s) failed: %s\n",
                path, strerror(errno));
        return -1;
    }
    return (int64_t)fd;
}

const char *nitty_pty_get_slave_path_str(int64_t master_fd)
{
    int result = ptsname_r((int)master_fd, g_slave_path_buf,
                           sizeof(g_slave_path_buf));
    if (result != 0) {
        g_slave_path_buf[0] = '\0';
    }
    return g_slave_path_buf;
}

/* ═══════════════════════════════════════════════════════════════════════
 * PTY configuration
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_set_nonblock(int64_t fd)
{
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) {
        fprintf(stderr, "PTY: fcntl F_GETFL(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }
    int result = fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
    if (result < 0) {
        fprintf(stderr, "PTY: fcntl F_SETFL O_NONBLOCK(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_pty_set_winsize(int64_t fd, int64_t rows, int64_t cols,
                               int64_t xpixel, int64_t ypixel)
{
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row    = (unsigned short)rows;
    ws.ws_col    = (unsigned short)cols;
    ws.ws_xpixel = (unsigned short)xpixel;
    ws.ws_ypixel = (unsigned short)ypixel;

    int result = ioctl((int)fd, TIOCSWINSZ, &ws);
    if (result < 0) {
        fprintf(stderr, "PTY: ioctl TIOCSWINSZ(%d, %dx%d) failed: %s\n",
                (int)fd, (int)cols, (int)rows, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_pty_get_winsize(int64_t fd)
{
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));

    int result = ioctl((int)fd, TIOCGWINSZ, &ws);
    if (result < 0) {
        fprintf(stderr, "PTY: ioctl TIOCGWINSZ(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }

    /* Pack into int64: rows(16) | cols(16) | xpixel(16) | ypixel(16) */
    int64_t packed = ((int64_t)ws.ws_row    << 48) |
                     ((int64_t)ws.ws_col    << 32) |
                     ((int64_t)ws.ws_xpixel << 16) |
                     ((int64_t)ws.ws_ypixel);
    return packed;
}

int64_t nitty_pty_close(int64_t fd)
{
    if (fd < 0) return -1;
    int result = close((int)fd);
    if (result < 0) {
        fprintf(stderr, "PTY: close(%d) failed: %s\n",
                (int)fd, strerror(errno));
        return -1;
    }
    return 0;
}
