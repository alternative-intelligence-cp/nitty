/*
 * nitty_telnet_shim.h — Telnet client C shim for Nitty (v0.9.2)
 *
 * Provides TCP socket lifecycle and I/O helpers for the Telnet client module
 * (src/telnet/telnet_session.npk).  All pointer/fd parameters are int64_t.
 *
 * Thread safety: single-threaded (all functions called from GTK draw thread).
 */

#ifndef NITTY_TELNET_SHIM_H
#define NITTY_TELNET_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Hostname resolution
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * nitty_telnet_resolve — Resolve hostname to dotted-quad IPv4 address.
 *
 * Uses getaddrinfo(AF_INET).  Returns a pointer to a static buffer
 * containing the dotted-quad string (e.g. "93.184.216.34"), or an empty
 * string "" on failure.  The result is valid until the next call.
 */
const char *nitty_telnet_resolve(const char *hostname);

/* ═══════════════════════════════════════════════════════════════════════
 * Connection lifecycle
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * nitty_telnet_connect — Create TCP socket and connect to ip_str:port.
 *
 * ip_str must be a dotted-quad string (use nitty_telnet_resolve first).
 * After a successful connect the socket is set to non-blocking mode so
 * subsequent reads return EAGAIN rather than blocking.
 *
 * Returns the socket fd (>= 0) on success, -1 on failure.
 */
int64_t nitty_telnet_connect(const char *ip_str, int64_t port);

/**
 * nitty_telnet_close — Close the socket fd.
 *
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_telnet_close(int64_t fd);

/* ═══════════════════════════════════════════════════════════════════════
 * Non-blocking read (call every frame)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * nitty_telnet_read — Non-blocking recv from fd into internal 16 KB buffer.
 *
 * Returns:
 *   n > 0  — n bytes received; call nitty_telnet_read_buf_str() to get them
 *   0      — remote closed the connection (EOF)
 *  -1      — read error
 *  -2      — no data available (EAGAIN/EWOULDBLOCK) — normal, try next frame
 */
int64_t nitty_telnet_read(int64_t fd);

/** Number of bytes stored in the internal read buffer (valid after read > 0). */
int64_t nitty_telnet_read_buf_len(void);

/**
 * Pointer to the null-terminated read buffer (valid until next nitty_telnet_read).
 * The buffer contains raw bytes including any embedded NUL characters; use
 * nitty_telnet_read_buf_len() for the actual byte count.
 */
const char *nitty_telnet_read_buf_str(void);

/* ═══════════════════════════════════════════════════════════════════════
 * Write
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * nitty_telnet_write — Write len bytes of data to fd (blocking write loop).
 *
 * Returns bytes written (>= 0) on success, -1 on error.
 * Caller is responsible for IAC-escaping data before calling this.
 */
int64_t nitty_telnet_write(int64_t fd, const char *data, int64_t len);

/* ═══════════════════════════════════════════════════════════════════════
 * Key input queue (captures keystrokes from draw-thread GTK events)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * nitty_telnet_key_input_len — Number of bytes queued from keyboard input.
 *
 * The key input queue accumulates raw input bytes as they arrive from the GTK
 * key controller.  The Nitpick draw tick drains this each frame and writes
 * the bytes to the TCP socket via telnet_write.
 *
 * Returns the number of bytes currently in the queue (0..TELNET_KEY_BUF_MAX).
 */
int64_t nitty_telnet_key_input_len(void);

/**
 * nitty_telnet_key_input_str — Pointer to queued key input bytes.
 *
 * Valid until the next call to nitty_telnet_key_input_consume or any key event.
 * The buffer is NUL-terminated.
 */
const char *nitty_telnet_key_input_str(void);

/**
 * nitty_telnet_key_input_consume — Clear the key input queue.
 *
 * Call after draining the queue to the TCP socket.
 */
void nitty_telnet_key_input_consume(void);

/**
 * nitty_telnet_key_input_push_byte — Append one byte to the key queue.
 *
 * Called by the GTK input handler when the active pane is a Telnet pane.
 * If the queue is full the byte is silently dropped.
 */
void nitty_telnet_key_input_push_byte(int64_t byte_val);

/**
 * nitty_telnet_key_input_push_str — Append a C string to the key queue.
 *
 * Called by the GTK input handler for multi-byte sequences (e.g. arrow keys).
 */
void nitty_telnet_key_input_push_str(const char *str);

/**
 * nitty_telnet_set_active_fd — Register the active Telnet socket fd with the
 * input handler so key events are routed there instead of the PTY.
 *
 * Pass -1 to disable Telnet input routing.
 */
void nitty_telnet_set_active_fd(int64_t fd);

/**
 * nitty_telnet_get_active_fd — Return the currently active Telnet fd (-1 if none).
 */
int64_t nitty_telnet_get_active_fd(void);

/* ═══════════════════════════════════════════════════════════════════════
 * Protocol helpers (used by telnet_session.npk via FFI)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * nitty_telnet_write_iac3 — Send a 3-byte IAC command (IAC CMD OPT) to fd.
 * Handles bytes > 127 that cannot be expressed directly in Nitpick strings.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_telnet_write_iac3(int64_t fd, int64_t cmd, int64_t opt);

/**
 * nitty_telnet_write_naws — Send NAWS subnegotiation:
 *   IAC SB NAWS <cols_hi> <cols_lo> <rows_hi> <rows_lo> IAC SE
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_telnet_write_naws(int64_t fd, int64_t cols, int64_t rows);

/**
 * nitty_telnet_write_ttype_response — Send TTYPE IS "xterm-256color":
 *   IAC SB TTYPE IS "xterm-256color" IAC SE
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_telnet_write_ttype_response(int64_t fd);

/**
 * nitty_telnet_byte_to_str — Return a 1-character string whose single byte
 * has the raw value b (0..255).  The returned pointer is valid until the next
 * call.  This is needed because Nitpick string literals cannot encode bytes
 * above 127 as single-byte chars.
 */
const char *nitty_telnet_byte_to_str(int64_t b);

/**
 * nitty_telnet_escape_iac_prepare — Prepare IAC-escaped copy of data into
 * an internal buffer (every 0xFF byte doubled: IAC IAC).
 * Call nitty_telnet_escape_iac_str() and nitty_telnet_escape_iac_len() to read.
 */
void nitty_telnet_escape_iac_prepare(const char *data, int64_t len);

/** Escaped data buffer (valid until next prepare call). */
const char *nitty_telnet_escape_iac_str(void);

/** Length of escaped data. */
int64_t nitty_telnet_escape_iac_len(void);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_TELNET_SHIM_H */
