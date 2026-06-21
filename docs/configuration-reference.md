# Configuration reference

Persistent settings are stored in:

```
%APPDATA%\manual_map\settings.ini
```

Format: `key=value` lines, `#` or `;` comments, UTF-16 file read/write in `config.cpp`.

Implementation: `manual_map/include/app/config.hpp`, `manual_map/src/app/config.cpp`.

---

## Window and layout

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `window_x` | int | -1 | Last window X (-1 = default) |
| `window_y` | int | -1 | Last window Y |
| `window_w` | int | 1280 | Client width |
| `window_h` | int | 720 | Client height |
| `panel_split` | float | 0.42 | Injection page log/panel split |

---

## General

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `last_dll` | path | empty | Last used payload path |
| `recent_dll` | path | (repeat) | Up to 8 recent DLL paths |
| `confirm_inject` | bool | 0 | Legacy; confirm popup removed in GUI |
| `log_timestamps` | bool | 1 | Prefix log lines with time |
| `cli_notes` | string | empty | Passed to payload on inject |
| `language` | string | en | Reserved |
| `light_mode` | bool | 0 | Light vs dark theme |
| `compact_mode` | bool | 0 | Compact spacing/fonts |
| `first_run_complete` | bool | 0 | Skips first-run wizard when 1 |
| `min_to_tray` | bool | 0 | Minimize to tray |
| `watch_folder` | path | empty | Auto-watch folder for DLLs |
| `show_process_tree` | bool | 0 | Tree vs flat process list |

---

## Safety

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `use_allowlist` | bool | 0 | 1 = allowlist, 0 = blocklist |
| `process_rule` | string | (repeat) | Process name entries |

`is_process_allowed()` matches rules with wildcard semantics documented in `config.cpp`.

---

## Capture

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `stealth_capture` | bool | 0 | Hide window from screen capture |

---

## Section open state (GUI)

| Key | Default |
|-----|---------|
| `settings_appearance_open` | 1 |
| `settings_capture_open` | 1 |
| `settings_injection_open` | 1 |
| `settings_logging_open` | 1 |
| `settings_safety_open` | 1 |
| `settings_profiles_open` | 1 |
| `settings_advanced_open` | 1 |
| `settings_payload_open` | 1 |

---

## Payload DLL settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `payload_enabled` | bool | 1 | Master switch for payload protocol |
| `payload_silent` | bool | 0 | Hides MessageBox when 1 |
| `payload_show_message` | bool | 1 | Success MessageBox in target |
| `payload_file_log` | bool | 1 | Payload file logging |
| `payload_debug_log` | bool | 1 | OutputDebugString from payload |
| `payload_attach_console` | bool | 0 | AllocConsole in target |
| `payload_heartbeat` | bool | 1 | Heartbeat thread |
| `payload_proof_file` | bool | 1 | JSON proof file |
| `payload_module_watch` | bool | 1 | Ldr notification watcher |
| `payload_loadlib_hook` | bool | 1 | LoadLibraryW detour |
| `payload_hotkeys` | bool | 1 | F8/F9/F10 in target |
| `payload_ipc_pipe` | bool | 1 | Named pipe server |
| `payload_host_snapshot` | bool | 1 | Snapshot on attach |
| `payload_plugin_loader` | bool | 0 | Load plugin DLL |
| `payload_overlay` | bool | 1 | Overlay window |
| `payload_delay_ms` | uint | 0 | Delayed init milliseconds |
| `payload_heartbeat_ms` | uint | 1000 | Heartbeat interval |
| `payload_snapshot_mode` | uint | 3 | Bitmask: modules + threads |
| `payload_ui_message` | string | empty | Custom MessageBox text |
| `payload_log_path` | path | empty | Default `%TEMP%\manual_map_payload.log` |
| `payload_proof_dir` | path | empty | Default `%TEMP%\manual_map_proofs` |
| `payload_plugin_path` | path | empty | Secondary plugin DLL |

---

## Injection history

Repeated blocks (newest inserted at front, max **20** entries):

| Key | Description |
|-----|-------------|
| `history_timestamp` | ISO-like timestamp string |
| `history_target` | Target description |
| `history_dll` | DLL path used |
| `history_success` | 1 = success, 0 = failure |

Cleared via GUI **Clear History** (`clear_injection_history()`).

---

## Profiles

Repeated profile blocks:

| Key | Description |
|-----|-------------|
| `profile_name` | Display name |
| `profile_dll` | Saved DLL path |
| `profile_process` | Saved process name filter |
| `profile_wait` | Wait-for-process flag |
| `profile_inject_all` | Inject all instances flag |
| `profile_delay` | Delay seconds |

---

## Favorites and queue

| Key | Description |
|-----|-------------|
| `favorite_pid` | Pinned process PID (repeat) |
| `queue_dll` | DLL queue path (repeat) |

---

## API functions

| Function | Description |
|----------|-------------|
| `load_config` | Load default INI path into `app_config` |
| `save_config` | Write default INI path |
| `load_config_from_path` / `save_config_to_path` | Import/export |
| `remember_dll` | Push to recent list and last_dll |
| `add_injection_history` | Prepend history entry, trim to 20 |
| `clear_injection_history` | Empty history vector |
| `is_process_allowed` | Safety check |

---

## Example fragment

```ini
last_dll=C:\tools\payload_dll.dll
light_mode=0
payload_show_message=1
payload_silent=0
history_timestamp=2026-06-20 22:00:00
history_target=notepad.exe (PID 1234)
history_dll=C:\tools\payload_dll.dll
history_success=1
```

---

## Related

- [GUI settings UI](gui-application.md#settings-tab-draw_settings_page)
- [Payload flags](payload-dll.md#feature-flags)
