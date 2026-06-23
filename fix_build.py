import re

with open('build.abc', 'r') as f:
    content = f.read()

MEGA_LIBS = 'link_libraries = ["nitty_gtk4_shim", "nitty_pty_shim", "nitty_serial_shim", "nitty_telnet_shim", "nitpick_toml_shim", "nitpick_fs_shim", "nitpick_env_shim", "nitpick_ssh_shim", "nitpick_yaml_shim", "nitpick_semver", "nitpick_compress_shim", "nitpick_libc_mem", "nitpick_unix_socket_shim", "nitpick_socket", "nitpick_libc_net", "gtk-4", "pangocairo-1.0", "pango-1.0", "harfbuzz", "gdk_pixbuf-2.0", "cairo-gobject", "cairo", "vulkan", "graphene-1.0", "gio-2.0", "gobject-2.0", "glib-2.0", "util", "X11", "ssl", "crypto", "z", "nitty_pty_shim"]'
MEGA_PATHS = 'link_paths = [".nitpick_make/build", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-toml/shim", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-fs", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-env", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-ssh/shim/.nitpick_make/lib", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-yaml/shim", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-semver/shim/.nitpick_make/lib", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-compress", "/home/randy/Workspace/REPOS/nitpick-libc/shim", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-unix-socket/shim", "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-socket/shim/.nitpick_make/lib"]'

targets_to_fix = [
    'nitty', 'test_app', 'test_terminal_widget', 'test_terminal_integration',
    'test_search', 'test_selection', 'test_clipboard', 'test_session', 'test_tab_manager',
    'test_ssh_e2e', 'test_vt_conformance', 'test_e2e_shell', 'test_e2e_tabs_panes',
    'test_gtk4_ffi', 'test_renderer', 'test_input', 'test_terminal_grid', 'test_output_processing',
    'test_config', 'test_theme', 'test_hotkey', 'test_quake', 'test_global_hotkey', 'test_quake_minimal',
    'test_unicode_width', 'test_font_config', 'test_glyph_atlas', 'test_damage_tracker', 'test_render_perf',
    'test_terminal_polish', 'test_color_scheme', 'test_color_resolve', 'test_text_attributes', 'test_cursor', 'test_scrollback', 'test_bell', 'test_activity', 'test_process_monitor'
]

lines = content.split('\n')
current_target = None
out_lines = []

for line in lines:
    m = re.match(r'^\[target\.(.+)\]', line)
    if m:
        current_target = m.group(1)
        
    if line.startswith('link_libraries =') and current_target in targets_to_fix:
        out_lines.append(MEGA_LIBS)
    elif line.startswith('link_paths =') and current_target in targets_to_fix:
        out_lines.append(MEGA_PATHS)
    else:
        out_lines.append(line)

with open('build.abc', 'w') as f:
    f.write('\n'.join(out_lines))
