#pragma once

namespace FMC
{
	// Undo a Windows pointer change left behind by a crash with a menu open.
	void RestorePointerFromLastRun();
	// Call from SKSEPlugin_Load (needs the trampoline).
	void InstallHooks();
	// Call on kDataLoaded (game window and other plugins exist by then).
	void OnDataLoaded();
	// Request a settings reload on the input-dispatch thread after the menu writes the INI.
	void RequestSettingsReload();
}
