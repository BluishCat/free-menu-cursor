#include "MenuUI.h"

#include "Cursor.h"
#include "Settings.h"
#include "MenuUIStrings.h"

#include <array>

namespace FMC
{
	namespace
	{
		struct ImVec2
		{
			float x;
			float y;
		};

		using RenderFunction = void(__stdcall*)();
		using AddSectionItemFunction = void(*)(const char*, RenderFunction);
		using TextUnformattedFunction = void(*)(const char*, const char*);
		using SeparatorFunction = void(*)();
		using CheckboxFunction = bool(*)(const char*, bool*);
		using SliderFloatFunction = bool(*)(const char*, float*, float, float, const char*, int);
		using ComboFunction = bool(*)(const char*, int*, const char* const[], int, int);
		using InputIntFunction = bool(*)(const char*, int*, int, int, int);
		using ButtonFunction = bool(*)(const char*, const ImVec2);

		struct FrameworkAPI
		{
			AddSectionItemFunction addSectionItem{};
			TextUnformattedFunction textUnformatted{};
			SeparatorFunction separator{};
			CheckboxFunction checkbox{};
			SliderFloatFunction sliderFloat{};
			ComboFunction combo{};
			InputIntFunction inputInt{};
			ButtonFunction button{};
		};

		FrameworkAPI g_api;

		template <class T>
		T Resolve(HMODULE a_module, const char* a_name)
		{
			return reinterpret_cast<T>(::GetProcAddress(a_module, a_name));
		}

		bool ResolveAPI()
		{
			auto module = ::GetModuleHandleW(L"SKSEMenuFramework.dll");
			if (!module)
				return false;

			g_api.addSectionItem = Resolve<AddSectionItemFunction>(module, "AddSectionItem");
			g_api.textUnformatted = Resolve<TextUnformattedFunction>(module, "igTextUnformatted");
			g_api.separator = Resolve<SeparatorFunction>(module, "igSeparator");
			g_api.checkbox = Resolve<CheckboxFunction>(module, "igCheckbox");
			g_api.sliderFloat = Resolve<SliderFloatFunction>(module, "igSliderFloat");
			g_api.combo = Resolve<ComboFunction>(module, "igCombo_Str_arr");
			g_api.inputInt = Resolve<InputIntFunction>(module, "igInputInt");
			g_api.button = Resolve<ButtonFunction>(module, "igButton");

			return g_api.addSectionItem && g_api.textUnformatted && g_api.separator && g_api.checkbox &&
			       g_api.sliderFloat && g_api.combo && g_api.inputInt && g_api.button;
		}

		void Text(const char* a_text)
		{
			g_api.textUnformatted(a_text, nullptr);
		}

		void Separator()
		{
			g_api.separator();
		}

		bool Checkbox(const char* a_label, bool& a_value)
		{
			return g_api.checkbox(a_label, &a_value);
		}

		bool Combo(const char* a_label, int& a_value, const char* const* a_items, int a_count)
		{
			return g_api.combo(a_label, &a_value, a_items, a_count, -1);
		}

		bool Button(const char* a_label)
		{
			return g_api.button(a_label, ImVec2{ 0.0f, 0.0f });
		}

		using MenuUIStrings::kHotkeyCodes;
		constexpr int kCustomHotkey = static_cast<int>(MenuUIStrings::kHotkeyCount) - 1;

		int FindHotkeyOption(int a_code)
		{
			for (int i = 0; i < kCustomHotkey; ++i)
				if (kHotkeyCodes[i] == a_code)
					return i;
			return kCustomHotkey;
		}

		const char* GameLanguage()
		{
			auto ini = RE::INISettingCollection::GetSingleton();
			auto setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
			return setting && setting->GetType() == RE::Setting::Type::kString ? setting->GetString() : nullptr;
		}

		const MenuUIStrings::UIText& T(int a_language)
		{
			return MenuUIStrings::Select(static_cast<MenuUIStrings::Language>(std::clamp(a_language, 0, 2)), GameLanguage());
		}

		struct PageState
		{
			Settings draft;
			bool initialized{};
			bool dirty{};
			int hotkeyChoice{};
			std::string status;
		};

		PageState g_page;
		int       g_registeredLanguage{};

