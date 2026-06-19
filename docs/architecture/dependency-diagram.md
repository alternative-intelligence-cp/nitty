# Dependency Diagram

The following Mermaid graph visualizes Nitty's high-level module dependencies. Nitty enforces a strict, layered architecture to prevent circular dependencies (which Nitpick does not support).

```mermaid
graph TD
    %% Layer Definitions
    subgraph GUI ["GUI Layer (src/gui)"]
        window(window.npk)
        tab_bar(tab_bar.npk)
        term_widget(terminal_widget.npk)
        conn_mgr(connection_manager.npk)
        plugin_ui(plugin_manager_ui.npk)
        renderer(renderer.npk)
    end

    subgraph Core ["Core Layer (src/core)"]
        app(app.npk)
        hotkey(hotkey.npk)
        tab_mgr(tab_manager.npk)
        pane(pane.npk)
    end

    subgraph Plugin ["Plugin Layer (src/plugin)"]
        plugin_mgr(plugin_manager.npk)
        plugin_api(api.npk)
        safe_path(plugin_safe_path.npk)
    end

    subgraph Conn ["Connection Layer (src/ssh, src/serial)"]
        ssh(ssh_session.npk)
        vault(ssh_vault.npk)
        serial(serial_port.npk)
    end

    subgraph Term ["Terminal Layer (src/terminal)"]
        vt(vt_parser.npk)
        state(terminal_state.npk)
        pty(pty.npk)
    end

    subgraph Config ["Config Layer (src/config)"]
        cfg(config.npk)
        schema(schema.npk)
        themes(theme.npk)
    end

    %% Flow/Dependencies
    app --> window
    app --> hotkey
    app --> plugin_mgr
    app --> ssh

    window --> tab_bar
    window --> term_widget
    window --> conn_mgr
    window --> plugin_ui

    term_widget --> renderer
    term_widget --> pane
    term_widget --> plugin_api

    pane --> vt
    pane --> pty
    pane --> ssh
    pane --> serial

    vt --> state

    conn_mgr --> ssh
    conn_mgr --> vault
    conn_mgr --> serial

    plugin_mgr --> plugin_api
    plugin_mgr --> safe_path

    %% Config is accessed everywhere
    GUI -.-> Config
    Core -.-> Config
    Term -.-> Config

    classDef gui fill:#d4edda,stroke:#28a745,color:#155724;
    classDef core fill:#cce5ff,stroke:#007bff,color:#004085;
    classDef term fill:#fff3cd,stroke:#ffc107,color:#856404;
    classDef conn fill:#f8d7da,stroke:#dc3545,color:#721c24;
    classDef plugin fill:#e2e3e5,stroke:#6c757d,color:#383d41;
    classDef config fill:#d1ecf1,stroke:#17a2b8,color:#0c5460;

    class window,tab_bar,term_widget,conn_mgr,plugin_ui,renderer gui;
    class app,hotkey,tab_mgr,pane core;
    class vt,state,pty term;
    class ssh,vault,serial conn;
    class plugin_mgr,plugin_api,safe_path plugin;
    class cfg,schema,themes config;
```

## Dependency Rules
1. **GUI depends on Core and Config.**
2. **Core depends on Terminal, Connection, and Plugin Layers.**
3. **Terminal depends *only* on Config and Libc bindings.**
4. **No circular imports.** If module A imports module B, B cannot import A. Shared state must be hoisted to a common dependency (like `constants.npk` or `schema.npk`).
