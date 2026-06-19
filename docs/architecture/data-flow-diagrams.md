# Data Flow Diagrams

These sequence diagrams illustrate the data flow for critical paths in Nitty's operation.

## 1. Input Flow (Keypress to Screen)

```mermaid
sequenceDiagram
    participant User
    participant GTK4 as GTK4 Shim
    participant Input as input.npk
    participant Hotkey as hotkey.npk
    participant Widget as terminal_widget.npk
    participant PTY as pty_io.npk
    
    User->>GTK4: Presses "A"
    GTK4->>Input: nitty_gtk4_on_key_pressed()
    Input->>Hotkey: hk_handle_key(keyval, modifiers)
    
    alt Is Hotkey Match
        Hotkey-->>Input: action_string ("tab.new")
        Input->>Widget: dispatch_action("tab.new")
        Widget->>Widget: Creates new tab
    else Not a Hotkey
        Hotkey-->>Input: "" (Empty)
        Input->>Input: Translate to UTF-8
        Input->>PTY: Write to PTY FD
    end
```

## 2. Output Flow (Screen Draw)

```mermaid
sequenceDiagram
    participant PTY as PTY / Socket
    participant GTK4 as GTK4 Main Loop
    participant VT as vt_parser.npk
    participant State as terminal_state.npk
    participant Renderer as renderer.npk
    participant C_Grid as C Render Buffer
    
    PTY-->>GTK4: FD Readable Event
    GTK4->>VT: Trigger parse loop
    VT->>PTY: Read chunk (e.g. 4096 bytes)
    
    loop Parse Escape Sequences
        VT->>State: Write char, move cursor, change color
    end
    
    State->>State: Mark cells as dirty
    
    Note over GTK4, C_Grid: Later, GTK requests a frame draw
    GTK4->>Renderer: tw_on_draw()
    Renderer->>State: Flush dirty cells
    State->>C_Grid: Copy dirty Nitpick cells to C struct
    C_Grid->>GTK4: Draw Cairo/Pango glyphs from C struct
```

## 3. SSH Connection Handshake

```mermaid
sequenceDiagram
    participant CM as connection_manager.npk
    participant SSH as ssh_session.npk
    participant Vault as ssh_vault.npk
    participant SSH2 as libssh2 (C Shim)
    
    CM->>SSH: ssh_connect(host, port, user)
    SSH->>SSH2: Setup non-blocking socket
    
    loop Non-blocking Handshake
        SSH2-->>SSH: EAGAIN
        SSH->>GTK4: Wait for socket readability
    end
    
    SSH2-->>SSH: Auth required
    SSH->>Vault: Check for saved password
    
    alt Password in Vault
        Vault-->>SSH: Returns decrypted password
    else Not in Vault
        SSH->>CM: Trigger auth_dialog.npk UI
        CM-->>SSH: User enters password
        SSH->>Vault: Save password (if checked)
    end
    
    SSH->>SSH2: Authenticate
    SSH2-->>SSH: Success
    SSH->>SSH2: Request PTY ("xterm-256color")
    SSH->>SSH2: Request Shell
    SSH-->>CM: Connection Established (FD bound to VT parser)
```
