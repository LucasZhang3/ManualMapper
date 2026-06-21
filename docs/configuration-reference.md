# Configuration reference

Persistent settings are stored in:

```
%APPDATA%\manual_map\settings.ini
```

Typical expanded path: `C:\Users\<User>\AppData\Roaming\manual_map\settings.ini`.

Format: `key=value` lines, `#` or `;` comments, UTF-16 file read/write in `manual_map/src/app/config.cpp`.

Implementation: `manual_map/include/app/config.hpp`, `manual_map/src/app/config.cpp`.

See also: [GUI application - Settings](gui-application.md#settings-tab-draw_settings_page), [Payload DLL](payload-dll.md), [CLI reference](cli-reference.md), [Architecture](architecture.md).

---

## File format rules

| Rule | Behavior |
|------|----------|
| Encoding | UTF-16 LE with BOM on write (`save_config`) |
| Comments | Lines starting with `#` or `;` skipped |
| Keys | Trimmed whitespace around key and value |
| Booleans | `1`/`true`/`yes` = true; `0`/`false`/`no` = false; else fallback default |
| Repeated keys | `recent_dll`, `process_rule`, `history_*`, `profile_*`, `favorite_pid`, `queue_dll` append or start new block |
| Load reset | `load_config_stream` sets `config = {}` before parse |
| Directory | Created on first save if missing (`CreateDirectoryW`) |

---

## Window and layout

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `window_x` | int | -1 | Last window X (-1 = default centered placement) |
| `window_y` | int | -1 | Last window Y |
| `window_w` | int | 1280 | Client width saved on exit/resize |
| `window_h` | int | 720 | Client height |
| `panel_split` | float | 0.42 | Injection page log width fraction (0.0-1.0) |

Used by `gui_app.cpp` / `gui_state.cpp` when restoring window geometry.

---

## General

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `last_dll` | path | empty | Last used payload path |
| `recent_dll` | path | (repeat, max 8) | Recent DLL paths, newest first via `push_recent` |
| `confirm_inject` | bool | 0 | Legacy; confirm popup removed in GUI |
| `log_timestamps` | bool | 1 | Prefix log lines with time in `append_log` |
| `cli_notes` | string | empty | Passed to payload `cli_notes` on inject |
| `language` | string | en | Reserved for future localization |
| `light_mode` | bool | 0 | Light vs dark theme |
| `compact_mode` | bool | 0 | Compact spacing/fonts |
| `first_run_complete` | bool | 0 | When 0, first-run wizard may show |
| `min_to_tray` | bool | 0 | Minimize to tray instead of taskbar |
| `watch_folder` | path | empty | Folder polled for newest `.dll` (GUI) |
| `show_process_tree` | bool | 0 | Tree indent vs flat process list |

---

## Safety

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `use_allowlist` | bool | 0 | 1 = allowlist mode, 0 = blocklist mode |
| `process_rule` | string | (repeat) | Process executable names, one per line in GUI |

### `is_process_allowed` logic

Source: `manual_map/src/app/config.cpp`.

1. If `process_rules` empty: **allow all**.
2. Match rule with exact `_wcsicmp` on full process name (e.g. `notepad.exe`).
3. **Allowlist** (`use_allowlist=1`): allow only if name listed.
4. **Blocklist** (`use_allowlist=0`): allow unless name listed.

**No wildcards** in current implementation. Entry `notepad.exe` does not match `Notepad.exe` case-sensitively but does via `_wcsicmp`.

Applied in:

- `run_injection` (`inject_service.cpp`) for CLI and core
- `launch_single_injection` (`gui_state.cpp`) before worker thread

Blocked inject returns code **`0x1000`** with log "Process blocked by safety rules".

---

## Capture

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `stealth_capture` | bool | 0 | Hide GUI from screen capture (`window_stealth.cpp`) |

When toggled at runtime, GUI calls `SetWindowDisplayAffinity` on main HWND.

---

## Section open state (GUI)

Controls collapsible Settings sections (persisted between sessions):

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
| `payload_enabled` | bool | 1 | Master switch for payload protocol session |
| `payload_silent` | bool | 0 | Sets SILENT flag, clears MessageBox |
| `payload_show_message` | bool | 1 | Success MessageBox in target |
| `payload_file_log` | bool | 1 | Payload file logging |
| `payload_debug_log` | bool | 1 | OutputDebugString from payload |
| `payload_attach_console` | bool | 0 | AllocConsole in target |
| `payload_heartbeat` | bool | 1 | Heartbeat thread |
| `payload_proof_file` | bool | 1 | JSON proof file |
| `payload_module_watch` | bool | 1 | Ldr notification watcher |
| `payload_loadlib_hook` | bool | 1 | LoadLibraryW detour |
| `payload_hotkeys` | bool | 1 | F8/F9/F10 in target |
| `payload_ipc_pipe` | bool | 1 | Named pipe server + injector ping |
| `payload_host_snapshot` | bool | 1 | Snapshot on attach |
| `payload_plugin_loader` | bool | 0 | Load plugin DLL when path set |
| `payload_overlay` | bool | 1 | Overlay window |
| `payload_delay_ms` | uint | 0 | Delayed init milliseconds |
| `payload_heartbeat_ms` | uint | 1000 | Heartbeat interval |
| `payload_snapshot_mode` | uint | 3 | Bitmask: 1=modules, 2=threads, 3=all |
| `payload_ui_message` | string | empty | Custom MessageBox text (wide) |
| `payload_log_path` | path | empty | Default `%TEMP%\manual_map_payload.log` |
| `payload_proof_dir` | path | empty | Default `%TEMP%\manual_map_proofs` |
| `payload_plugin_path` | path | empty | Secondary plugin DLL |

Mapped to `payload_config.feature_flags` in `build_payload_config` (`payload_bridge.cpp`).

---

## Injection history

Repeated blocks (newest inserted at front by `add_injection_history`, max **20** entries).

| Key | Description |
|-----|-------------|
| `history_timestamp` | Local timestamp string from GUI |
| `history_target` | Target description (name + PID) |
| `history_dll` | DLL path used |
| `history_success` | 1 = success, 0 = failure |

**Parse order:** `history_target` starts a new entry object; subsequent keys fill current entry until next `history_target`.

Cleared via GUI **Clear History** (`clear_injection_history()` + `save_config`).

---

## Profiles

Repeated profile blocks (`inject_profile` struct):

| Key | Description |
|-----|-------------|
| `profile_name` | Display name (required; empty names stripped on load) |
| `profile_dll` | Saved DLL path |
| `profile_process` | Saved process name filter |
| `profile_wait` | Wait-for-process flag |
| `profile_inject_all` | Inject all instances flag |
| `profile_delay` | Delay seconds (GUI converts to ms on load) |

`profile_name` starts new profile entry in file order.

---

## Favorites and queue

| Key | Description |
|-----|-------------|
| `favorite_pid` | Pinned process PID (repeat, uint32) |
| `queue_dll` | DLL queue path for sequential inject (repeat) |

Favorites shown in process list UI; queue consumed by `launch_injection` in `gui_state.cpp`.

---

## API functions

| Function | File | Description |
|----------|------|-------------|
| `load_config(app_config&)` | config.cpp | Load default INI path |
| `save_config(const app_config&)` | config.cpp | Write default INI path |
| `load_config_from_path` / `save_config_to_path` | config.cpp | Import/export arbitrary path |
| `default_config_path()` | config.cpp | Returns `%APPDATA%\manual_map\settings.ini` |
| `remember_dll` | config.cpp | Sets `last_dll`, pushes `recent_dlls` (max 8) |
| `remove_recent_dll` | config.cpp | Removes one path from recent list |
| `add_injection_history` | config.cpp | Prepend history entry, trim to 20 |
| `clear_injection_history` | config.cpp | Empty history vector |
| `is_process_allowed` | config.cpp | Safety check before inject |

---

## Example fragment

```ini
last_dll=C:\tools\payload_dll.dll
light_mode=0
use_allowlist=0
process_rule=csrss.exe
payload_enabled=1
payload_show_message=1
payload_silent=0
recent_dll=C:\tools\payload_dll.dll
history_timestamp=2026-06-20 22:00:00
history_target=notepad.exe (PID 1234)
history_dll=C:\tools\payload_dll.dll
history_success=1
profile_name=Notepad test
profile_dll=C:\tools\payload_dll.dll
profile_process=notepad.exe
profile_wait=0
profile_inject_all=0
profile_delay=0
```

---

## How to modify configuration

### Add a new setting

1. Add field to `app_config` in `manual_map/include/app/config.hpp`.
2. Add branch in `apply_line` in `config.cpp`.
3. Add write line in `write_config_stream`.
4. Wire GUI control in `draw_settings_page`.
5. Document key in this file.

### Reset to factory defaults

Delete or rename `%APPDATA%\manual_map\settings.ini`. Next launch recreates defaults.

### Force first-run wizard

Set `first_run_complete=0` and restart GUI.

---

## Debugging configuration issues

| Symptom | Check |
|---------|-------|
| Settings revert | Another instance overwriting INI; grep `save_config` callers |
| Payload flags ignored | `payload_enabled=0` disables entire session |
| History not showing | Empty `history_target` entries stripped on load |
| Import failed | UTF-16 BOM; use GUI Export as template |
| Blocklist not working | Exact name match; verify `use_allowlist` value |

Use GUI **Advanced - Export settings** to snapshot working INI.

---

## Common failure modes

| Failure | Cause | Fix |
|---------|-------|-----|
| Inject blocked, no mapper log | Safety rules | Remove process from blocklist or add to allowlist |
| Recent DLL stale | Path moved | Pick new DLL; old entry remains until removed |
| Corrupt INI line | Missing `=` | Line skipped silently in parser |
| Two GUIs race | Concurrent save | Run single GUI instance |

---

## Related

- [GUI settings UI](gui-application.md#settings-tab-draw_settings_page)
- [Payload flags](payload-dll.md#feature-flags)
- [CLI configuration load](cli-reference.md#configuration)
