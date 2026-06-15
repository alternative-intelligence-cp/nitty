/*
 * nitty_pty_shim.c — PTY (pseudo-terminal) C shim implementation
 *
 * v0.1.0: PTY allocation, slave access, winsize, and configuration.
 * v0.1.1: Session management, environment, shell spawning.
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
#include <sys/wait.h>
#include <signal.h>
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

/* ═══════════════════════════════════════════════════════════════════════
 * Session management (v0.1.1)
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_setsid(void)
{
    pid_t sid = setsid();
    if (sid < 0) {
        fprintf(stderr, "PTY: setsid failed: %s\n", strerror(errno));
        return -1;
    }
    return (int64_t)sid;
}

int64_t nitty_pty_set_ctty(int64_t slave_fd)
{
    /* TIOCSCTTY with arg=0: set controlling terminal */
    int result = ioctl((int)slave_fd, TIOCSCTTY, 0);
    if (result < 0) {
        fprintf(stderr, "PTY: ioctl TIOCSCTTY(%d) failed: %s\n",
                (int)slave_fd, strerror(errno));
        return -1;
    }
    return 0;
}

void nitty_pty_close_range(int64_t first_fd, int64_t last_fd)
{
    for (int fd = (int)first_fd; fd <= (int)last_fd; fd++) {
        close(fd);  /* silently ignore EBADF */
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Environment helpers (v0.1.1)
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_setenv(const char *name, const char *value)
{
    int result = setenv(name, value, 1);  /* 1 = overwrite */
    if (result != 0) {
        fprintf(stderr, "PTY: setenv(%s=%s) failed: %s\n",
                name, value, strerror(errno));
        return -1;
    }
    return 0;
}

const char *nitty_pty_getenv_str(const char *name)
{
    return getenv(name);
}

static char g_shell_path_buf[256];

const char *nitty_pty_get_default_shell(void)
{
    /* Try $SHELL first */
    const char *shell = getenv("SHELL");
    if (shell && shell[0] != '\0') {
        if (access(shell, X_OK) == 0) {
            snprintf(g_shell_path_buf, sizeof(g_shell_path_buf), "%s", shell);
            return g_shell_path_buf;
        }
    }

    /* Fall back to /bin/bash */
    if (access("/bin/bash", X_OK) == 0) {
        snprintf(g_shell_path_buf, sizeof(g_shell_path_buf), "/bin/bash");
        return g_shell_path_buf;
    }

    /* Last resort: /bin/sh */
    snprintf(g_shell_path_buf, sizeof(g_shell_path_buf), "/bin/sh");
    return g_shell_path_buf;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Shell spawning (v0.1.1)
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_spawn_shell(int64_t master_fd, int64_t rows, int64_t cols)
{
    /* Step 1: Get slave path */
    char slave_path[256];
    int pr = ptsname_r((int)master_fd, slave_path, sizeof(slave_path));
    if (pr != 0) {
        fprintf(stderr, "PTY: spawn: ptsname_r failed: %s\n", strerror(errno));
        return -1;
    }

    /* Step 2: Set initial terminal size */
    nitty_pty_set_winsize(master_fd, rows, cols, 0, 0);

    /* Step 3: Open slave fd */
    int slave_fd = open(slave_path, O_RDWR);
    if (slave_fd < 0) {
        fprintf(stderr, "PTY: spawn: open slave(%s) failed: %s\n",
                slave_path, strerror(errno));
        return -1;
    }

    /* Step 4: Fork */
    pid_t pid = fork();

    if (pid < 0) {
        /* Error */
        fprintf(stderr, "PTY: spawn: fork failed: %s\n", strerror(errno));
        close(slave_fd);
        return -1;
    }

    if (pid == 0) {
        /* ────── Child process ────── */

        /* Close master fd — only the parent uses it */
        close((int)master_fd);

        /* Create new session (detach from parent's controlling terminal) */
        if (setsid() < 0) {
            fprintf(stderr, "PTY: child: setsid failed: %s\n", strerror(errno));
            _exit(126);
        }

        /* Set slave as controlling terminal */
        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            fprintf(stderr, "PTY: child: TIOCSCTTY failed: %s\n", strerror(errno));
            _exit(126);
        }

        /* Redirect stdin/stdout/stderr to slave */
        if (dup2(slave_fd, STDIN_FILENO) < 0 ||
            dup2(slave_fd, STDOUT_FILENO) < 0 ||
            dup2(slave_fd, STDERR_FILENO) < 0) {
            fprintf(stderr, "PTY: child: dup2 failed: %s\n", strerror(errno));
            _exit(126);
        }

        /* Close original slave fd if it's not 0, 1, or 2 */
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        /* Close any leaked fds above stderr */
        for (int fd = STDERR_FILENO + 1; fd < 1024; fd++) {
            close(fd);
        }

        /* Set environment variables */
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "nitty", 1);
        setenv("TERM_PROGRAM_VERSION", "0.1.1", 1);

        /* Resolve shell path */
        const char *shell = nitty_pty_get_default_shell();

        /* Build argv: [shell, "-l", NULL] (login shell) */
        char *argv[] = { (char *)shell, (char *)"-l", NULL };

        /* Exec the shell (inherits current environment) */
        execv(shell, argv);

        /* If we get here, exec failed */
        fprintf(stderr, "PTY: child: execv(%s) failed: %s\n",
                shell, strerror(errno));
        _exit(127);
    }

    /* ────── Parent process ────── */

    /* Close slave fd — only the child uses it */
    close(slave_fd);

    /* Set master to non-blocking for async I/O */
    nitty_pty_set_nonblock(master_fd);

    fprintf(stderr, "PTY: spawned shell PID %d on %s\n",
            (int)pid, slave_path);

    return (int64_t)pid;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Child process management (v0.1.1)
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pty_child_alive(int64_t pid)
{
    int status = 0;
    pid_t result = waitpid((pid_t)pid, &status, WNOHANG);
    if (result == 0) {
        return 1;  /* still running */
    }
    if (result == (pid_t)pid) {
        return 0;  /* exited */
    }
    return -1;  /* error */
}

int64_t nitty_pty_wait_child(int64_t pid)
{
    int status = 0;
    pid_t result = waitpid((pid_t)pid, &status, 0);
    if (result < 0) {
        fprintf(stderr, "PTY: waitpid(%d) failed: %s\n",
                (int)pid, strerror(errno));
        return -1;
    }
    return (int64_t)status;
}

int64_t nitty_pty_wait_child_nonblock(int64_t pid)
{
    int status = 0;
    pid_t result = waitpid((pid_t)pid, &status, WNOHANG);
    if (result == 0) {
        return 0;  /* still running */
    }
    if (result == (pid_t)pid) {
        return (int64_t)status;
    }
    return -1;
}

int64_t nitty_pty_exit_code(int64_t status)
{
    int s = (int)status;
    if (WIFEXITED(s)) {
        return (int64_t)WEXITSTATUS(s);
    }
    return -1;
}

int64_t nitty_pty_was_signaled(int64_t status)
{
    int s = (int)status;
    return WIFSIGNALED(s) ? 1 : 0;
}

int64_t nitty_pty_term_signal(int64_t status)
{
    int s = (int)status;
    if (WIFSIGNALED(s)) {
        return (int64_t)WTERMSIG(s);
    }
    return -1;
}

int64_t nitty_pty_kill(int64_t pid, int64_t sig)
{
    int result = kill((pid_t)pid, (int)sig);
    if (result < 0) {
        fprintf(stderr, "PTY: kill(%d, %d) failed: %s\n",
                (int)pid, (int)sig, strerror(errno));
        return -1;
    }
    return 0;
}

int64_t nitty_pty_SIGTERM(void) { return (int64_t)SIGTERM; }
int64_t nitty_pty_SIGKILL(void) { return (int64_t)SIGKILL; }
int64_t nitty_pty_SIGHUP(void)  { return (int64_t)SIGHUP;  }

/* ═══════════════════════════════════════════════════════════════════════
 * PTY I/O (v0.1.2)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Internal 16KB read buffer + 1 byte for null terminator */
#define PTY_READ_BUF_SIZE 16384
static char g_read_buf[PTY_READ_BUF_SIZE + 1];
static int64_t g_read_buf_len = 0;

int64_t nitty_pty_read_raw(int64_t fd, int64_t buf_ptr, int64_t max_len)
{
    char *buf = (char *)(uintptr_t)buf_ptr;
    ssize_t n;

    do {
        n = read((int)fd, buf, (size_t)max_len);
    } while (n < 0 && errno == EINTR);  /* Retry on signal interrupt */

    if (n > 0) return (int64_t)n;
    if (n == 0) return 0;  /* EOF */
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;  /* Would block */
    return -1;  /* Real error */
}

int64_t nitty_pty_write_raw(int64_t fd, int64_t buf_ptr, int64_t len)
{
    const char *buf = (const char *)(uintptr_t)buf_ptr;
    int64_t total = 0;

    while (total < len) {
        ssize_t n = write((int)fd, buf + total, (size_t)(len - total));
        if (n < 0) {
            if (errno == EINTR) continue;  /* Retry on signal */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Non-blocking: return what we've written so far */
                if (total > 0) return total;
                return -2;
            }
            fprintf(stderr, "PTY: write(%d) failed: %s\n",
                    (int)fd, strerror(errno));
            return -1;
        }
        total += (int64_t)n;
    }
    return total;
}

int64_t nitty_pty_write_string(int64_t fd, const char *str)
{
    if (!str) return -1;
    size_t len = strlen(str);
    return nitty_pty_write_raw(fd, (int64_t)(uintptr_t)str, (int64_t)len);
}

int64_t nitty_pty_read_buffered(int64_t fd)
{
    g_read_buf_len = 0;
    int64_t n = nitty_pty_read_raw(fd, (int64_t)(uintptr_t)g_read_buf,
                                    PTY_READ_BUF_SIZE);
    if (n > 0) {
        g_read_buf_len = n;
        g_read_buf[n] = '\0';  /* null-terminate for str access */
    }
    return n;
}

int64_t nitty_pty_read_buf_ptr(void)
{
    return (int64_t)(uintptr_t)g_read_buf;
}

int64_t nitty_pty_read_buf_len(void)
{
    return g_read_buf_len;
}

const char *nitty_pty_read_buf_str(void)
{
    g_read_buf[g_read_buf_len] = '\0';
    return g_read_buf;
}

int64_t nitty_pty_write_byte(int64_t fd, int64_t byte_val)
{
    char c = (char)(byte_val & 0xFF);
    ssize_t n;
    do {
        n = write((int)fd, &c, 1);
    } while (n < 0 && errno == EINTR);

    if (n == 1) return 1;
    if (n < 0) {
        fprintf(stderr, "PTY: write_byte(%d, 0x%02X) failed: %s\n",
                (int)fd, (int)(byte_val & 0xFF), strerror(errno));
        return -1;
    }
    return -1;
}
