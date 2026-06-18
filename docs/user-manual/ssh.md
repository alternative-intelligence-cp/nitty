# SSH User Guide

Nitty features a first-class, built-in SSH client, eliminating the need to run external `ssh` commands in local terminals. This deep integration allows Nitty to offer advanced connection management, native SFTP file browsing, and secure credential storage.

## Connection Manager

Press `Ctrl+Shift+B` to open the Connection Manager. This sidebar is your hub for organizing remote connections.

- **Creating Profiles:** Click the `+` button to create a new SSH profile. You can define the host, port, username, and authentication method.
- **Groups:** Organize your profiles into folders by specifying a group name in the profile settings.
- **Quick Connect:** If you don't want to create a profile, use the quick connect bar at the top of the Connection Manager. Simply type `user@hostname` and press `Enter`.

## Authentication Methods

Nitty supports all standard SSH authentication methods:

1. **Password Authentication:** You will be prompted for a password upon connecting. Nitty's dialog securely handles the input.
2. **Public Key Authentication:** Uses keys from `~/.ssh/id_rsa`, `id_ed25519`, etc. You can specify a custom private key path in the profile.
3. **SSH Agent:** If an SSH agent (`ssh-agent` or `pageant`) is running, Nitty will automatically attempt to use loaded keys.
4. **Keyboard-Interactive:** Fully supports 2FA (Two-Factor Authentication) prompts, PAM modules, and custom server challenges.

## The Credential Vault

Nitty includes an encrypted credential vault. When you enter a password for an SSH connection, you have the option to save it. 
Saved credentials are encrypted at rest using your system's secure keychain. When you connect using that profile again, Nitty authenticates automatically without prompting.

## Host Key Verification

Like the standard OpenSSH client, Nitty verifies the server's identity to protect against man-in-the-middle attacks.
- **TOFU (Trust On First Use):** The first time you connect to a server, Nitty presents the server's fingerprint. If you accept it, it is saved.
- **known_hosts:** Nitty reads and updates your standard `~/.ssh/known_hosts` file, ensuring compatibility with other SSH tools.

## Jump Hosts (ProxyJump)

If a target server is only accessible via an intermediate bastion or jump host, you can configure this in your Nitty profile.
In the profile settings, enter the name or IP of the jump host. Nitty will transparently establish an SSH tunnel through the jump host to reach your final destination.

## Port Forwarding

Nitty supports robust port forwarding to tunnel traffic securely over SSH.

- **Local Forwarding (-L):** Forwards a port on your local machine to a port on the remote network. Example: `8080:localhost:80` (access remote web server locally).
- **Remote Forwarding (-R):** Forwards a port on the remote machine back to your local network.
- **Dynamic Forwarding (-D):** Creates a SOCKS proxy on your local machine, routing all traffic through the remote server.

You can configure persistent port forwards within your SSH profiles.

## SFTP Browser

When connected to an SSH server, Nitty can open a graphical file browser over the same connection.
Press `Ctrl+Shift+S` to toggle the SFTP pane.
- Navigate remote directories.
- Download files (they will be saved to your configured downloads directory).
- Upload files via drag-and-drop.
- Double-click text files to view or edit them directly within Nitty.

## X11 Forwarding

To run graphical Linux applications on the remote server and have them display on your local machine, enable **X11 Forwarding** in the SSH profile settings. Ensure that an X server (like XQuartz on Mac, or standard X11/Wayland-Xwayland on Linux) is running locally.

## Importing from OpenSSH Config

Nitty can read your existing `~/.ssh/config` file. It will automatically detect Host definitions, respect `ProxyJump` directives, identify custom `IdentityFile` paths, and populate your Connection Manager, giving you immediate access to your existing setups.

## Troubleshooting

If a connection fails, Nitty will display the error in the terminal window. Common issues include:
- **Permission Denied:** Verify your username, password, or ensure your private key is loaded in your SSH Agent.
- **Connection Refused:** Verify the remote hostname, IP, and port (default is 22). Ensure the server's firewall permits SSH traffic.
- **Host Key Verification Failed:** The remote server's identity has changed. This could indicate a security breach, or simply that the server was reinstalled. You must manually remove the offending key from `~/.ssh/known_hosts`.
