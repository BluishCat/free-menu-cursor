#pragma once

#include <cstddef>

// UI text for the SKSE Menu Framework settings page. English is the default; Japanese is
// chosen by sLanguage in the INI (auto follows Skyrim's own sLanguage setting).
// Keep this file UTF-8: the Framework treats every const char* as UTF-8.
namespace FMC::MenuUIStrings
{
	enum class Language
	{
		kAuto = 0,
		kEnglish = 1,
		kJapanese = 2,
	};

	inline constexpr std::size_t kSyncModeCount = 4;
	inline constexpr std::size_t kPrecisionModeCount = 3;
	inline constexpr std::size_t kLanguageModeCount = 3;
	inline constexpr std::size_t kHotkeyCount = 20;  // last entry = custom DIK code

	// DirectInput scan codes matching hotkeyNames; -1 = custom code
	inline constexpr int kHotkeyCodes[kHotkeyCount] = {
		0, 0x9D, 0xB8, 0x3A, 0x1D, 0x38, 0xD2,
		0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,
		-1
	};

	// Only const char* members: ForEachString walks the struct as an array of them.
	struct UIText
	{
		const char* pagePath;
		const char* intro;
		const char* generalSection;
		const char* enabled;
		const char* vanillaMenus;
		const char* smfSupport;
		const char* flickSupport;
		const char* blockFreeCursorKey;
		const char* debugLog;
		const char* language;
		const char* languageModes[kLanguageModeCount];
		const char* languageRestartNote;

		const char* syncSection;
		const char* syncModes[kSyncModeCount];
		const char* vanillaSync;
		const char* flickSync;
		const char* alignOnOpen;
		const char* reclipOnClose;
		const char* hideCursorInsideWindow;

		const char* mouseSection;
		const char* relaxMouse;
		const char* pointerNote;
		const char* pointerSpeed;
		const char* pointerPrecision;
		const char* precisionModes[kPrecisionModeCount];

		const char* hotkeyDescription;
		const char* hotkeyLabel;
		const char* hotkeyNames[kHotkeyCount];
		const char* customHotkeyHelp;
		const char* customDIKCode;

		const char* unsaved;
		const char* applyButton;
		const char* savedStatus;
		const char* saveError;
		const char* discardButton;
		const char* reloadedStatus;
	};

	// Language names stay ASCII in both tables: the default Framework font may lack
	// Japanese glyphs, and the user must be able to read the switch to get back.
	inline constexpr UIText kEnglish{
		.pagePath = "Free Menu Cursor/Settings",
		.intro = "Change settings, then press Apply. They are saved to the INI and take effect right away.",
		.generalSection = "General",
		.enabled = "Enable Free Menu Cursor",
		.vanillaMenus = "Free the cursor in vanilla menus",
		.smfSupport = "Free the cursor in SKSE Menu Framework",
		.flickSupport = "Free the cursor in FLICK",
		.blockFreeCursorKey = "Do not pass the free-cursor key to the game",
		.debugLog = "Verbose log",
		.language = "Language",
		.languageModes = { "Auto (follow the game)", "English", "Japanese" },
		.languageRestartNote = "The menu page name changes after restarting the game.",

		.syncSection = "Cursor sync",
		.syncModes = { "Off", "Direct", "Compensate movement", "Correct after move" },
		.vanillaSync = "Vanilla cursor sync mode",
		.flickSync = "Keep FLICK's cursor on the Windows cursor",
		.alignOnOpen = "Move the Windows cursor to the game cursor when a menu opens",
		.reclipOnClose = "Lock the cursor to the game window when the menu closes",
		.hideCursorInsideWindow = "Hide the Windows cursor over the game window",

		.mouseSection = "Mouse",
		.relaxMouse = "Use a non-exclusive mouse while a menu is open",
		.pointerNote = "Optional: these change the Windows pointer settings only while a menu cursor is free, "
		               "and restore them when the menu closes or the game exits. Default: unchanged.",
		.pointerSpeed = "Pointer speed in menus",
		.pointerPrecision = "Enhance pointer precision (mouse acceleration)",
		.precisionModes = { "Keep Windows setting", "Off", "On" },

		.hotkeyDescription = "Free-cursor key (hold to free the cursor without a menu)",
		.hotkeyLabel = "Free-cursor key",
		.hotkeyNames = {
			"Disabled", "Right Ctrl", "Right Alt", "Caps Lock", "Left Ctrl", "Left Alt", "Insert",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"Custom DIK code" },
		.customHotkeyHelp = "Enter a DirectInput scan code in decimal (0 = disabled; e.g. Right Ctrl = 157, Right Alt = 184, Caps Lock = 58).",
		.customDIKCode = "DIK scan code (decimal)",

		.unsaved = "You have unsaved changes.",
		.applyButton = "Apply",
		.savedStatus = "Saved. Settings are reloaded on the next input frame.",
		.saveError = "Could not write Free Menu Cursor.ini. Check that the mod folder is writable.",
		.discardButton = "Discard and reload from INI",
		.reloadedStatus = "Settings reloaded from the INI.",
	};

