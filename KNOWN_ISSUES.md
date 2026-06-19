# Known Issues — Nitty v0.15.0

This document lists known limitations and issues that were triaged during beta testing but deferred to a future release. Issues are categorized by priority.

## Medium Priority

### M-001: Serial Port Permissions on Fedora (SELinux)
**Symptoms:** On Fedora systems with SELinux in Enforcing mode, opening `/dev/ttyUSB*` may fail with "Permission Denied" even if the user is in the `dialout` group.  
**Workaround:** Run `sudo setenforce 0` temporarily, or add a custom SELinux policy.  
**Target:** v1.0.1

### M-002: SFTP Browser — Large Directory Listing Performance
**Symptoms:** Directories containing >5000 files may cause a UI stall of 1–2 seconds while the listing populates.  
**Workaround:** Navigate to a more specific subdirectory.  
**Target:** v1.1.0

### M-003: Quake Mode — Multi-Monitor Positioning
**Symptoms:** On multi-monitor setups, the Quake Mode drop-down always appears on the primary monitor, regardless of which monitor holds the current focus.  
**Workaround:** Use regular window mode when working on secondary monitors.  
**Target:** v1.0.1

### M-004: AppImage — Wayland Compositors Without XWayland
**Symptoms:** The AppImage format does not bundle a Wayland EGL client library, and on pure Wayland compositors without XWayland installed, the application may not display.  
**Workaround:** Enable XWayland on the compositor (it is almost universally available by default).  
**Target:** v1.1.0

### M-005: Flatpak — Serial Port Access Requires Explicit Device Permission
**Symptoms:** In the Flatpak sandbox, `--device=all` grants access to serial ports in principle, but some older udev rulesets may block access. Users may need to manually run `flatpak override --user --device=all com.nitty.Terminal`.  
**Workaround:** Run the override command above once.  
**Target:** Investigate for v1.0.1

## Low Priority

### L-001: Tab Reordering via Drag-and-Drop
**Status:** Not yet implemented. Tabs can be switched but not reordered by dragging.  
**Workaround:** Use keyboard shortcuts to navigate.  
**Target:** v1.1.0

### L-002: Session Persistence Across Restarts
**Status:** Session restore (reopening tabs from a previous session) is not yet implemented.  
**Workaround:** None.  
**Target:** v1.1.0

### L-003: Right-to-Left (RTL) Text Rendering
**Status:** RTL text (Arabic, Hebrew) does not render in the correct visual order.  
**Workaround:** None.  
**Target:** v1.2.0

### L-004: Font Ligatures
**Status:** Programming ligatures (e.g., `=>`, `!=`) are not rendered, even when a ligature-capable font is configured.  
**Workaround:** None.  
**Target:** v1.1.0

### L-005: High CPU on Fast-Scrolling Output in VMs
**Symptoms:** In virtualized environments (VMware, VirtualBox) without GPU acceleration, rendering extremely fast output (e.g., `cat /dev/urandom`) can cause high CPU usage.  
**Workaround:** Pipe through a rate limiter: `cat /dev/urandom | pv -L 100k`.  
**Target:** Optimize in v1.0.1
