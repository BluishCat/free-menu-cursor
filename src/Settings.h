#pragma once

namespace FMC
{
	struct Settings
	{
		bool enabled{ true };
		bool vanillaMenus{ true };
		bool smf{ true };
		bool flick{ true };
		// 0 = off, 1 = direct (set position, zero delta), 2 = compensate (pre-offset), 3 = post-write
		int  vanillaSyncMode{ 1 };
		bool flickSync{ true };   // steer FLICK's cursor onto the OS cursor (from the outer input hook)
		bool relaxMouse{ true };  // switch the DirectInput mouse to non-exclusive while a menu is open
		bool alignOnOpen{ true };
		bool reclipOnClose{ true };
		bool hideOSCursorInsideWindow{ true };
		// Windows pointer while the menu cursor is free (the in-game cursor follows it).
		// 1.0 = leave the Windows setting alone; otherwise multiply its effective speed.
		float menuCursorSpeed{ 1.0f };
		// -1 = leave "Enhance pointer precision" alone, 0 = off, 1 = on (menus only)
		int  menuPointerPrecision{ -1 };
		// Hold this key (DirectInput scan code, 0 = off) to free the cursor without a menu.
		// Moving the cursor out of the window while holding keeps it free after release,
		// until the game window is clicked. 0x9D = Right Ctrl.
		int  freeCursorKey{ 0x9D };
		bool blockFreeCursorKey{ true };  // don't pass the key itself to the game / other mods
		bool debugLog{ false };
		// UI language: 0 = auto (follow Skyrim's sLanguage), 1 = English, 2 = Japanese
		int  language{ 0 };

		void Load();
		bool Save() const;
	};

	Settings& GetSettings();
}
