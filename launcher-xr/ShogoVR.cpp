// ShogoVR.cpp  -  minimal launcher for WinlatorXR containers.
//
// Some containers do not handle Windows desktop shortcuts well, so this is a
// real EXE with no window: it finds Client.exe next to itself (or inside the
// overlaid ShogoVR folder sits next to Client.exe), launches the game with
// the same -rez list the GUI launcher uses, and waits for the game to exit.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {

std::wstring SelfDir()
{
	wchar_t buf[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (!n) return L"";
	std::wstring p(buf, n);
	size_t s = p.find_last_of(L"\\/");
	return (s == std::wstring::npos) ? L"" : p.substr(0, s + 1);
}

bool Exists(const std::wstring& path)
{
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The -rez list Client.exe needs: the base game archives (it refuses to start
// without them), the Custom folder, the map packs, then the VR mod folder last
// so its CShell.dll wins.  Same shape as launcher/Common.h's BuildGameArgs.
std::wstring BuildArgs(const std::wstring& dir)
{
	std::wstring args = L"-windowtitle Shogo";
	auto addRez = [&](const std::wstring& rel)
	{
		if (Exists(dir + rel))
		{
			if (!args.empty()) args += L" ";
			args += L"-rez \"" + rel + L"\"";
		}
	};

	addRez(L"SHOGO.REZ");
	addRez(L"SOUND.REZ");
	addRez(L"Custom");

	std::vector<std::wstring> packs;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"SHOGOP*.REZ").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do { packs.push_back(fd.cFileName); } while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	std::sort(packs.begin(), packs.end(),
		[](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
	for (auto& p : packs) addRez(p);

	args += L" +multiplayer 0";

	// The engine's default render mode is 640x480 unless told otherwise; the
	// ScreenWidth/ScreenHeight console variables seed it before the renderer
	// starts.  Use the desktop resolution so the window fills the display.
	int nScreenW = GetSystemMetrics(SM_CXSCREEN);
	int nScreenH = GetSystemMetrics(SM_CYSCREEN);
	if (nScreenW > 0 && nScreenH > 0)
	{
		args += L" +screenwidth " + std::to_wstring(nScreenW);
		args += L" +screenheight " + std::to_wstring(nScreenH);
	}

	addRez(L"ShogoVR");
	return args;
}

bool FindClientDir(const std::wstring& self, std::wstring& game, std::wstring& client)
{
	// The launcher lives either right next to Client.exe or inside the
	// overlaid ShogoVR folder that does; search ourselves, then our parents.
	std::wstring probe = self;
	for (int depth = 0; depth < 3; ++depth)
	{
		if (Exists(probe + L"Client.exe"))
		{
			game = probe;
			client = probe + L"Client.exe";
			return true;
		}
		probe += L"..\\";
	}
	return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	std::wstring game, client;
	if (!FindClientDir(SelfDir(), game, client)) return 1;

	std::wstring cmdLine = L"\"" + client + L"\" " + BuildArgs(game);
	std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back(L'\0');

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(client.c_str(), &cmdBuf[0], nullptr, nullptr, FALSE, 0, nullptr,
			game.c_str(), &si, &pi))
		return 1;

	CloseHandle(pi.hThread);
	DWORD code = 0;
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	return (int)code;
}