#include "MenuUIStrings.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string_view>

namespace
{
	bool IsValidUtf8(const char* a_text)
	{
		const auto* p = reinterpret_cast<const unsigned char*>(a_text);
		while (*p) {
			if (*p <= 0x7F) {
				++p;
				continue;
			}

			std::uint32_t codePoint{};
			std::uint32_t minimum{};
			int continuation{};
			if ((*p & 0xE0) == 0xC0) {
				codePoint = *p & 0x1F;
				minimum = 0x80;
				continuation = 1;
			} else if ((*p & 0xF0) == 0xE0) {
				codePoint = *p & 0x0F;
				minimum = 0x800;
				continuation = 2;
			} else if ((*p & 0xF8) == 0xF0) {
				codePoint = *p & 0x07;
				minimum = 0x10000;
				continuation = 3;
			} else {
				return false;
			}

			++p;
			for (int i = 0; i < continuation; ++i, ++p) {
				if ((*p & 0xC0) != 0x80)
					return false;
				codePoint = (codePoint << 6) | (*p & 0x3F);
			}
			if (codePoint < minimum || codePoint > 0x10FFFF ||
				(codePoint >= 0xD800 && codePoint <= 0xDFFF))
				return false;
		}
		return true;
	}
}

int main()
{
	using namespace FMC::MenuUIStrings;

	// English is the default; Japanese only when chosen or when the game runs in Japanese
	assert(&Select(Language::kAuto, nullptr) == &kEnglish);
	assert(&Select(Language::kAuto, "ENGLISH") == &kEnglish);
	assert(&Select(Language::kAuto, "JAPANESE") == &kJapanese);
	assert(&Select(Language::kAuto, "japanese") == &kJapanese);
	assert(&Select(Language::kAuto, "JAPANESEX") == &kEnglish);
	assert(&Select(Language::kEnglish, "JAPANESE") == &kEnglish);
	assert(&Select(Language::kJapanese, "ENGLISH") == &kJapanese);

	assert(std::string_view(kEnglish.pagePath) == "Free Menu Cursor/Settings");
	assert(std::string_view(kJapanese.pagePath) == "Free Menu Cursor/設定");
	assert(std::string_view(kJapanese.intro) == "設定を変更したら「適用」を押してください。INIに保存され、変更が反映されます。");
	assert(std::string_view(kJapanese.applyButton) == "適用");
	assert(std::string_view(kJapanese.syncModes[1]) == "直接同期");
	assert(std::string_view(kEnglish.hotkeyNames[1]) == "Right Ctrl");
	assert(kHotkeyCodes[1] == 0x9D && kHotkeyCodes[kHotkeyCount - 1] == -1);

	auto checkTable = [](const UIText& t, bool asciiOnly) {
		int n = 0;
		ForEachString(t, [&](const char* text) {
			assert(text && *text && IsValidUtf8(text));
			if (asciiOnly)
				for (const char* c = text; *c; ++c)
					assert(static_cast<unsigned char>(*c) < 0x80);
			++n;
		});
		assert(n == static_cast<int>(sizeof(UIText) / sizeof(const char*)) && n >= 60);
	};
	checkTable(kEnglish, true);  // English must render with any Framework font
	checkTable(kJapanese, false);
	for (const auto* name : kJapanese.languageModes)  // the switch back must stay readable
		for (const char* c = name; *c; ++c)
			assert(static_cast<unsigned char>(*c) < 0x80);

	std::cout << "UI strings (English/Japanese) and UTF-8 encoding: PASS\n";
	return 0;
}
