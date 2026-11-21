#include "Cursor.h"
#include "Settings.h"
#include "MenuUI.h"

// Frees the Windows cursor while a cursor menu is open (vanilla Scaleform menus,
// SKSE Menu Framework, FLICK) so it can leave the game window and operate other apps.
//
// - Releases ClipCursor (SSE Display Tweaks LockCursor re-clips on focus/size messages,
//   so the window procedure releases it again after those messages).
// - Shows the OS cursor (ShowCursor counter), but keeps it hidden over the game's client
//   area via WM_SETCURSOR, where the game draws its own cursor.
// - Keeps the game's own cursor on top of the OS cursor position:
//     vanilla: rewrites RE::MenuCursor position / MouseMoveEvent deltas
//     FLICK:   steers its ImGui cursor through MouseMoveEvent deltas (custom backend)
//     SMF:     nothing, its ImGui Win32 backend already reads the OS cursor
// - Re-clips the cursor to the window when the menu closes.

namespace FMC
{
	namespace
	{
		// Prefix of FUCK_Interface, identical in FLICK API v5 and v6. Only reads fields.
		// The real struct is declared under #pragma pack(push, 1): no padding after version.
#pragma pack(push, 1)
		struct FlickInterface
		{
			unsigned int version;
			void*        registerTool;
			void*        registerWindow;
			void*        unregisterWindow;
			float (*GetResolutionScale)();
			float (*GetGlobalScale)();
			float (*GetUserScale)();
			void (*GetDisplaySize)(float*, float*);
			void (*TranslateScaleformToScreen)(float, float, float*, float*);
			void* getFont;
			void* pushFont;
			void* popFont;
			void* suspendRendering;
			void (*SetMenuOpen)(bool);
			bool (*IsMenuOpen)();
			float (*GetDeltaTime)();
			double (*GetTime)();
			void (*GetMouseDelta)(float*, float*);
			void (*GetMousePos)(float*, float*);
		};
#pragma pack(pop)
		static_assert(offsetof(FlickInterface, registerTool) == 4);
		static_assert(offsetof(FlickInterface, GetDisplaySize) == 4 + 6 * 8);
		static_assert(offsetof(FlickInterface, IsMenuOpen) == 4 + 13 * 8);
		static_assert(offsetof(FlickInterface, GetMousePos) == 4 + 17 * 8);

		// True when a_ptr lies inside the image of a_module (guards against layout drift).
		bool InModule(HMODULE a_module, const void* a_ptr)
		{
			if (!a_module || !a_ptr)
				return false;
			const auto base = reinterpret_cast<std::uintptr_t>(a_module);
			const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			const auto addr = reinterpret_cast<std::uintptr_t>(a_ptr);
			return addr >= base && addr < base + nt->OptionalHeader.SizeOfImage;
		}

		enum class Owner
		{
			kNone,
			kVanilla,
			kSMF,
			kFlick,
			kHotkey  // no menu: the free-cursor key is held (or was held while leaving the window)
		};

		constexpr std::string_view OwnerName(Owner a_owner)
		{
			switch (a_owner) {
			case Owner::kVanilla:
				return "vanilla"sv;
			case Owner::kSMF:
				return "SKSEMenuFramework"sv;
			case Owner::kFlick:
				return "FLICK"sv;
			case Owner::kHotkey:
				return "hotkey"sv;
			default:
				return "none"sv;
			}
		}

		struct Vec2
		{
			float x{};
			float y{};
		};

		const FlickInterface* g_flick{};
		bool (*g_smfBlocking)(){};
		std::atomic<bool> g_settingsReloadRequested{};

		HWND    g_hwnd{};
		WNDPROC g_origProc{};
		DWORD   g_windowThread{};
		UINT    g_msgShow{};

		std::atomic<bool> g_free{};  // read by the window thread, written by the input thread
		Owner g_owner{ Owner::kNone };
		int   g_showIncrements{};  // touched on the window thread only

		// vanilla gain: MenuCursor units per raw mouse unit, learned per axis
		Vec2 g_vanillaGain{ 1.0f, 1.0f };
		bool g_vanillaPending{};
		Vec2 g_vanillaPre{};
		Vec2 g_vanillaTarget{};
		Vec2 g_vanillaSent{};

		// FLICK gain: ImGui pixels per raw mouse unit, learned across frames
		Vec2 g_flickGain{ 1.0f, 1.0f };
		bool g_flickPending{};
		Vec2 g_flickPrev{};
		Vec2 g_flickSent{};

		std::uint32_t g_moveCount{};

		constexpr int kLeaveGraceFrames = 10;

		// ---- free-cursor hotkey (no menu) ----------------------------------------------
		// Held: cursor free. Leaving the window while held latches it free after release,
		// until the game window is clicked/reactivated. Releasing without leaving ends it.
		std::atomic<bool> g_hotkeyMode{};     // current owner is kHotkey (read by the window thread)
		std::atomic<bool> g_hotkeyHeld{};
		std::atomic<bool> g_hotkeyLatched{};
		std::atomic<bool> g_releaseLatch{};   // set by the window thread on a click / reactivation
		bool              g_wentOutside{};    // the cursor left the window during the current hold
		int               g_suppressMouseFrames{};  // drop mouse buttons after the releasing click

		int HotkeyVK(int a_dik)
		{
			if (a_dik <= 0)
				return 0;
			// DirectInput codes >= 0x80 are E0-prefixed (extended) scan codes
			const UINT scan = (a_dik & 0x80) ? (0xE000u | (a_dik & 0x7F)) : static_cast<UINT>(a_dik);
			return static_cast<int>(::MapVirtualKeyA(scan, MAPVK_VSC_TO_VK_EX));
		}

		// ---- DirectInput mouse cooperative level ---------------------------------------
		// The game acquires the mouse DISCL_EXCLUSIVE | DISCL_FOREGROUND (bBackgroundMouse=0).
		// Exclusive mode hides the cursor, suppresses legacy mouse messages (no WM_SETCURSOR)
		// and keeps clicks from activating other windows, so it is relaxed while a menu is open.
		constexpr std::uint32_t kDisclExclusive = 0x1;
		constexpr std::uint32_t kDisclNonExclusive = 0x2;
		constexpr std::uint32_t kDisclForeground = 0x4;
		constexpr std::uint32_t kDisclBackground = 0x8;

		bool                            g_mouseRelaxed{};
		REX::W32::IDirectInputDevice8A* g_relaxedDevice{};
		std::atomic<bool>               g_reapplyMouse{};  // set by WM_ACTIVATE on the window thread