	inline constexpr UIText kJapanese{
		.pagePath = "Free Menu Cursor/設定",
		.intro = "設定を変更したら「適用」を押してください。INIに保存され、変更が反映されます。",
		.generalSection = "基本設定",
		.enabled = "Free Menu Cursorを有効にする",
		.vanillaMenus = "バニラメニューのカーソルに対応",
		.smfSupport = "SKSE Menu Frameworkのカーソルに対応",
		.flickSupport = "FLICKのカーソルに対応",
		.blockFreeCursorKey = "フリーカーソルキーをゲームに送らない",
		.debugLog = "詳細ログを出力する",
		.language = "言語 (Language)",
		.languageModes = { "Auto (follow the game)", "English", "Japanese" },
		.languageRestartNote = "メニューのページ名はゲームの再起動後に切り替わります。",

		.syncSection = "カーソル同期",
		.syncModes = { "無効", "直接同期", "移動量を補正", "移動後に補正" },
		.vanillaSync = "バニラカーソルの同期方式",
		.flickSync = "FLICKのカーソルを同期する",
		.alignOnOpen = "メニューを開いたときカーソル位置を合わせる",
		.reclipOnClose = "メニューを閉じたときポインターをウィンドウ内に制限する",
		.hideCursorInsideWindow = "ゲームウィンドウ内でWindowsカーソルを隠す",

		.mouseSection = "マウス動作",
		.relaxMouse = "メニュー中に非排他マウスを使う",
		.pointerNote = "任意設定：メニューでカーソルが解放されている間だけWindowsのポインター設定を変更し、"
		               "メニューを閉じたときやゲーム終了時に元に戻します。既定では変更しません。",
		.pointerSpeed = "メニュー中のポインター速度倍率",
		.pointerPrecision = "ポインターの精度を高める（マウス加速）",
		.precisionModes = { "Windowsの設定を維持", "オフ", "オン" },

		.hotkeyDescription = "フリーカーソルキー（メニューなしで押している間、カーソルを解放）",
		.hotkeyLabel = "フリーカーソルキー",
		.hotkeyNames = {
			"無効", "右Ctrl", "右Alt", "Caps Lock", "左Ctrl", "左Alt", "Insert",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"DIKコードを指定" },
		.customHotkeyHelp = "DirectInputのスキャンコードを10進数で入力（0で無効。例：右Ctrl=157、右Alt=184、Caps Lock=58）。",
		.customDIKCode = "DIKスキャンコード（10進数）",

		.unsaved = "未保存の変更があります。",
		.applyButton = "適用",
		.savedStatus = "保存しました。入力ループで設定を再読み込みします。",
		.saveError = "Free Menu Cursor.ini に書き込めません。MO2のMODフォルダーが書き込み可能か確認してください。",
		.discardButton = "破棄してINIから再読み込み",
		.reloadedStatus = "INIから設定を再読み込みしました。",
	};

	// a_gameLanguage: Skyrim's sLanguage:General value (e.g. "ENGLISH", "JAPANESE"), may be null
	inline const UIText& Select(Language a_setting, const char* a_gameLanguage)
	{
		if (a_setting == Language::kJapanese)
			return kJapanese;
		if (a_setting == Language::kEnglish)
			return kEnglish;
		if (a_gameLanguage) {
			const char* j = "JAPANESE";
			const char* g = a_gameLanguage;
			while (*j && *g && ((*g | 0x20) == (*j | 0x20))) {
				++j;
				++g;
			}
			if (!*j && !*g)
				return kJapanese;
		}
		return kEnglish;
	}

	// Every string of a table, for encoding tests.
	template <class F>
	void ForEachString(const UIText& t, F&& f)
	{
		const char* const* begin = reinterpret_cast<const char* const*>(&t);
		const std::size_t  count = sizeof(UIText) / sizeof(const char*);
		for (std::size_t i = 0; i < count; ++i)
			f(begin[i]);
	}
}
