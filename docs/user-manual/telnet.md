# Telnet Guide

Nitty provides a built-in Telnet client, allowing you to connect to legacy network equipment, mainframes, MUDs, and BBS servers without launching an external `telnet` process.

## Connecting via Telnet

You can initiate a Telnet connection through the **Connection Manager** (`Ctrl+Shift+B`).

1. Create a new profile and set the type to **Telnet**.
2. Specify the hostname or IP address.
3. Specify the port (the standard default is `23`).

## Protocol Negotiation

Telnet is not just raw TCP; it includes an in-band negotiation protocol (RFC 854) using the `IAC` (Interpret As Command) escape byte (`0xFF`).

Nitty natively handles this negotiation. When the server requests capabilities (like Echo, Suppress Go Ahead, or Terminal Type), Nitty automatically responds according to modern terminal standards, ensuring the server correctly formats its output for Nitty's VT emulator.

## Use Cases and Limitations

**Security Warning:** Telnet transmits all data, including passwords, in plain text. It should never be used over the public internet or untrusted networks. Always prefer SSH when available.

Telnet remains useful for:
- Connecting to legacy managed switches and routers.
- Interacting with raw TCP services for debugging (similar to `netcat`).
- Connecting to specialized legacy systems or hobbyist servers.

For raw TCP connections that do *not* use the Telnet protocol (where `0xFF` might appear in binary data), use a local terminal running `nc` (netcat) instead of Nitty's built-in Telnet client, as Nitty will attempt to interpret `0xFF` bytes as Telnet commands.
