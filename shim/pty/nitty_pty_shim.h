/*
 * nitty_pty_shim.h — PTY (pseudo-terminal) C shim for Nitty
 *
 * v0.1.0: PTY allocation, slave access, and configuration.
 *
 * Provides a thin C wrapper around POSIX PTY functions so that
 * Nitpick can call them via FFI. All functions use int64_t for
 * compatibility with Nitpick's int64 type.
 *
 * PTY allocation sequence:
 *   1. nitty_pty_openpt()          → master fd
 *   2. nitty_pty_grantpt(fd)       → 0 on success
 *   3. nitty_pty_unlockpt(fd)      → 0 on success
 *   4. nitty_pty_get_slave_path(fd, buf) → path length
 *   5. nitty_pty_open_slave(path)  → slave fd
 *
 * All functions return -1 on error.
 */

#ifndef NITTY_PTY_SHIM_H
#define NITTY_PTY_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * PTY allocation
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Open a new pseudo-terminal master device.
 * Calls posix_openpt(O_RDWR | O_NOCTTY).
 * Returns master fd on success, -1 on error.
 */
int64_t nitty_pty_openpt(void);

/**
 * Change ownership and permissions of the slave PTY device.
 * Must be called after openpt, before unlockpt.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_pty_grantpt(int64_t master_fd);

/**
 * Unlock the slave PTY device for opening.
 * Must be called after grantpt.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_pty_unlockpt(int64_t master_fd);

/**
 * Get the slave device path for a master PTY fd.
 * Writes the null-terminated path into the buffer at buf_ptr.
 * buf_ptr must point to at least 256 bytes of writable memory.
 * Returns the path length (excluding null) on success, -1 on error.
 */
int64_t nitty_pty_get_slave_path(int64_t master_fd, int64_t buf_ptr);

/**
 * Open the slave PTY device by path.
 * path_ptr points to a null-terminated path string.
 * Returns slave fd on success, -1 on error.
 */
int64_t nitty_pty_open_slave(int64_t path_ptr);

/* ═══════════════════════════════════════════════════════════════════════
 * PTY configuration
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Set O_NONBLOCK flag on a file descriptor.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_pty_set_nonblock(int64_t fd);

/**
 * Set the terminal window size on a PTY master fd.
 * rows/cols set ws_row/ws_col; xpixel/ypixel set ws_xpixel/ws_ypixel.
 * Calls ioctl(fd, TIOCSWINSZ, &ws).
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_pty_set_winsize(int64_t fd, int64_t rows, int64_t cols,
                               int64_t xpixel, int64_t ypixel);

/**
 * Get the terminal window size from a PTY fd.
 * Returns encoded value: (rows << 48) | (cols << 32) | (xpixel << 16) | ypixel
 * Returns -1 on error.
 */
int64_t nitty_pty_get_winsize(int64_t fd);

/**
 * Close a file descriptor.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_pty_close(int64_t fd);

/**
 * Get the slave path as a null-terminated string.
 * Returns pointer to internal static buffer.
 * Valid until next call to nitty_pty_get_slave_path.
 */
const char *nitty_pty_get_slave_path_str(int64_t master_fd);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_PTY_SHIM_H */
