/*
 * nitty_serial_shim.h — Serial port C shim for Nitty
 *
 * v0.1.0: Serial port open/close, configuration, I/O, and enumeration.
 *
 * Provides a thin C wrapper around POSIX termios/serial functions so that
 * Nitpick can call them via FFI. All functions use int64_t for
 * compatibility with Nitpick's int64 type.
 *
 * Serial open sequence:
 *   1. nitty_serial_open(path, baud, data_bits, parity,
 *                        stop_bits, flow_control)  -> fd
 *   2. nitty_serial_read_buffered(fd)              -> bytes read
 *   3. nitty_serial_write_string(fd, data)         -> bytes written
 *   4. nitty_serial_close(fd)                      -> 0 on success
 *
 * Port enumeration:
 *   nitty_serial_enumerate()   -> count of ports found
 *   nitty_serial_port_count()  -> same count
 *   nitty_serial_port_name(i)  -> device path string
 *   nitty_serial_port_desc(i)  -> human-readable description
 *
 * Parameter conventions:
 *   parity:       0=none, 1=odd, 2=even
 *   stop_bits:    1=one, 2=two
 *   flow_control: 0=none, 1=RTS/CTS (hardware), 2=XON/XOFF (software)
 *
 * Read return codes:
 *   >0  = bytes read
 *    0  = EOF / device disconnected
 *   -1  = error
 *   -2  = EAGAIN (no data available, try again)
 *
 * All functions return -1 on error unless otherwise documented.
 */

#ifndef NITTY_SERIAL_SHIM_H
#define NITTY_SERIAL_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Serial port lifecycle (v0.1.0)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Open and configure a serial port.
 *
 * path:         device path, e.g. "/dev/ttyUSB0"
 * baud:         baud rate (300, 1200, 2400, 4800, 9600, 19200, 38400,
 *               57600, 115200, 230400, 460800, 921600)
 * data_bits:    5, 6, 7, or 8
 * parity:       0=none, 1=odd, 2=even
 * stop_bits:    1=one stop bit, 2=two stop bits
 * flow_control: 0=none, 1=RTS/CTS (hardware), 2=XON/XOFF (software)
 *
 * Returns the file descriptor on success, -1 on error.
 * The port is opened in raw, non-blocking mode.
 * Original termios settings are saved and restored on close.
 */
int64_t nitty_serial_open(const char *path, int64_t baud, int64_t data_bits,
                           int64_t parity, int64_t stop_bits,
                           int64_t flow_control);

/**
 * Close a serial port fd.
 * Restores the original termios saved at open() time.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_serial_close(int64_t fd);

/* ═══════════════════════════════════════════════════════════════════════
 * Serial port configuration (v0.1.0)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Change the baud rate of an already-open serial port.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_serial_set_baud(int64_t fd, int64_t baud);

/**
 * Check whether fd refers to a TTY.
 * Returns 1 if fd is a TTY, 0 if not, -1 on error.
 */
int64_t nitty_serial_is_tty(int64_t fd);

/* ═══════════════════════════════════════════════════════════════════════
 * Serial port I/O (v0.1.0)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Non-blocking read into an internal 16KB static buffer.
 *
 * Returns:
 *   >0  — number of bytes read (access via nitty_serial_read_buf_str /
 *          nitty_serial_read_buf_len)
 *    0  — EOF / device disconnected
 *   -1  — error
 *   -2  — EAGAIN (no data currently available)
 */
int64_t nitty_serial_read_buffered(int64_t fd);

/** Get the number of valid bytes in the internal read buffer. */
int64_t nitty_serial_read_buf_len(void);

/**
 * Get the contents of the read buffer as a null-terminated string.
 * The pointer is valid until the next call to nitty_serial_read_buffered.
 */
const char *nitty_serial_read_buf_str(void);

/**
 * Write a null-terminated string to fd.
 * Returns bytes written, or -1 on error.
 */
int64_t nitty_serial_write_string(int64_t fd, const char *data);

/**
 * Write a single byte to fd (useful for control characters).
 * byte_val is masked to the low 8 bits.
 * Returns 1 on success, -1 on error.
 */
int64_t nitty_serial_write_byte(int64_t fd, int64_t byte_val);

