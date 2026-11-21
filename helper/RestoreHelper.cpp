// Free Menu Cursor pointer restore helper.
//
// Started by Free Menu Cursor.dll only when the user has enabled a menu pointer-speed or
// pointer-precision override. It waits for the game process to exit and, if the plugin did
// not get the chance to undo its temporary change (crash, fast exit), restores the Windows
// pointer settings recorded in the marker file, then deletes that file.
//
// Usage: "Free Menu Cursor Restore.exe" <game pid> <marker file>
// The marker holds four integers: pointer speed (1..20) and the three SPI_GETMOUSE values.
// Changes are never persisted (no SPIF_UPDATEINIFILE), matching the plugin.

#include <Windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cwchar>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int    argc = 0;
	LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if (!argv || argc != 3)
		return 2;

	const DWORD pid = static_cast<DWORD>(std::wcstoul(argv[1], nullptr, 10));
	const wchar_t* marker = argv[2];

	if (HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid)) {
		::WaitForSingleObject(process, INFINITE);
		::CloseHandle(process);
	}

	int v[4]{};
	bool ok = false;
	if (FILE* f = _wfopen(marker, L"r")) {
		ok = std::fscanf(f, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4;
		std::fclose(f);
	} else {
		::LocalFree(argv);
		return 0;  // the plugin already restored the pointer and removed the marker
	}

	if (ok && v[0] >= 1 && v[0] <= 20) {
		::SystemParametersInfoW(SPI_SETMOUSESPEED, 0, reinterpret_cast<void*>(static_cast<INT_PTR>(v[0])), SPIF_SENDCHANGE);
		int mouse[3] = { v[1], v[2], v[3] };
		::SystemParametersInfoW(SPI_SETMOUSE, 0, mouse, SPIF_SENDCHANGE);
	}
	::DeleteFileW(marker);
	::LocalFree(argv);
	return 0;
}
