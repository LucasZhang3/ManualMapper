# Screenshot placeholders

PNG assets for documentation live in `docs/images/`. The main docs link to these files for visual reference.

See [Documentation index](../INDEX.md) for which doc references each image.

---

## Provided in repository (01-13)

These files exist under `docs/images/` and are referenced from the docs:

| File | What to capture |
|------|-----------------|
| `01-main-window-injection.png` | Full window on Injection tab: target panel, payload panel, output log, action buttons |
| `02-tab-bar.png` | Title bar and Injection / History / Settings tabs |
| `03-process-list.png` | Process list with search, sort, tree mode, favorites |
| `04-payload-panel.png` | Payload path, recent DLLs, PE hash line, Browse / Add |
| `05-output-log.png` | Output log with filter, Copy Log, Jump to bottom |
| `06-inject-success-popup.png` | Notepad (or target) showing Manual Map success MessageBox after inject |
| `07-status-bar.png` | Bottom status bar: Ready, target, Admin/Standard |
| `08-history-tab.png` | History tab with entries, Re-inject, Clear History |
| `09-settings-appearance.png` | Settings: Appearance section |
| `10-settings-capture.png` | Settings: Capture / stealth section |
| `11-settings-injection.png` | Settings: Injection options |
| `12-settings-payload.png` | Settings: Payload DLL feature toggles |
| `13-command-palette.png` | Ctrl+K command palette open |

---

## Optional / not provided (14-16)

These screenshots are **not included** in the repository. Documentation describes the features with text and Mermaid diagrams instead of PNGs. You may add them later for richer docs; filenames would be:

| File | Status | Documented in |
|------|--------|-------------|
| `14-first-run-wizard.png` | **Optional, not provided** | [GUI application - First-run wizard](../gui-application.md#first-run-wizard-gui_draw_first_run_wizard) |
| `15-cli-list-processes.png` | **Optional, not provided** | [CLI reference - Interactive mode](../cli-reference.md#interactive-mode-flow) |
| `16-drag-drop-overlay.png` | **Optional, not provided** | [GUI application - Drag overlay](../gui-application.md#drag-overlay-gui_draw_drag_overlay) |

No broken image links should point to 14-16 until files are added.

---

## Capturing new screenshots

1. Build Release x64 per [build-and-deployment.md](../build-and-deployment.md).
2. Run `bin\Release\x64\manual_map_gui.exe` (or CLI for item 15 if you add it).
3. Save PNG to `docs/images/` using exact filenames above.
4. Prefer 1280x720 or native window size; avoid personal paths in visible UI.

For optional 14-16, add markdown image references to the relevant doc sections only after the PNG exists.

---

## Related

- [GUI application](../gui-application.md)
- [CLI reference](../cli-reference.md)
