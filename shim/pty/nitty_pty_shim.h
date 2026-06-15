/*
 * nitty_pty_shim.h — PTY (pseudo-terminal) C shim for Nitty
 *
 * v0.1.0: PTY allocation, slave access, and configuration.
 * v0.1.1: Session management, environment, shell spawning.
 *
 * Provides a thin C wrapper around POSIX PTY functions so that
 * Nitpick can call them via FFI. All functions use int64_t for
 * compatibility with Nitpick's int64 type.
 *
 * PTY allocation sequence:
 *   1. nitty_pty_openpt()          -> master fd
 *   2. nitty_pty_grantpt(fd)       -> 0 on success
 *   3. nitty_pty_unlockpt(fd)      -> 0 on success
 *   4. nitty_pty_get_slave_path(fd, buf) -> path length
 *   5. nitty_pty_open_slave(path)  -> slave fd
 *
 * Shell spawning (v0.1.1):
 *   nitty_pty_spawn_shell(master_fd, rows, cols) -> child PID
 *     Internally: open slave, fork, setsid, dup2, execve
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
 * PTY allocation (v0.1.0)
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_openpt(void);
int64_t nitty_pty_grantpt(int64_t master_fd);
int64_t nitty_pty_unlockpt(int64_t master_fd);
int64_t nitty_pty_get_slave_path(int64_t master_fd, int64_t buf_ptr);
int64_t nitty_pty_open_slave(int64_t path_ptr);
const char *nitty_pty_get_slave_path_str(int64_t master_fd);

/* ═══════════════════════════════════════════════════════════════════════
 * PTY configuration (v0.1.0)
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_set_nonblock(int64_t fd);
int64_t nitty_pty_set_winsize(int64_t fd, int64_t rows, int64_t cols,
                               int64_t xpixel, int64_t ypixel);
int64_t nitty_pty_get_winsize(int64_t fd);
int64_t nitty_pty_close(int64_t fd);

/* ═══════════════════════════════════════════════════════════════════════
 * Session management (v0.1.1)
 * ═══════════════════════════════════════════════════════════════════════ */

/** Create a new session. Returns new session ID or -1. */
int64_t nitty_pty_setsid(void);

/** Set controlling terminal. Returns 0 or -1. */
int64_t nitty_pty_set_ctty(int64_t slave_fd);

/** Close all fds in [first_fd, last_fd]. Ignores EBADF. */
void nitty_pty_close_range(int64_t first_fd, int64_t last_fd);

/* ═══════════════════════════════════════════════════════════════════════
 * Environment helpers (v0.1.1)
 * ═══════════════════════════════════════════════════════════════════════ */

/** Set an environment variable. Returns 0 or -1. */
int64_t nitty_pty_setenv(const char *name, const char *value);

/** Get environment variable. Returns value string or NULL. */
const char *nitty_pty_getenv_str(const char *name);

/** Get user's default shell. Checks $SHELL, /bin/bash, /bin/sh. */
const char *nitty_pty_get_default_shell(void);

/* ═══════════════════════════════════════════════════════════════════════
 * Shell spawning (v0.1.1)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Spawn a shell on the given PTY master.
 * Full fork/exec: open slave, fork, setsid, ctty, dup2, env, execve.
 * Returns child PID (> 0) on success, -1 on error.
 */
int64_t nitty_pty_spawn_shell(int64_t master_fd, int64_t rows, int64_t cols);

/**
 * Spawn a shell on the given PTY master with a specific working directory.
 * cwd: initial working directory for the shell (NULL = use default).
 * Returns child PID (> 0) on success, -1 on error.
 */
int64_t nitty_pty_spawn_shell_at(int64_t master_fd, int64_t rows, int64_t cols,
                                   const char *cwd);

/** Check if child is alive. Returns 1=alive, 0=exited, -1=error. */
int64_t nitty_pty_child_alive(int64_t pid);

/** Blocking wait for child exit. Returns raw wait status or -1. */
int64_t nitty_pty_wait_child(int64_t pid);

/** Non-blocking wait. Returns status if exited, 0 if running, -1 error. */
int64_t nitty_pty_wait_child_nonblock(int64_t pid);

/** Extract exit code from wait status. */
int64_t nitty_pty_exit_code(int64_t status);

/** Check if child was killed by signal. Returns 1 or 0. */
int64_t nitty_pty_was_signaled(int64_t status);

/** Get signal that killed child. */
int64_t nitty_pty_term_signal(int64_t status);

/** Send signal to child. Returns 0 or -1. */
int64_t nitty_pty_kill(int64_t pid, int64_t sig);

/** Signal constants. */
int64_t nitty_pty_SIGTERM(void);
int64_t nitty_pty_SIGKILL(void);
int64_t nitty_pty_SIGHUP(void);
int64_t nitty_pty_SIGWINCH(void);

/* ═══════════════════════════════════════════════════════════════════════
 * PTY I/O (v0.1.2)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Non-blocking read from fd.
 * Returns bytes read (> 0), 0 for EOF, -1 for error, -2 for EAGAIN.
 */
int64_t nitty_pty_read_raw(int64_t fd, int64_t buf_ptr, int64_t max_len);

/**
 * Write to fd. Handles partial writes by looping.
 * Returns total bytes written, or -1 on error.
 */
int64_t nitty_pty_write_raw(int64_t fd, int64_t buf_ptr, int64_t len);

/**
 * Write a string to fd. Convenience wrapper.
 * Returns bytes written, or -1 on error.
 */
int64_t nitty_pty_write_string(int64_t fd, const char *str);

/**
 * Read from fd into a static internal buffer (16KB).
 * Returns bytes read, 0 for EOF, -2 for EAGAIN, -1 for error.
 * Use nitty_pty_read_buf_ptr() and nitty_pty_read_buf_len() to access data.
 */
int64_t nitty_pty_read_buffered(int64_t fd);

/** Get pointer to internal read buffer. */
int64_t nitty_pty_read_buf_ptr(void);

/** Get number of valid bytes in internal read buffer. */
int64_t nitty_pty_read_buf_len(void);

/**
 * Get contents of read buffer as a null-terminated string.
 * Returns pointer to buffer (null-terminated after read_buf_len bytes).
 */
const char *nitty_pty_read_buf_str(void);

/**
 * Write a single byte to fd (for control characters).
 * Returns 1 on success, -1 on error.
 */
int64_t nitty_pty_write_byte(int64_t fd, int64_t byte_val);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_PTY_SHIM_H */
