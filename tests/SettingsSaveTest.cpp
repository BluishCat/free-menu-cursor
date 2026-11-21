#include "Settings.h"

#include <Windows.h>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv)
{
	assert(argc == 2 && "pass a scratch directory");
	const std::filesystem::path root = argv[1];
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "Data" / "SKSE" / "Plugins");
	const auto ini = root / "Data" / "SKSE" / "Plugins" / "Free Menu Cursor.ini";
	{
		std::ofstream file(ini, std::ios::binary);
		file << "; preserve this comment\r\n"
			 << "[General]\r\n"
			 << "bEnabled=1\r\n"
			 << "; preserve unknown settings\r\n"
			 << "otherPluginOption=keep-me\r\n";
	}

	assert(::SetCurrentDirectoryA(root.string().c_str()));
	FMC::Settings settings;
	settings.Load();
	assert(settings.enabled);
	assert(settings.vanillaMenus);
	assert(std::fabs(settings.menuCursorSpeed - 1.0f) < 0.001f);
	assert(!settings.debugLog);        // off by default for release builds
	assert(settings.language == 0);    // auto when sLanguage is missing

	settings.enabled = false;
	settings.vanillaMenus = false;
	settings.smf = false;
	settings.flick = false;
	settings.vanillaSyncMode = 3;
	settings.flickSync = false;
	settings.relaxMouse = false;
	settings.alignOnOpen = false;
	settings.reclipOnClose = false;
	settings.hideOSCursorInsideWindow = false;
	settings.menuCursorSpeed = 2.25f;
	settings.menuPointerPrecision = 1;
	settings.freeCursorKey = 0xB8;
	settings.blockFreeCursorKey = false;
	settings.debugLog = true;
	settings.language = 2;
	assert(settings.Save());

	FMC::Settings loaded;
	loaded.Load();
	assert(!loaded.enabled);
	assert(!loaded.vanillaMenus);
	assert(!loaded.smf);
	assert(!loaded.flick);
	assert(loaded.vanillaSyncMode == 3);
	assert(!loaded.flickSync);
	assert(!loaded.relaxMouse);
	assert(!loaded.alignOnOpen);
	assert(!loaded.reclipOnClose);
	assert(!loaded.hideOSCursorInsideWindow);
	assert(std::fabs(loaded.menuCursorSpeed - 2.25f) < 0.001f);
	assert(loaded.menuPointerPrecision == 1);
	assert(loaded.freeCursorKey == 0xB8);
	assert(!loaded.blockFreeCursorKey);
	assert(loaded.debugLog);
	assert(loaded.language == 2);
	assert(::WritePrivateProfileStringA("General", "sLanguage", "ENGLISH", "Data\\SKSE\\Plugins\\Free Menu Cursor.ini"));
	loaded.Load();
	assert(loaded.language == 1);
	assert(::WritePrivateProfileStringA("General", "sLanguage", "nonsense", "Data\\SKSE\\Plugins\\Free Menu Cursor.ini"));
	loaded.Load();
	assert(loaded.language == 0);

	std::ifstream saved(ini, std::ios::binary);
	const std::string contents(std::istreambuf_iterator<char>(saved), {});
	assert(contents.find("; preserve this comment") != std::string::npos);
	assert(contents.find("; preserve unknown settings") != std::string::npos);
	assert(contents.find("otherPluginOption=keep-me") != std::string::npos);
	std::cout << "Settings save/load and INI preservation: PASS\n";
	return 0;
}