		void __stdcall RenderSettings()
		{
			if (!g_page.initialized) {
				g_page.draft.Load();
				g_page.hotkeyChoice = FindHotkeyOption(g_page.draft.freeCursorKey);
				g_page.initialized = true;
			}

			const auto& t = T(g_page.draft.language);
			Text(t.intro);
			Separator();
			Text(t.generalSection);
			if (Checkbox(t.enabled, g_page.draft.enabled)) g_page.dirty = true;
			if (Checkbox(t.vanillaMenus, g_page.draft.vanillaMenus)) g_page.dirty = true;
			if (Checkbox(t.smfSupport, g_page.draft.smf)) g_page.dirty = true;
			if (Checkbox(t.flickSupport, g_page.draft.flick)) g_page.dirty = true;
			if (Checkbox(t.blockFreeCursorKey, g_page.draft.blockFreeCursorKey)) g_page.dirty = true;
			if (Checkbox(t.debugLog, g_page.draft.debugLog)) g_page.dirty = true;
			if (Combo(t.language, g_page.draft.language, t.languageModes, static_cast<int>(std::size(t.languageModes))))
				g_page.dirty = true;
			if (g_page.draft.language != g_registeredLanguage)
				Text(t.languageRestartNote);

			Separator();
			Text(t.syncSection);
			if (Combo(t.vanillaSync, g_page.draft.vanillaSyncMode, t.syncModes,
				static_cast<int>(std::size(t.syncModes))))
				g_page.dirty = true;
			if (Checkbox(t.flickSync, g_page.draft.flickSync)) g_page.dirty = true;
			if (Checkbox(t.alignOnOpen, g_page.draft.alignOnOpen)) g_page.dirty = true;
			if (Checkbox(t.reclipOnClose, g_page.draft.reclipOnClose)) g_page.dirty = true;
			if (Checkbox(t.hideCursorInsideWindow, g_page.draft.hideOSCursorInsideWindow)) g_page.dirty = true;

			Separator();
			Text(t.mouseSection);
			if (Checkbox(t.relaxMouse, g_page.draft.relaxMouse)) g_page.dirty = true;
			Text(t.pointerNote);
			if (g_api.sliderFloat(t.pointerSpeed, &g_page.draft.menuCursorSpeed, 0.1f, 4.0f, "%.2fx", 0))
				g_page.dirty = true;
			int precisionChoice = g_page.draft.menuPointerPrecision + 1;
			if (Combo(t.pointerPrecision, precisionChoice, t.precisionModes,
				static_cast<int>(std::size(t.precisionModes)))) {
				g_page.draft.menuPointerPrecision = precisionChoice - 1;
				g_page.dirty = true;
			}

			Separator();
			Text(t.hotkeyDescription);
			if (Combo(t.hotkeyLabel, g_page.hotkeyChoice, t.hotkeyNames,
				static_cast<int>(std::size(t.hotkeyNames))) &&
			    g_page.hotkeyChoice < kCustomHotkey) {
				g_page.draft.freeCursorKey = kHotkeyCodes[g_page.hotkeyChoice];
				g_page.dirty = true;
			}
			if (g_page.hotkeyChoice == kCustomHotkey) {
				Text(t.customHotkeyHelp);
				if (g_api.inputInt(t.customDIKCode, &g_page.draft.freeCursorKey, 1, 10, 0)) {
					g_page.draft.freeCursorKey = std::clamp(g_page.draft.freeCursorKey, 0, 0xFF);
					g_page.dirty = true;
				}
			}

			Separator();
			if (g_page.dirty)
				Text(t.unsaved);
			if (Button(t.applyButton)) {
				if (g_page.draft.Save()) {
					g_page.dirty = false;
					g_page.status = t.savedStatus;
					RequestSettingsReload();
				} else {
					g_page.status = t.saveError;
				}
			}
			if (Button(t.discardButton)) {
				g_page.draft.Load();
				g_page.hotkeyChoice = FindHotkeyOption(g_page.draft.freeCursorKey);
				g_page.dirty = false;
				g_page.status = t.reloadedStatus;
			}
			if (!g_page.status.empty())
				Text(g_page.status.c_str());
		}
	}

	void RegisterSettingsMenu()
	{
		if (!ResolveAPI()) {
			logger::warn("SKSE Menu Framework is present but its settings-page API is incomplete; UI not registered");
			return;
		}
		Settings current;
		current.Load();
		g_registeredLanguage = current.language;
		g_api.addSectionItem(T(current.language).pagePath, RenderSettings);
		logger::info("Registered Free Menu Cursor settings page in SKSE Menu Framework");
	}
}