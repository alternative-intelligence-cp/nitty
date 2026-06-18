# Serial Console Guide

Nitty provides native support for connecting to serial ports (RS-232, UART, USB-to-Serial adapters). This is ideal for embedded development, configuring network switches, interacting with Arduino/Raspberry Pi consoles, and debugging hardware.

## Connecting to a Serial Port

You can initiate a serial connection through the **Connection Manager** (`Ctrl+Shift+B`).

1. Create a new profile and set the type to **Serial**.
2. Select the device (e.g., `/dev/ttyUSB0`, `/dev/ttyACM0`, or `COM3`).
3. Set the baud rate (e.g., `115200` or `9600`).
4. Configure data bits, parity, and stop bits (the standard default is 8-N-1).

## Serial Port Permissions (Linux)

To access serial ports on Linux without root privileges, your user must be part of the `dialout` or `tty` group.

```bash
sudo usermod -a -G dialout $USER
# You must log out and log back in for this change to take effect.
```

## Output Modes

Nitty offers two ways to view incoming serial data, which can be toggled on the fly by pressing **`F8`**:

1. **Text Mode (Default):** Incoming bytes are parsed as standard ANSI/UTF-8 terminal text. Ideal for standard shell consoles.
2. **Hexdump Mode:** Incoming bytes are displayed in a split-pane hex editor format (Offset, Hex bytes, ASCII representation). This is crucial for protocol debugging or when communicating with devices that send binary payloads.

## Input Modes

Different devices expect input differently. Nitty supports multiple input modes for serial communication:

- **Raw Mode:** Keystrokes are sent immediately to the device as you type them. This is how standard SSH/local terminals work.
- **Line Mode (Readline):** Nitty intercepts your keystrokes and provides a local text input bar at the bottom of the screen. The text is only sent to the device when you press `Enter`. This is useful for devices with slow or unreliable RX lines, or when typing complex AT commands.

## Newline Conversion

Different operating systems and devices use different line endings. In the Serial Profile settings, you can configure how Nitty translates the `Enter` key:

- `CR` (Carriage Return, `\r`) — Common for old Macs and some network gear.
- `LF` (Line Feed, `\n`) — Standard Linux/Unix.
- `CRLF` (`\r\n`) — Standard Windows and many AT modems/microcontrollers.

## Control Signals

For hardware debugging, Nitty allows you to manually manipulate the serial control lines. These options are available in the tab context menu (right-click the serial tab):

- **Toggle DTR (Data Terminal Ready):** Often used to reset microcontrollers (like Arduinos) to enter bootloader mode.
- **Toggle RTS (Request To Send):** Used for manual hardware flow control.
- **Send BREAK Signal:** Sends a continuous spacing condition (logic 0) for a specified duration. You can trigger this quickly using the hotkey **`Ctrl+Shift+\``**. Break signals are often used to drop into ROM monitors (e.g., SysRq on Linux, ROMMON on Cisco).

## File Transfers (Zmodem)

Nitty natively supports the Zmodem file transfer protocol over serial connections. 

If `zmodem.auto_detect` is enabled in your configuration (it is by default), Nitty will automatically intercept Zmodem transfer headers (`**\x18B`) sent by the remote device. 

To download a file from the device:
1. Run `sz filename` on the remote device's serial console.
2. Nitty will intercept the sequence, display a progress bar, and save the file to your configured `zmodem.download_dir` (default: `~/Downloads`).