/**
 * Discard all pending input and output on fd.
 * Equivalent to tcflush(fd, TCIOFLUSH).
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_serial_flush(int64_t fd);

/**
 * Wait until all pending output has been transmitted.
 * Equivalent to tcdrain(fd).
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_serial_drain(int64_t fd);

/* ═══════════════════════════════════════════════════════════════════════
 * Port enumeration (v0.1.0)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Scan /dev/ for serial ports and populate internal port list.
 *
 * Detects: ttyUSB*, ttyACM*, ttyS0–ttyS7.
 * For USB/ACM ports, reads manufacturer/product from sysfs when available.
 * Results are sorted: ttyUSB first, ttyACM second, ttyS last.
 *
 * Returns the number of ports found (also available via
 * nitty_serial_port_count). Up to 64 ports are stored.
 */
int64_t nitty_serial_enumerate(void);

/** Return the number of ports found by the last nitty_serial_enumerate call. */
int64_t nitty_serial_port_count(void);

/**
 * Return the device path of port i (e.g. "/dev/ttyUSB0").
 * Returns "" if i is out of range.
 * Pointer is valid until the next nitty_serial_enumerate call.
 */
const char *nitty_serial_port_name(int64_t i);

/**
 * Return a human-readable description of port i.
 * For USB/ACM ports this may include manufacturer and product strings.
 * For ttyS ports it is "Serial port".
 * Returns "" if i is out of range.
 * Pointer is valid until the next nitty_serial_enumerate call.
 */
const char *nitty_serial_port_desc(int64_t i);

/* ═══════════════════════════════════════════════════════════════════════
 * Serial control signals and modem status (v0.9.1)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Send a BREAK signal. duration_ms=0 uses tcsendbreak() default (~250ms).
 * Returns 0 on success, -1 on error. */
int64_t nitty_serial_send_break(int64_t fd, int64_t duration_ms);

/* Set or clear DTR (Data Terminal Ready) line.
 * state=1 asserts DTR, state=0 clears it.
 * Returns 0 on success, -1 on error. */
int64_t nitty_serial_set_dtr(int64_t fd, int64_t state);

/* Set or clear RTS (Request To Send) line.
 * state=1 asserts RTS, state=0 clears it.
 * Returns 0 on success, -1 on error. */
int64_t nitty_serial_set_rts(int64_t fd, int64_t state);

/* Read modem status register via TIOCMGET.
 * Returns bitmask (TIOCM_CTS | TIOCM_DSR | TIOCM_CD | TIOCM_RI), or -1 on error. */
int64_t nitty_serial_get_modem_status(int64_t fd);

/* Bitmask extractor helpers (operate on the value from get_modem_status) */
int64_t nitty_serial_modem_cts(int64_t status); /* 1 if CTS is asserted */
int64_t nitty_serial_modem_dsr(int64_t status); /* 1 if DSR is asserted */
int64_t nitty_serial_modem_dcd(int64_t status); /* 1 if DCD (carrier) is asserted */
int64_t nitty_serial_modem_ri(int64_t status);  /* 1 if RI (ring indicator) is asserted */

/* Write a single byte then sleep for delay_us microseconds.
 * Used for slow-feed mode (paste with inter-character delay).
 * Returns 1 on success, -1 on write error. */
int64_t nitty_serial_write_byte_delayed(int64_t fd, int64_t byte_val, int64_t delay_us);


/* ═══════════════════════════════════════════════════════════════════════
 * Byte-level string helpers (v0.9.1)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Extract the integer byte value of character at index i in a string.
 * Returns the byte value (0–255), or -1 if i is out of range. */
int64_t nitty_serial_byte_at(const char *s, int64_t i);

/* Format data as an xxd-style hex dump starting at byte_offset.
 * Output is stored in an internal static 64KB buffer.
 * Returns a pointer to the null-terminated hex dump string.
 * The string remains valid until the next call to nitty_serial_hexdump.
 * max_bytes: maximum number of bytes from data to format (0 = no limit). */
const char *nitty_serial_hexdump(const char *data, int64_t len,
                                  int64_t byte_offset, int64_t max_bytes);

/* Returns the length in bytes of the last nitty_serial_hexdump result. */
int64_t nitty_serial_hexdump_len(void);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_SERIAL_SHIM_H */
