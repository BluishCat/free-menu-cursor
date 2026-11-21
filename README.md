# Free Menu Cursor

[日本語版 README](README.ja.md) · [Download (Releases)](https://github.com/BluishCat/free-menu-cursor/releases/latest)

An SKSE plugin for Skyrim Special Edition / Anniversary Edition. While a menu cursor is on screen, the Windows cursor is no longer locked inside the game window, so you can move it to another monitor or another app (a browser, a wiki, Discord) without Alt+Tab.

## Features
- Frees the cursor in **vanilla menus**, **SKSE Menu Framework**, and **FLICK**.
- Keeps the in-game cursor on the Windows cursor, so it doesn't jump when you come back.
- **Free-cursor key** (default: Right Ctrl). Hold it to free the cursor without opening a menu. If you move the cursor out of the window while holding it, the cursor stays free after release until you click the game.
- Optional pointer speed / acceleration while in menus (off by default, see below).
- Settings page in SKSE Menu Framework, in English or Japanese (`sLanguage=auto|en|ja`), or edit the INI by hand.

## Requirements
- SKSE64 for your game version
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Optional: SKSE Menu Framework (settings page, cursor support), FLICK (cursor support)

## Compatibility
| Game version | Status |
|---|---|
| SE 1.5.97 | Tested |
| AE 1.6.1170 | Tested |
| AE 1.7.104 | Tested |
| Skyrim VR | Not supported |

- **SSE Display Tweaks:** cursor locking is handled while a menu is open. `LockCursor` can stay as it is.
- Mods that also hook the input dispatch (for example SKSE Menu Framework and Hotkey Atlas) chain correctly.
- ImGui overlays that show without a vanilla menu are not detected yet. Use the free-cursor key with those.

## Windows pointer settings (optional)
While the cursor is free, the in-game menu cursor follows the Windows cursor, so its speed is your Windows pointer speed. `fMenuCursorSpeed` and `iMenuPointerPrecision` can change the speed and the acceleration for menus only. **Both are off by default.** When enabled:
- The change is never saved to your Windows settings (no `SPIF_UPDATEINIFILE`).
- It is restored when the menu closes, when the game loses focus, and when the game exits.
- If the game crashes, `Free Menu Cursor Restore.exe` (a small helper started only when one of these options is on) restores the original values. The next launch also checks for leftover changes.

## Files
```
SKSE/Plugins/Free Menu Cursor.dll
SKSE/Plugins/Free Menu Cursor.pdb                         (crash-log symbols)
SKSE/Plugins/Free Menu Cursor.ini
SKSE/Plugins/Free Menu Cursor/Free Menu Cursor Restore.exe
SKSE/Plugins/Free Menu Cursor/THIRD_PARTY_NOTICES.txt
```

## Reporting problems
Attach these files, right after the session where the problem happened (they are overwritten on each launch):
- `Documents\My Games\Skyrim Special Edition\SKSE\Free Menu Cursor.log` (set `bDebugLog=1` first)
- `crash-*.log` from Crash Logger, if the game crashed

## Upgrading from 0.1.x (FreeMenuCursor)
Delete the old `FreeMenuCursor.dll`, and rename `FreeMenuCursor.ini` to `Free Menu Cursor.ini` to keep your settings.

## Building
Requirements: Visual Studio 2022 (MSVC x64), CMake 3.21+, Ninja (bundled with VS), and [vcpkg](https://github.com/microsoft/vcpkg).

1. Clone [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (`ng` branch) and set `COMMONLIB_SSE_FOLDER` to that folder.
2. Set `VCPKG_ROOT` to your vcpkg folder.
3. From an "x64 Native Tools Command Prompt for VS 2022":
   ```
   cmake --preset release
   cmake --build build/release
   ```
   Output: `build/release/Free Menu Cursor.dll` and `Free Menu Cursor Restore.exe`.

Tests (no game needed): `testsun_ui_strings_test.bat`, `testsun_settings_test.bat`.

## License
GPL-3.0-or-later (required by CommonLibSSE-NG, which this plugin links statically). See `LICENSE` and `THIRD_PARTY_NOTICES.txt`.