		REX::W32::IDirectInputDevice8A* MouseDevice()
		{
			auto manager = RE::BSInputDeviceManager::GetSingleton();
			auto mouse = manager ? manager->GetMouse() : nullptr;
			return mouse ? mouse->GetRuntimeData().dInputDevice : nullptr;
		}

		std::string ModuleOf(const void* a_address)
		{
			HMODULE module{};
			if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					static_cast<LPCWSTR>(a_address), &module))
				return "?";
			char path[MAX_PATH]{};
			::GetModuleFileNameA(module, path, MAX_PATH);
			std::string name = path;
			const auto slash = name.find_last_of("\\/");
			return std::format("{}+0x{:X}", slash == std::string::npos ? name : name.substr(slash + 1),
				reinterpret_cast<std::uintptr_t>(a_address) - reinterpret_cast<std::uintptr_t>(module));
		}

		// IDirectInputDevice8A::SetCooperativeLevel is vtable slot 13. While a menu frees the
		// cursor, anyone re-requesting exclusive mode for the mouse (the game on refocus, for
		// example) gets non-exclusive instead.
		using SetCoopFn = HRESULT(__stdcall*)(void*, void*, std::uint32_t);
		SetCoopFn g_origSetCoop{};

		HRESULT __stdcall SetCoopHook(void* a_self, void* a_hwnd, std::uint32_t a_flags)
		{
			if (a_self != MouseDevice())
				return g_origSetCoop(a_self, a_hwnd, a_flags);
			std::uint32_t flags = a_flags;
			if (g_free && GetSettings().relaxMouse && (a_flags & kDisclExclusive))
				flags = kDisclNonExclusive | kDisclForeground;
			const auto result = g_origSetCoop(a_self, a_hwnd, flags);
			logger::info("Mouse SetCooperativeLevel(0x{:X}) from {} -> 0x{:X}: 0x{:08X}",
				a_flags, ModuleOf(_ReturnAddress()), flags, static_cast<std::uint32_t>(result));
			return result;
		}

		void InstallCoopHook(REX::W32::IDirectInputDevice8A* a_device)
		{
			auto vtable = *reinterpret_cast<std::uintptr_t**>(a_device);
			auto slot = &vtable[13];
			const auto hook = reinterpret_cast<std::uintptr_t>(&SetCoopHook);
			if (*slot == hook)
				return;
			if (g_origSetCoop && reinterpret_cast<std::uintptr_t>(g_origSetCoop) != *slot)
				logger::warn("Mouse device vtable changed; re-hooking SetCooperativeLevel");
			g_origSetCoop = reinterpret_cast<SetCoopFn>(*slot);
			REL::safe_write(reinterpret_cast<std::uintptr_t>(slot), &hook, sizeof(hook));
			logger::info("Hooked mouse SetCooperativeLevel (original {})", ModuleOf(reinterpret_cast<const void*>(g_origSetCoop)));
		}

		std::uint32_t GameMouseLevel()
		{
			auto ini = RE::INISettingCollection::GetSingleton();
			auto setting = ini ? ini->GetSetting("bBackgroundMouse:Controls") : nullptr;
			const bool background = setting && setting->GetBool();
			return background ? (kDisclNonExclusive | kDisclBackground) : (kDisclExclusive | kDisclForeground);
		}

		bool SetMouseLevel(std::uint32_t a_level, const char* a_tag)
		{
			auto device = MouseDevice();
			if (!device)
				return false;
			const auto hwnd = reinterpret_cast<REX::W32::HWND>(g_hwnd);
			device->Unacquire();
			const auto coop = device->SetCooperativeLevel(hwnd, a_level);
			const auto acquire = device->Acquire();
			logger::info("Mouse cooperative level -> 0x{:X} ({}): SetCooperativeLevel 0x{:08X}, Acquire 0x{:08X}",
				a_level, a_tag, static_cast<std::uint32_t>(coop), static_cast<std::uint32_t>(acquire));
			return coop >= 0;
		}

		void RelaxMouse(bool a_force = false)
		{
			if (!GetSettings().relaxMouse)
				return;
			if (g_mouseRelaxed && !a_force && MouseDevice() == g_relaxedDevice)
				return;
			if ((GameMouseLevel() & kDisclExclusive) == 0)
				return;  // the game already runs the mouse non-exclusive
			auto device = MouseDevice();
			if (!device)
				return;
			InstallCoopHook(device);
			g_relaxedDevice = device;
			g_mouseRelaxed = SetMouseLevel(kDisclNonExclusive | kDisclForeground, a_force ? "menu, reapplied" : "menu");
		}

		void RestoreMouse()
		{
			if (!g_mouseRelaxed)
				return;
			g_mouseRelaxed = false;
			SetMouseLevel(GameMouseLevel(), "gameplay");
		}

		bool ShouldLogMove()
		{
			++g_moveCount;
			return GetSettings().debugLog && (g_moveCount <= 40 || g_moveCount % 120 == 0);
		}

		void LearnGain(float& a_gain, float a_moved, float a_sent)
		{
			if (std::abs(a_sent) < 2.0f || std::abs(a_moved) < 0.5f)
				return;
			const float sample = a_moved / a_sent;
			if (!std::isfinite(sample) || std::abs(sample) < 0.01f || std::abs(sample) > 50.0f)
				return;
			a_gain = a_gain * 0.7f + sample * 0.3f;
		}

		// ---- window helpers ------------------------------------------------------

		bool OnWindowThread() { return ::GetCurrentThreadId() == g_windowThread; }

		bool IsForeground() { return g_hwnd && ::GetForegroundWindow() == g_hwnd; }

		bool ClientRectOnScreen(RECT& a_rect)
		{
			if (!::GetClientRect(g_hwnd, &a_rect))
				return false;
			POINT tl{ a_rect.left, a_rect.top };
			POINT br{ a_rect.right, a_rect.bottom };
			if (!::ClientToScreen(g_hwnd, &tl) || !::ClientToScreen(g_hwnd, &br))
				return false;
			a_rect = { tl.x, tl.y, br.x, br.y };
			return a_rect.right > a_rect.left && a_rect.bottom > a_rect.top;
		}

		// OS cursor in client coordinates, clamped into the client area.
		bool ClientCursor(POINT& a_point, SIZE& a_size, bool* a_outside = nullptr)
		{
			RECT client{};
			if (!::GetClientRect(g_hwnd, &client) || client.right <= 0 || client.bottom <= 0)
				return false;
			if (!::GetCursorPos(&a_point) || !::ScreenToClient(g_hwnd, &a_point))
				return false;
			a_size = { client.right, client.bottom };
			const bool outside = a_point.x < 0 || a_point.y < 0 || a_point.x >= a_size.cx || a_point.y >= a_size.cy;
			if (a_outside)
				*a_outside = outside;
			a_point.x = std::clamp<LONG>(a_point.x, 0, a_size.cx - 1);
			a_point.y = std::clamp<LONG>(a_point.y, 0, a_size.cy - 1);
			return true;
		}

		void Unclip()
		{
			RECT clip{};
			::GetClipCursor(&clip);
			const RECT all{
				::GetSystemMetrics(SM_XVIRTUALSCREEN),
				::GetSystemMetrics(SM_YVIRTUALSCREEN),
				::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
				::GetSystemMetrics(SM_YVIRTUALSCREEN) + ::GetSystemMetrics(SM_CYVIRTUALSCREEN)
			};
			if (!::EqualRect(&clip, &all))
				::ClipCursor(nullptr);
		}

		void Reclip()
		{
			RECT rect{};
			if (ClientRectOnScreen(rect))
				::ClipCursor(&rect);
		}

		void ApplyShow(bool a_show)  // window thread
		{
			if (a_show) {
				int count = 0;
				do {
					count = ::ShowCursor(TRUE);
					++g_showIncrements;
				} while (count < 0 && g_showIncrements < 64);
				if (GetSettings().debugLog)
					logger::info("OS cursor shown (count {}, increments {})", count, g_showIncrements);
			} else {
				int count = 0;
				const int increments = g_showIncrements;
				for (; g_showIncrements > 0; --g_showIncrements)
					count = ::ShowCursor(FALSE);
				if (GetSettings().debugLog)
					logger::info("OS cursor hidden (count {}, reverted {})", count, increments);
			}
		}

		void RequestShow(bool a_show)
		{
			if (OnWindowThread())
				ApplyShow(a_show);
			else
				::PostMessageA(g_hwnd, g_msgShow, a_show ? 1 : 0, 0);
		}

		// ---- coordinate mapping -------------------------------------------------------

		RE::MenuCursor::RUNTIME_DATA* MenuCursorData()
		{
			auto cursor = RE::MenuCursor::GetSingleton();
			return cursor ? &cursor->GetRuntimeData() : nullptr;
		}

		Vec2 MenuSpaceSize(const RE::MenuCursor::RUNTIME_DATA& a_data)
		{
			if (a_data.screenWidthX > 1.0f && a_data.screenWidthY > 1.0f)
				return { a_data.screenWidthX, a_data.screenWidthY };
			const auto size = RE::BSGraphics::Renderer::GetScreenSize();
			return { static_cast<float>(size.width), static_cast<float>(size.height) };
		}

		Vec2 FlickDisplaySize(SIZE a_client)
		{
			Vec2 size{ static_cast<float>(a_client.cx), static_cast<float>(a_client.cy) };
			if (g_flick && g_flick->GetDisplaySize) {
				float w = 0.0f, h = 0.0f;
				g_flick->GetDisplaySize(&w, &h);
				if (w > 1.0f && h > 1.0f)
					size = { w, h };
			}
			return size;
		}

		void MoveOSCursorToClient(float a_x, float a_y, SIZE a_client, Vec2 a_space)
		{
			POINT p{
				static_cast<LONG>(std::lround(a_x * a_client.cx / a_space.x)),
				static_cast<LONG>(std::lround(a_y * a_client.cy / a_space.y))
			};
			p.x = std::clamp<LONG>(p.x, 0, a_client.cx - 1);
			p.y = std::clamp<LONG>(p.y, 0, a_client.cy - 1);
			if (::ClientToScreen(g_hwnd, &p))
				::SetCursorPos(p.x, p.y);
		}

		void AlignOSCursor(Owner a_owner)
		{
			RECT client{};
			if (!::GetClientRect(g_hwnd, &client) || client.right <= 0 || client.bottom <= 0)
				return;
			const SIZE size{ client.right, client.bottom };
			if (a_owner == Owner::kVanilla) {
				if (auto data = MenuCursorData()) {
					const auto space = MenuSpaceSize(*data);
					MoveOSCursorToClient(data->cursorPosX, data->cursorPosY, size, space);
					if (GetSettings().debugLog)
						logger::info("Aligned OS cursor to MenuCursor ({:.1f}, {:.1f}) space {:.0f}x{:.0f} client {}x{}",
							data->cursorPosX, data->cursorPosY, space.x, space.y, size.cx, size.cy);
				}
			} else if (a_owner == Owner::kFlick && g_flick && g_flick->GetMousePos) {
				float x = 0.0f, y = 0.0f;
				g_flick->GetMousePos(&x, &y);
				const auto space = FlickDisplaySize(size);
				if (std::isfinite(x) && std::isfinite(y) && x >= 0.0f && y >= 0.0f) {
					MoveOSCursorToClient(x, y, size, space);
					if (GetSettings().debugLog)
						logger::info("Aligned OS cursor to FLICK ({:.1f}, {:.1f}) display {:.0f}x{:.0f}", x, y, space.x, space.y);
				}
			}
			// SMF: its Win32 backend follows the OS cursor; nothing to align.
		}

		// ---- state machine ------------------------------------------------------------

		Owner DetectOwner()
		{
			const auto& s = GetSettings();
			if (!s.enabled)
				return Owner::kNone;
			if (s.flick && g_flick && g_flick->IsMenuOpen && g_flick->IsMenuOpen())
				return Owner::kFlick;
			if (s.smf && g_smfBlocking && g_smfBlocking())
				return Owner::kSMF;
			if (s.vanillaMenus) {
				auto ui = RE::UI::GetSingleton();
				if (ui && ui->IsMenuOpen(RE::CursorMenu::MENU_NAME))
					return Owner::kVanilla;
			}
			if (g_hotkeyHeld || g_hotkeyLatched)
				return Owner::kHotkey;
			return Owner::kNone;
		}

		// Called once per frame from the input thread, before DetectOwner.
		void UpdateHotkey()
		{
			const auto& s = GetSettings();
			const int   vk = s.enabled ? HotkeyVK(s.freeCursorKey) : 0;
			const bool  down = vk && (::GetAsyncKeyState(vk) & 0x8000) != 0;
			const bool  wasHeld = g_hotkeyHeld;
			// A press only counts while the game is in front (not while typing in another app).
			const bool held = down && (wasHeld || IsForeground());
			g_hotkeyHeld = held;

			if (held && !wasHeld) {
				g_wentOutside = false;
				if (s.debugLog)
					logger::info("Free-cursor key down (DIK 0x{:X}, VK 0x{:X}){}", s.freeCursorKey, vk, g_hotkeyLatched ? " while latched" : "");
			}
			if (held) {
				POINT p{};
				SIZE  client{};
				bool  outside = false;
				if (ClientCursor(p, client, &outside) && outside && !g_wentOutside) {
					g_wentOutside = true;
					g_hotkeyLatched = true;
					if (s.debugLog)
						logger::info("Cursor left the window while the key is held: stays free until the game is clicked");
				}
			}
			if (!held && wasHeld) {
				if (!g_wentOutside && g_hotkeyLatched) {
					g_hotkeyLatched = false;  // a tap inside the window cancels the latch
					if (s.debugLog)
						logger::info("Free-cursor key tapped inside the window: latch cancelled");
				}
				if (s.debugLog)
					logger::info("Free-cursor key up ({})", g_hotkeyLatched ? "latched" : "released");
			}
			if (g_releaseLatch.exchange(false) && g_hotkeyLatched && !held) {
				g_hotkeyLatched = false;
				g_suppressMouseFrames = 20;  // the click that brought the game back is not an attack
				if (s.debugLog)
					logger::info("Game clicked/reactivated: free cursor latch released");
			}
		}

		void LogMenuCursor(const char* a_tag)
		{
			if (!GetSettings().debugLog)
				return;
			if (auto d = MenuCursorData()) {
				logger::info("{} MenuCursor pos=({:.1f},{:.1f}) safe=({:.1f},{:.1f}) size=({:.1f},{:.1f}) sens={:.3f} unk20={:.3f} unk24={:.3f} defSpeed={:.3f} show={}",
					a_tag, d->cursorPosX, d->cursorPosY, d->safeZoneX, d->safeZoneY, d->screenWidthX, d->screenWidthY,
					d->cursorSensitivity, d->unk20, d->unk24, d->defaultMouseSpeed, d->showCursorCount);
			}
		}

		// ---- Windows pointer speed while the menu cursor is free -------------------------
		// The in-game cursor follows the OS cursor (vanilla sync / FLICK sync), so menu cursor
		// speed is the Windows pointer speed. Change it only while free and the game is in
		// front; never persisted (no SPIF_UPDATEINIFILE). A marker file holds the originals
		// so the next launch can restore them if the game crashed with a menu open.
		std::mutex g_pointerLock;
		bool       g_pointerApplied{};
		int        g_origSpeed{};
		int        g_origMouse[3]{};

		std::filesystem::path PointerMarkerPath()
		{
			auto dir = SKSE::log::log_directory();
			return dir ? *dir / "Free Menu Cursor.pointer-restore" : std::filesystem::path{};
		}

		// marker written by builds before the rename (0.1.x, "FreeMenuCursor")
		std::filesystem::path LegacyPointerMarkerPath()
		{
			auto dir = SKSE::log::log_directory();
			return dir ? *dir / "FreeMenuCursor.pointer-restore" : std::filesystem::path{};
		}

		// effective multiplier of each Windows pointer-speed step (1..20)
		constexpr float kSpeedFactor[21] = { 0.0f, 1 / 32.0f, 1 / 16.0f, 1 / 8.0f, 2 / 8.0f, 3 / 8.0f, 4 / 8.0f, 5 / 8.0f, 6 / 8.0f, 7 / 8.0f,
			1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.25f, 3.5f };

		int ScaledSpeed(int a_orig, float a_mult)
		{
			const float want = kSpeedFactor[std::clamp(a_orig, 1, 20)] * a_mult;
			int         best = 1;
			for (int i = 2; i <= 20; ++i)
				if (std::fabs(kSpeedFactor[i] - want) < std::fabs(kSpeedFactor[best] - want))
					best = i;
			return best;
		}

		void RestorePointerLocked()
		{
			if (!g_pointerApplied)
				return;
			::SystemParametersInfoA(SPI_SETMOUSESPEED, 0, reinterpret_cast<void*>(static_cast<std::intptr_t>(g_origSpeed)), SPIF_SENDCHANGE);
			::SystemParametersInfoA(SPI_SETMOUSE, 0, g_origMouse, SPIF_SENDCHANGE);
			g_pointerApplied = false;
			std::error_code ec;
			std::filesystem::remove(PointerMarkerPath(), ec);
			if (GetSettings().debugLog)
				logger::info("Windows pointer restored (speed {}, precision {})", g_origSpeed, g_origMouse[2]);
		}

		void RestorePointer()
		{
			std::lock_guard lock(g_pointerLock);
			RestorePointerLocked();
		}

		// The game often exits without running any cleanup (quitting from the main menu with
		// the cursor free, or a CTD). A small helper next to the DLL waits for this process to
		// end and restores the pointer from the marker file if it is still there. It is only
		// started when the user opted into a pointer-speed/precision override.
		std::filesystem::path HelperPath()
		{
			HMODULE self{};
			if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&HelperPath), &self))
				return {};
			std::wstring buf(MAX_PATH, L'\0');
			for (;;) {
				const DWORD n = ::GetModuleFileNameW(self, buf.data(), static_cast<DWORD>(buf.size()));
				if (n == 0)
					return {};
				if (n < buf.size()) {
					buf.resize(n);
					break;
				}
				buf.resize(buf.size() * 2);
			}
			return std::filesystem::path(buf).parent_path() / L"Free Menu Cursor" / L"Free Menu Cursor Restore.exe";
		}

		void StartPointerWatcher(const std::filesystem::path& a_marker)
		{
			static bool started = false;
			if (started)
				return;
			started = true;
			const auto helper = HelperPath();
			std::error_code ec;
			if (helper.empty() || !std::filesystem::exists(helper, ec)) {
				logger::warn("Pointer restore helper not found; a crash in a menu leaves the pointer changed until the next launch");
				return;
			}
			std::wstring cmd = std::format(L"\"{}\" {} \"{}\"", helper.wstring(), ::GetCurrentProcessId(), a_marker.wstring());
			STARTUPINFOW        si{ sizeof(si) };
			PROCESS_INFORMATION pi{};
			if (::CreateProcessW(helper.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
				::CloseHandle(pi.hThread);
				::CloseHandle(pi.hProcess);
				logger::info("Pointer restore helper started (pid {})", pi.dwProcessId);
			} else {
				logger::warn("Could not start the pointer restore helper ({}); a crash in a menu leaves the pointer changed until the next launch", ::GetLastError());
			}
		}

		void ApplyPointer()
		{
			const auto& s = GetSettings();
			const bool  speed = std::fabs(s.menuCursorSpeed - 1.0f) > 0.01f;
			const bool  prec = s.menuPointerPrecision >= 0;
			std::lock_guard lock(g_pointerLock);
			if (g_pointerApplied || (!speed && !prec))
				return;
			int orig = 10;
			int mouse[3]{};
			if (!::SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &orig, 0) || !::SystemParametersInfoA(SPI_GETMOUSE, 0, mouse, 0))
				return;
			g_origSpeed = orig;
			std::copy(std::begin(mouse), std::end(mouse), g_origMouse);
			if (auto path = PointerMarkerPath(); !path.empty()) {
				if (FILE* f = _wfopen(path.c_str(), L"w")) {
					std::fprintf(f, "%d %d %d %d\n", orig, mouse[0], mouse[1], mouse[2]);
					std::fclose(f);
				}
				StartPointerWatcher(path);
			}
			g_pointerApplied = true;
			const int newSpeed = speed ? ScaledSpeed(orig, s.menuCursorSpeed) : orig;
			if (speed)
				::SystemParametersInfoA(SPI_SETMOUSESPEED, 0, reinterpret_cast<void*>(static_cast<std::intptr_t>(newSpeed)), SPIF_SENDCHANGE);
			if (prec) {
				int m[3] = { mouse[0], mouse[1], s.menuPointerPrecision };
				::SystemParametersInfoA(SPI_SETMOUSE, 0, m, SPIF_SENDCHANGE);
			}
			if (s.debugLog)
				logger::info("Windows pointer for menus: speed {} -> {} (x{:.2f}), precision {} -> {}", orig, newSpeed, s.menuCursorSpeed,
					mouse[2], prec ? s.menuPointerPrecision : mouse[2]);
		}

		void Enter(Owner a_owner)
		{
			GetSettings().Load();  // re-read the INI each time a menu opens
			g_free = true;
			g_moveCount = 0;
			g_vanillaPending = false;
			g_flickPending = false;
			logger::info("Free cursor ON ({})", OwnerName(a_owner));
			LogMenuCursor("enter");
			RelaxMouse();
			Unclip();
			if (a_owner == Owner::kHotkey) {
				// No in-game cursor here: show the Windows arrow where the hidden pointer was,
				// moved to the window centre if it sat on an edge (the game recentres it).
				POINT p{};
				SIZE  client{};
				if (IsForeground() && ClientCursor(p, client) &&
					(p.x <= 0 || p.y <= 0 || p.x >= client.cx - 1 || p.y >= client.cy - 1)) {
					POINT c{ client.cx / 2, client.cy / 2 };
					if (::ClientToScreen(g_hwnd, &c))
						::SetCursorPos(c.x, c.y);
				}
			} else {
				if (GetSettings().alignOnOpen && IsForeground())
					AlignOSCursor(a_owner);
				if (IsForeground())
					ApplyPointer();
			}
			RequestShow(true);
		}

		void Leave()
		{
			g_free = false;
			logger::info("Free cursor OFF (vanilla gain {:.3f},{:.3f} / FLICK gain {:.3f},{:.3f})",
				g_vanillaGain.x, g_vanillaGain.y, g_flickGain.x, g_flickGain.y);
			RestoreMouse();
			RestorePointer();
			RequestShow(false);
			if (GetSettings().reclipOnClose && IsForeground())
				Reclip();
		}

		void Update()
		{
			if (!g_hwnd)
				return;
			if (g_settingsReloadRequested.exchange(false, std::memory_order_acq_rel)) {
				const Settings previous = GetSettings();
				GetSettings().Load();
				const auto& current = GetSettings();
				if (g_free && previous.relaxMouse != current.relaxMouse) {
					if (current.relaxMouse)
						RelaxMouse(true);
					else
						RestoreMouse();
				}
				if (g_free && (std::fabs(previous.menuCursorSpeed - current.menuCursorSpeed) > 0.001f ||
					previous.menuPointerPrecision != current.menuPointerPrecision)) {
					RestorePointer();
					if (!g_hotkeyMode && IsForeground())
						ApplyPointer();
				}
				logger::info("Settings reloaded from SKSE Menu Framework");
			}
			UpdateHotkey();
			const auto owner = DetectOwner();
			// Menus hand over to each other through a frame or two with no cursor owner
			// (FLICK -> Cursor Menu, closing one menu and opening the next). Leaving at once
			// would re-lock the cursor and the exclusive mouse for that instant.
			static int noneFrames = 0;
			noneFrames = owner == Owner::kNone ? noneFrames + 1 : 0;
			static Owner lastOwner = Owner::kNone;  // last non-none owner of this free period
			// The hotkey ends at once (release / click): no handover to wait for.
			const int grace = lastOwner == Owner::kHotkey ? 1 : kLeaveGraceFrames;
			if (owner != Owner::kNone && !g_free) {
				g_hotkeyMode = owner == Owner::kHotkey;
				Enter(owner);
			} else if (owner == Owner::kNone && g_free && noneFrames >= grace) {
				Leave();
				g_hotkeyMode = false;
			} else if (owner != g_owner && g_free && owner != Owner::kNone) {
				logger::info("Free cursor owner {} -> {}", OwnerName(g_owner), OwnerName(owner));
				g_flickPending = false;
				g_vanillaPending = false;
				const bool toHotkey = owner == Owner::kHotkey;
				g_hotkeyMode = toHotkey;
				if (toHotkey)
					RestorePointer();  // the menu pointer speed is for the in-game cursor only
				else if (IsForeground())
					ApplyPointer();
			}
			if (owner != Owner::kNone)
				lastOwner = owner;
			g_owner = owner;
			if (g_free && IsForeground()) {
				Unclip();
				if (g_reapplyMouse.exchange(false))
					RelaxMouse(true);  // focus came back: the game may have re-acquired exclusively
				else
					RelaxMouse();  // cheap unless the device object changed
			}
		}

		// ---- input rewriting ----------------------------------------------------------

		void PreVanilla(RE::MouseMoveEvent* a_move)
		{
			const int mode = GetSettings().vanillaSyncMode;
			auto      data = MenuCursorData();
			if (mode == 0 || !data)
				return;
			POINT p{};
			SIZE  client{};
			bool  outside = false;
			if (!ClientCursor(p, client, &outside))
				return;
			const auto space = MenuSpaceSize(*data);
			const Vec2 target{ (p.x + 0.5f) * space.x / client.cx, (p.y + 0.5f) * space.y / client.cy };
			const Vec2 raw{ static_cast<float>(a_move->mouseInputX), static_cast<float>(a_move->mouseInputY) };

			switch (mode) {
			case 1:  // direct
				data->cursorPosX = target.x;
				data->cursorPosY = target.y;
				a_move->mouseInputX = 0;
				a_move->mouseInputY = 0;
				break;
			case 2:  // compensate: pre-offset so the game's own delta lands on the target
				data->cursorPosX = target.x - raw.x * g_vanillaGain.x;
				data->cursorPosY = target.y - raw.y * g_vanillaGain.y;
				break;
			default:  // 3: post-write after the game processed the event
				break;
			}

			g_vanillaPending = true;
			g_vanillaPre = { data->cursorPosX, data->cursorPosY };
			g_vanillaTarget = target;
			g_vanillaSent = { static_cast<float>(a_move->mouseInputX), static_cast<float>(a_move->mouseInputY) };

			if (ShouldLogMove()) {
				logger::info("V#{} mode={} os=({},{}){} client={}x{} target=({:.1f},{:.1f}) raw=({},{}) sent=({},{}) pre=({:.1f},{:.1f})",
					g_moveCount, mode, p.x, p.y, outside ? " outside" : "", client.cx, client.cy, target.x, target.y,
					raw.x, raw.y, g_vanillaSent.x, g_vanillaSent.y, g_vanillaPre.x, g_vanillaPre.y);
			}
		}

		void PostVanilla()
		{
			if (!g_vanillaPending)
				return;
			g_vanillaPending = false;
			auto data = MenuCursorData();
			if (!data)
				return;
			const Vec2 after{ data->cursorPosX, data->cursorPosY };
			LearnGain(g_vanillaGain.x, after.x - g_vanillaPre.x, g_vanillaSent.x);
			LearnGain(g_vanillaGain.y, after.y - g_vanillaPre.y, g_vanillaSent.y);
			if (GetSettings().vanillaSyncMode == 3) {
				data->cursorPosX = g_vanillaTarget.x;
				data->cursorPosY = g_vanillaTarget.y;
			}
			if (GetSettings().debugLog && (g_moveCount <= 40 || g_moveCount % 120 == 0)) {
				logger::info("V#{} after=({:.1f},{:.1f}) gain=({:.3f},{:.3f}) err=({:.1f},{:.1f})",
					g_moveCount, after.x, after.y, g_vanillaGain.x, g_vanillaGain.y,
					g_vanillaTarget.x - after.x, g_vanillaTarget.y - after.y);
			}
		}

		void PreFlick(RE::MouseMoveEvent* a_move)
		{
			if (!GetSettings().flickSync || !g_flick || !g_flick->GetMousePos)
				return;
			POINT p{};
			SIZE  client{};
			bool  outside = false;
			if (!ClientCursor(p, client, &outside))
				return;
			const auto space = FlickDisplaySize(client);
			const Vec2 target{ (p.x + 0.5f) * space.x / client.cx, (p.y + 0.5f) * space.y / client.cy };

			Vec2 current{};
			g_flick->GetMousePos(&current.x, &current.y);
			if (!std::isfinite(current.x) || !std::isfinite(current.y) || current.x < -1.0e6f || current.y < -1.0e6f)
				return;  // ImGui has no valid mouse position yet

			if (g_flickPending) {
				// FLICK clamps its cursor to the display; samples at an edge say nothing about gain.
				const bool edgeX = current.x <= 0.5f || current.x >= space.x - 1.0f;
				const bool edgeY = current.y <= 0.5f || current.y >= space.y - 1.0f;
				if (!edgeX)
					LearnGain(g_flickGain.x, current.x - g_flickPrev.x, g_flickSent.x);
				if (!edgeY)
					LearnGain(g_flickGain.y, current.y - g_flickPrev.y, g_flickSent.y);
				// FLICK moves its cursor the same way as the mouse; a negative or tiny learned
				// gain is noise and would make the correction run away.
				g_flickGain.x = std::clamp(g_flickGain.x, 0.25f, 4.0f);
				g_flickGain.y = std::clamp(g_flickGain.y, 0.25f, 4.0f);
			}

			const Vec2 raw{ static_cast<float>(a_move->mouseInputX), static_cast<float>(a_move->mouseInputY) };
			const Vec2 err{ target.x - current.x, target.y - current.y };
			const auto toDelta = [](float a_err, float a_gain) {
				return static_cast<std::int32_t>(std::clamp(std::lround(a_err / a_gain), -4000L, 4000L));
			};
			a_move->mouseInputX = toDelta(err.x, g_flickGain.x);
			a_move->mouseInputY = toDelta(err.y, g_flickGain.y);

			g_flickPending = true;
			g_flickPrev = current;
			g_flickSent = { static_cast<float>(a_move->mouseInputX), static_cast<float>(a_move->mouseInputY) };

			if (ShouldLogMove()) {
				logger::info("F#{} os=({},{}){} display={:.0f}x{:.0f} target=({:.1f},{:.1f}) flick=({:.1f},{:.1f}) raw=({},{}) sent=({},{}) gain=({:.3f},{:.3f})",
					g_moveCount, p.x, p.y, outside ? " outside" : "", space.x, space.y, target.x, target.y,
					current.x, current.y, raw.x, raw.y, g_flickSent.x, g_flickSent.y, g_flickGain.x, g_flickGain.y);
			}
		}

		// Turns a button event into "nothing happened" (neither down, held nor up) without
		// unlinking it from the queue.
		void Neutralize(RE::ButtonEvent* a_button)
		{
			a_button->GetRuntimeData().value = 0.0f;
			a_button->GetRuntimeData().heldDownSecs = 0.0f;
		}

		void PreProcess(RE::InputEvent* const* a_events)
		{
			if (!a_events)
				return;
			const auto& s = GetSettings();
			const bool  hotkey = g_free && g_owner == Owner::kHotkey;
			const bool  dropMouse = hotkey || g_suppressMouseFrames > 0;
			if (g_suppressMouseFrames > 0)
				--g_suppressMouseFrames;

			bool moveHandled = false;
			for (auto event = *a_events; event; event = event->next) {
				if (auto button = event->AsButtonEvent()) {
					const auto device = button->GetDevice();
					if (device == RE::INPUT_DEVICE::kKeyboard && s.enabled && s.blockFreeCursorKey && s.freeCursorKey > 0 &&
						button->GetIDCode() == static_cast<std::uint32_t>(s.freeCursorKey))
						Neutralize(button);  // the free-cursor key itself never reaches the game
					else if (device == RE::INPUT_DEVICE::kMouse && dropMouse)
						Neutralize(button);  // no attacks / zoom while using the Windows cursor
					continue;
				}
				auto move = event->AsMouseMoveEvent();
				if (!move || moveHandled)
					continue;
				moveHandled = true;  // the queue holds at most one mouse move per frame
				if (hotkey) {
					move->mouseInputX = 0;  // the camera stays still
					move->mouseInputY = 0;
				} else if (g_free && IsForeground()) {
					if (g_owner == Owner::kVanilla)
						PreVanilla(move);
					else if (g_owner == Owner::kFlick)
						PreFlick(move);
				}
			}
		}

		// ---- hooks --------------------------------------------------------------------

		// Two hooks on the same call site. The inner one is installed at plugin load (other
		// plugins wrap it later, FLICK included). On its first call it installs the outer one,
		// which then wraps everyone: FLICK reads mouse deltas in its own hook, so steering its
		// cursor only works from outside it. Installing from inside the chain is safe because
		// this frame's dispatch has already passed the call instruction.
		using DispatchFn = void (*)(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
		std::atomic<bool> g_outerActive{};

		void Process(DispatchFn a_next, RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
		{
			Update();
			PreProcess(a_events);
			a_next(a_dispatcher, a_events);
			if (g_free && g_owner == Owner::kVanilla)
				PostVanilla();
		}

		struct OuterDispatchHook
		{
			static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
			{
				Process(func.get(), a_dispatcher, a_events);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		REL::Relocation<std::uintptr_t> DispatchCallSite()
		{
			return REL::Relocation<std::uintptr_t>{ REL::RelocationID(67315, 68617, 67315).address() + REL::Relocate(0x7B, 0x7B, 0x81) };
		}

		// Installed from a main-thread SKSE task queued at kDataLoaded, i.e. after every
		// plugin's kDataLoaded handler has run. It used to be installed from the input
		// thread on the first dispatch, which raced other plugins that patch the same call
		// site from their kDataLoaded handler on the main thread (Hotkey Atlas re-hooks on
		// top there): both did VirtualProtect/write/restore at once and one of them wrote
		// into a page the other had already made read-only again (CTD at load).
		void InstallOuterHook()
		{
			const auto site = DispatchCallSite().address();
			const auto code = reinterpret_cast<const std::uint8_t*>(site);
			if (code[0] != 0xE8) {
				logger::error("Input dispatch call site no longer holds a call (0x{:02X}); outer hook not installed", code[0]);
				return;
			}
			// Publish the current target before patching: the input thread may run the
			// outer thunk the moment the call is rewritten.
			std::int32_t disp;
			std::memcpy(&disp, code + 1, sizeof(disp));
			OuterDispatchHook::func = site + 5 + static_cast<std::intptr_t>(disp);
			g_outerActive = true;  // inner hook forwards from now on (at most one frame unprocessed)
			const auto prev = SKSE::GetTrampoline().write_call<5>(site, OuterDispatchHook::thunk);
			if (prev != OuterDispatchHook::func.address()) {
				logger::warn("Input dispatch target changed while patching; using the latest one");
				OuterDispatchHook::func = prev;
			}
			logger::info("Outer input dispatch hook installed on thread {} (wraps {})", ::GetCurrentThreadId(),
				ModuleOf(reinterpret_cast<const void*>(OuterDispatchHook::func.address())));
		}

		struct InputDispatchHook
		{
			static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
			{
				if (g_outerActive)
					func(a_dispatcher, a_events);  // the outer hook already did the work
				else
					Process(func.get(), a_dispatcher, a_events);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (g_msgShow && a_msg == g_msgShow) {
				ApplyShow(a_wParam != 0);
				return 0;
			}
			// Latched hotkey mode ends when the user clicks the game (or brings it back).
			if (g_hotkeyLatched && !g_hotkeyHeld) {
				const bool click = a_msg == WM_LBUTTONDOWN || a_msg == WM_RBUTTONDOWN || a_msg == WM_MBUTTONDOWN || a_msg == WM_XBUTTONDOWN;
				const bool activate = a_msg == WM_ACTIVATE && LOWORD(a_wParam) != WA_INACTIVE;
				if (click || activate)
					g_releaseLatch = true;
			}
			// Hide the OS cursor over the game only while the game is in front: in the background
			// the game neither moves nor draws its own cursor, so nothing would be visible.
			// In hotkey mode there is no in-game cursor at all, so the arrow stays visible.
			const bool hideHere = g_free && !g_hotkeyMode && GetSettings().hideOSCursorInsideWindow && ::GetForegroundWindow() == a_hwnd;
			if (hideHere && a_msg == WM_SETCURSOR && LOWORD(a_lParam) == HTCLIENT) {
				::SetCursor(nullptr);  // the game draws its own cursor inside the window
				return TRUE;
			}
			if (g_free && !hideHere && a_msg == WM_SETCURSOR && LOWORD(a_lParam) == HTCLIENT) {
				// In the background: show a normal arrow (the game's window class has none).
				// The game decrements the cursor show count when it loses focus, so lift it
				// back to >= 0 on this (the window's) thread; Leave() reverts every increment.
				int count = ::ShowCursor(TRUE);
				++g_showIncrements;
				while (count < 0 && g_showIncrements < 64) {
					count = ::ShowCursor(TRUE);
					++g_showIncrements;
				}
				if (count > 0) {  // was already visible: undo the probe
					::ShowCursor(FALSE);
					--g_showIncrements;
				}
				static const HCURSOR arrow = ::LoadCursorA(nullptr, IDC_ARROW);
				::SetCursor(arrow);
				static std::uint32_t logged = 0;
				if (GetSettings().debugLog && logged < 20) {
					++logged;
					logger::info("Background WM_SETCURSOR over game: show count {}, increments {}", count, g_showIncrements);
				}
				return TRUE;
			}
			if (a_msg == WM_CLOSE || a_msg == WM_DESTROY)
				RestorePointer();
			const auto result = ::CallWindowProcA(g_origProc, a_hwnd, a_msg, a_wParam, a_lParam);
			if (g_free) {
				switch (a_msg) {
				case WM_MOUSEMOVE:
					// after the chain, in case an ImGui backend set an arrow while handling it
					if (hideHere)
						::SetCursor(nullptr);
					break;
				case WM_SETFOCUS:
				case WM_ACTIVATE:
					if (a_msg == WM_ACTIVATE && LOWORD(a_wParam) == WA_INACTIVE)
						RestorePointer();  // other apps get the normal Windows pointer
					if (a_msg == WM_SETFOCUS || LOWORD(a_wParam) != WA_INACTIVE) {
						if (!g_hotkeyMode)
							ApplyPointer();
						g_reapplyMouse = true;
						if (GetSettings().debugLog)
							logger::info("Window reactivated while free (msg 0x{:X}, wParam 0x{:X})", a_msg, a_wParam);
					}
					::ClipCursor(nullptr);
					break;
				case WM_WINDOWPOSCHANGED:
				case WM_SIZING:
					::ClipCursor(nullptr);  // undo LockCursor (SSE Display Tweaks) re-clipping
					break;
				default:
					break;
				}
			}
			return result;
		}

		class MenuLogger : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuLogger* GetSingleton()
			{
				static MenuLogger singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && GetSettings().debugLog)
					logger::info("Menu '{}' {}", a_event->menuName.c_str(), a_event->opening ? "opened" : "closed");
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void RequestSettingsReload()
	{
		g_settingsReloadRequested.store(true, std::memory_order_release);
	}

	void RestorePointerFromMarker(const std::filesystem::path& path);

	void RestorePointerFromLastRun()
	{
		for (const auto& path : { PointerMarkerPath(), LegacyPointerMarkerPath() })
			RestorePointerFromMarker(path);
	}

	void RestorePointerFromMarker(const std::filesystem::path& path)
	{
		if (path.empty() || !std::filesystem::exists(path))
			return;
		int v[4]{};
		bool ok = false;
		if (FILE* f = _wfopen(path.c_str(), L"r")) {
			ok = std::fscanf(f, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4;
			std::fclose(f);
		}
		if (ok && v[0] >= 1 && v[0] <= 20) {
			::SystemParametersInfoA(SPI_SETMOUSESPEED, 0, reinterpret_cast<void*>(static_cast<std::intptr_t>(v[0])), SPIF_SENDCHANGE);
			int m[3] = { v[1], v[2], v[3] };
			::SystemParametersInfoA(SPI_SETMOUSE, 0, m, SPIF_SENDCHANGE);
			logger::warn("Restored Windows pointer left changed by the last run (speed {}, precision {})", v[0], v[3]);
		}
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	void InstallHooks()
	{
		auto& trampoline = SKSE::GetTrampoline();
		InputDispatchHook::func = trampoline.write_call<5>(
			REL::RelocationID(67315, 68617, 67315).address() + REL::Relocate(0x7B, 0x7B, 0x81),
			InputDispatchHook::thunk);
		logger::info("Input dispatch hook installed");
	}

	void OnDataLoaded()
	{
		GetSettings().Load();

		auto window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
		g_hwnd = window ? reinterpret_cast<HWND>(window->hWnd) : nullptr;
		if (!g_hwnd) {
			logger::error("Game window not found; plugin inactive");
			return;
		}
		g_windowThread = ::GetWindowThreadProcessId(g_hwnd, nullptr);
		g_msgShow = ::RegisterWindowMessageA("Free Menu Cursor.Show");
		g_origProc = reinterpret_cast<WNDPROC>(
			::SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
		logger::info("Window {:p} subclassed (window thread {}, this thread {}, unicode {})",
			static_cast<void*>(g_hwnd), g_windowThread, ::GetCurrentThreadId(), ::IsWindowUnicode(g_hwnd) != 0);

		if (OnWindowThread()) {
			const int count = ::ShowCursor(TRUE) - 1;
			::ShowCursor(FALSE);
			logger::info("Initial ShowCursor count {}", count);
		}

		if (auto module = ::GetModuleHandleW(L"FUCK.dll")) {
			auto request = reinterpret_cast<void* (*)()>(::GetProcAddress(module, "RequestFUCK"));
			auto iface = request ? static_cast<const FlickInterface*>(request()) : nullptr;
			const bool valid = iface && iface->version >= 5 &&
			                   InModule(module, reinterpret_cast<const void*>(iface->IsMenuOpen)) &&
			                   InModule(module, reinterpret_cast<const void*>(iface->GetMousePos)) &&
			                   InModule(module, reinterpret_cast<const void*>(iface->GetDisplaySize));
			if (valid) {
				g_flick = iface;
				logger::info("FLICK API v{} connected", iface->version);
			} else {
				logger::warn("FLICK API layout not recognised (version {}); FLICK support disabled", iface ? iface->version : 0);
			}
		}
		if (auto module = ::GetModuleHandleW(L"SKSEMenuFramework.dll")) {
			g_smfBlocking = reinterpret_cast<bool (*)()>(::GetProcAddress(module, "IsAnyBlockingWindowOpened"));
			logger::info("SKSE Menu Framework {}", g_smfBlocking ? "connected" : "found without IsAnyBlockingWindowOpened");
			RegisterSettingsMenu();
		}
		if (::GetModuleHandleW(L"SSEDisplayTweaks.dll"))
			logger::info("SSE Display Tweaks present (LockCursor is released while menus are open)");

		if (auto ui = RE::UI::GetSingleton())
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuLogger::GetSingleton());

		const auto& s = GetSettings();
		logger::info("Settings: enabled={} vanilla={} smf={} flick={} vanillaSync={} flickSync={} align={} reclip={} hideInside={} freeCursorKey=0x{:X} (VK 0x{:X}) blockKey={}",
			s.enabled, s.vanillaMenus, s.smf, s.flick, s.vanillaSyncMode, s.flickSync, s.alignOnOpen, s.reclipOnClose, s.hideOSCursorInsideWindow,
			s.freeCursorKey, HotkeyVK(s.freeCursorKey), s.blockFreeCursorKey);

		if (auto tasks = SKSE::GetTaskInterface())
			tasks->AddTask([] { InstallOuterHook(); });
		else
			logger::error("SKSE task interface unavailable; outer input hook not installed (FLICK steering disabled)");
	}
}
