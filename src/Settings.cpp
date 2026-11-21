#include "Settings.h"

namespace FMC
{
	namespace
	{
		constexpr auto kPath = "Data\\SKSE\\Plugins\\Free Menu Cursor.ini";

		bool ReadBool(const char* a_key, bool a_default)
		{
			return ::GetPrivateProfileIntA("General", a_key, a_default ? 1 : 0, kPath) != 0;
		}

		int ReadInt(const char* a_key, int a_default)
		{
			return static_cast<int>(::GetPrivateProfileIntA("General", a_key, a_default, kPath));
		}

		// accepts decimal or 0x-prefixed hex (GetPrivateProfileInt is decimal only)
		int ReadIntAuto(const char* a_key, int a_default)
		{
			char buf[64]{};
			::GetPrivateProfileStringA("General", a_key, "", buf, sizeof(buf), kPath);
			char*      end = nullptr;
			const long v = std::strtol(buf, &end, 0);
			return end != buf ? static_cast<int>(v) : a_default;
		}

		float ReadFloat(const char* a_key, float a_default)
		{
			char buf[64]{};
			::GetPrivateProfileStringA("General", a_key, "", buf, sizeof(buf), kPath);
			char* end = nullptr;
			const float v = std::strtof(buf, &end);
			return end != buf && std::isfinite(v) ? v : a_default;
		}
	}

	Settings& GetSettings()
	{
		static Settings settings;
		return settings;
	}

	void Settings::Load()
	{
		enabled = ReadBool("bEnabled", true);
		vanillaMenus = ReadBool("bVanillaMenus", true);
		smf = ReadBool("bSKSEMenuFramework", true);
		flick = ReadBool("bFLICK", true);
		vanillaSyncMode = std::clamp(ReadInt("iVanillaSyncMode", 1), 0, 3);
		flickSync = ReadBool("bFLICKSync", true);
		relaxMouse = ReadBool("bNonExclusiveMouseInMenus", true);
		alignOnOpen = ReadBool("bAlignOnOpen", true);
		reclipOnClose = ReadBool("bReclipOnClose", true);
		hideOSCursorInsideWindow = ReadBool("bHideOSCursorInsideWindow", true);
		menuCursorSpeed = std::clamp(ReadFloat("fMenuCursorSpeed", 1.0f), 0.1f, 4.0f);
		menuPointerPrecision = std::clamp(static_cast<int>(std::lround(ReadFloat("iMenuPointerPrecision", -1.0f))), -1, 1);  // GetPrivateProfileInt turns negatives into 0
		freeCursorKey = std::clamp(ReadIntAuto("iFreeCursorKey", 0x9D), 0, 0xFF);
		blockFreeCursorKey = ReadBool("bBlockFreeCursorKey", true);
		debugLog = ReadBool("bDebugLog", false);
		{
			char buf[32]{};
			::GetPrivateProfileStringA("General", "sLanguage", "auto", buf, sizeof(buf), kPath);
			const std::string_view v(buf);
			auto eq = [&v](std::string_view a_word) {
				return v.size() == a_word.size() &&
				       std::equal(v.begin(), v.end(), a_word.begin(), [](char a, char b) { return (a | 0x20) == (b | 0x20); });
			};
			language = eq("en") || eq("english") ? 1 : eq("ja") || eq("japanese") ? 2 : 0;
		}
	}

	bool Settings::Save() const
	{
		auto writeValues = [](const Settings& s) {
			bool ok = true;
			auto write = [&ok](const char* key, const std::string& value) {
				if (!::WritePrivateProfileStringA("General", key, value.c_str(), kPath))
					ok = false;
			};
			auto boolean = [&write](const char* key, bool value) { write(key, value ? "1" : "0"); };
			auto integer = [&write](const char* key, int value) { write(key, std::to_string(value)); };

			boolean("bEnabled", s.enabled);
			boolean("bVanillaMenus", s.vanillaMenus);
			boolean("bSKSEMenuFramework", s.smf);
			boolean("bFLICK", s.flick);
			integer("iVanillaSyncMode", s.vanillaSyncMode);
			boolean("bFLICKSync", s.flickSync);
			boolean("bNonExclusiveMouseInMenus", s.relaxMouse);
			boolean("bAlignOnOpen", s.alignOnOpen);
			boolean("bReclipOnClose", s.reclipOnClose);
			boolean("bHideOSCursorInsideWindow", s.hideOSCursorInsideWindow);
			write("fMenuCursorSpeed", std::format("{:.3f}", s.menuCursorSpeed));
			integer("iMenuPointerPrecision", s.menuPointerPrecision);
			write("iFreeCursorKey", std::format("0x{:02X}", s.freeCursorKey));
			boolean("bBlockFreeCursorKey", s.blockFreeCursorKey);
			boolean("bDebugLog", s.debugLog);
			write("sLanguage", s.language == 1 ? "en" : s.language == 2 ? "ja" : "auto");
			return ok;
		};

		Settings normalized = *this;
		normalized.vanillaSyncMode = std::clamp(normalized.vanillaSyncMode, 0, 3);
		normalized.menuCursorSpeed = std::clamp(normalized.menuCursorSpeed, 0.1f, 4.0f);
		normalized.menuPointerPrecision = std::clamp(normalized.menuPointerPrecision, -1, 1);
		normalized.freeCursorKey = std::clamp(normalized.freeCursorKey, 0, 0xFF);
		normalized.language = std::clamp(normalized.language, 0, 2);

		Settings previous;
		previous.Load();
		if (!writeValues(normalized)) {
			writeValues(previous);  // best-effort rollback if a write fails
			::WritePrivateProfileStringA(nullptr, nullptr, nullptr, kPath);
			return false;
		}
		::WritePrivateProfileStringA(nullptr, nullptr, nullptr, kPath);  // best-effort flush
		return true;
	}
}
