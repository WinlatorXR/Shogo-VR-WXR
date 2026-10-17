// ShogoVR.cpp  -  minimal launcher for WinlatorXR containers.
//
// Some containers do not handle Windows desktop shortcuts well, so this is a
// real EXE with no window: it finds Client.exe next to itself (or inside the
// overlaid ShogoVR folder sits next to Client.exe), launches the game with
// the same -rez list the GUI launcher uses, and waits for the game to exit.
//
// Before launching it also checks the GitHub release channel for a newer mod
// build and offers to install it (see TryUpdate).  The check is best-effort:
// offline, or any download/copy failure, just falls through to a normal launch.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <wchar.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

// The stable-release channel the updater reads (see Docs/PROJECT_PLAN.md, s. 7).
const wchar_t* const kVersionUrl =
	L"https://raw.githubusercontent.com/WinlatorXR/Shogo-VR-WXR/refs/heads/main/AutoUpdate/version.txt";
const wchar_t* const kNotesUrl =
	L"https://raw.githubusercontent.com/WinlatorXR/Shogo-VR-WXR/refs/heads/main/AutoUpdate/notes.txt";
const wchar_t* const kDllUrl =
	L"https://github.com/WinlatorXR/Shogo-VR-WXR/raw/refs/heads/main/AutoUpdate/CShell.dll";

// A real CShell.dll is ~700 KB; anything tiny is an error page or a partial
// download, so refuse to install it.
const unsigned long long kMinDllBytes = 32768;

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

unsigned long long FileSize(const std::wstring& path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
	return ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

// --- tiny helpers ----------------------------------------------------------

std::wstring Widen(const std::string& s)
{
	return std::wstring(s.begin(), s.end());
}

// Trailing CR/LF and surrounding spaces are not part of a version number.
std::string Trim(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && (unsigned char)s[b] <= ' ') ++b;
	while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
	return s.substr(b, e - b);
}

bool ReadFileBytes(const std::wstring& path, std::string& out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;

	char buf[4096];
	DWORD got = 0;
	out.clear();
	while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0)
		out.append(buf, got);
	CloseHandle(h);
	return true;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;

	DWORD wrote = 0;
	bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &wrote, nullptr) != 0
		&& wrote == data.size();
	CloseHandle(h);
	if (!ok) DeleteFileW(path.c_str());
	return ok;
}

// Delete a folder's *contents* but keep the folder itself.
void DeleteTree(const std::wstring& dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;

	do
	{
		std::wstring name = fd.cFileName;
		if (name == L"." || name == L"..") continue;

		std::wstring full = dir + name;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			DeleteTree(full + L"\\");
			RemoveDirectoryW(full.c_str());
		}
		else
		{
			SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileW(full.c_str());
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

// Append one line to ShogoVR_update.log next to the EXE (never fatal).
void Log(const std::wstring& exeDir, const std::wstring& msg)
{
	HANDLE h = CreateFileW((exeDir + L"ShogoVR_update.log").c_str(), FILE_APPEND_DATA,
		FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return;

	SYSTEMTIME st;
	GetLocalTime(&st);
	wchar_t stamp[64];
	swprintf_s(stamp, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
	std::wstring line = std::wstring(stamp) + msg + L"\r\n";

	int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
	std::string utf8(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &utf8[0], n, nullptr, nullptr);

	DWORD wrote = 0;
	WriteFile(h, utf8.data(), (DWORD)utf8.size(), &wrote, nullptr);
	CloseHandle(h);
}

// --- HTTPS -----------------------------------------------------------------

// One HTTPS/HTTP GET (redirects are followed by WinHTTP).  The body goes to
// hFile when it is a real handle, otherwise into `text`.  Plain text results
// are what the version/notes files are, so this is enough for both cases.
bool HttpGet(const std::wstring& url, HANDLE hFile, std::string& text, std::wstring& err)
{
	const std::wstring httpsPrefix = L"https://";
	const std::wstring httpPrefix = L"http://";
	bool secure = true;
	std::wstring rest;

	if (url.compare(0, httpsPrefix.size(), httpsPrefix) == 0) rest = url.substr(httpsPrefix.size());
	else if (url.compare(0, httpPrefix.size(), httpPrefix) == 0) { secure = false; rest = url.substr(httpPrefix.size()); }
	else { err = L"unsupported URL"; return false; }

	size_t slash = rest.find(L'/');
	std::wstring host = (slash == std::wstring::npos) ? rest : rest.substr(0, slash);
	std::wstring path = (slash == std::wstring::npos) ? L"/" : rest.substr(slash);
	if (host.empty()) { err = L"no host in the URL"; return false; }

	// NO_PROXY: a container has no system proxy, and asking for one (WPAD) can
	// stall for a long time when the headset has no network.
	HINTERNET hSession = WinHttpOpen(L"ShogoVR-Updater/1.0",
		WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) { err = L"WinHttpOpen failed"; return false; }
	WinHttpSetTimeouts(hSession, 5000, 5000, 15000, 20000);

	bool ok = false;
	HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
		secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
	if (!hConnect) { err = L"WinHttpConnect failed"; }
	else
	{
		HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
		if (!hRequest) err = L"WinHttpOpenRequest failed";
		else
		{
			if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
					WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
				&& WinHttpReceiveResponse(hRequest, nullptr))
			{
				DWORD status = 0, len = sizeof(status);
				WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
				if (status != 200)
					err = L"HTTP " + std::to_wstring(status);
				else
				{
					ok = true;
					for (;;)
					{
						DWORD avail = 0;
						if (!WinHttpQueryDataAvailable(hRequest, &avail)) { ok = false; err = L"read failed"; break; }
						if (!avail) break;

						std::vector<char> chunk(avail);
						DWORD got = 0;
						if (!WinHttpReadData(hRequest, &chunk[0], avail, &got)) { ok = false; err = L"read failed"; break; }
						if (!got) break;

						if (hFile != INVALID_HANDLE_VALUE)
						{
							DWORD wrote = 0;
							if (!WriteFile(hFile, &chunk[0], got, &wrote, nullptr) || wrote != got)
							{
								ok = false; err = L"write failed"; break;
							}
						}
						else text.append(&chunk[0], got);
					}
				}
			}
			else err = L"request failed";
			WinHttpCloseHandle(hRequest);
		}
		WinHttpCloseHandle(hConnect);
	}
	WinHttpCloseHandle(hSession);
	return ok;
}

bool DownloadToFile(const std::wstring& url, const std::wstring& path, const std::wstring& exeDir)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) { Log(exeDir, L"download failed: cannot create " + path); return false; }

	std::string unused;
	std::wstring err;
	bool ok = HttpGet(url, h, unused, err);
	CloseHandle(h);
	if (!ok)
	{
		Log(exeDir, L"download failed: " + err);
		DeleteFileW(path.c_str());
	}
	return ok;
}

// --- the update check ------------------------------------------------------

void TryUpdate(const std::wstring& exeDir, const std::wstring& gameDir)
{
	Log(exeDir, L"update check: start");

	std::string online;
	std::wstring err;
	if (!HttpGet(kVersionUrl, INVALID_HANDLE_VALUE, online, err))
	{
		Log(exeDir, L"update check skipped (" + err + L")");
		return;						// offline / unreachable - just play
	}

	online = Trim(online);
	if (online.empty()) { Log(exeDir, L"update check skipped (version.txt online is empty)"); return; }

	const std::wstring versionPath = exeDir + L"version.txt";
	std::string local;
	bool haveLocal = ReadFileBytes(versionPath, local);
	local = Trim(local);

	Log(exeDir, L"versions: online=" + Widen(online) + L" local=" + (haveLocal ? Widen(local) : L"(none)"));
	if (haveLocal && local == online) return;	// already current

	std::string notes;
	if (!HttpGet(kNotesUrl, INVALID_HANDLE_VALUE, notes, err)) notes.clear();
	notes = Trim(notes);

	std::wstring prompt = L"A new update is available, download now?\r\nVersion " + Widen(online);
	if (!notes.empty()) prompt += L": " + Widen(notes);

	if (MessageBoxW(nullptr, prompt.c_str(), L"Shogo VR",
			MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST) != IDYES)
	{
		Log(exeDir, L"update: declined by the player");
		return;
	}

	// Stage both files first: the install below only starts once they are
	// complete, so a dropped connection can never leave a half-updated mod.
	const std::wstring stageDir = exeDir + L"ShogoVRUpdate\\";
	CreateDirectoryW(stageDir.c_str(), nullptr);
	DeleteTree(stageDir);			// reuse whatever an earlier run left here

	const std::wstring stageVersion = stageDir + L"version.txt";
	const std::wstring stageDll = stageDir + L"CShell.dll";
	if (!DownloadToFile(kVersionUrl, stageVersion, exeDir)
		|| !DownloadToFile(kDllUrl, stageDll, exeDir))
	{
		Log(exeDir, L"update: download failed, the staged files are kept for the next launch");
		return;
	}

	unsigned long long dllSize = FileSize(stageDll);
	if (dllSize < kMinDllBytes)
	{
		Log(exeDir, L"update: downloaded CShell.dll is only " + std::to_wstring(dllSize) + L" bytes, ignored");
		return;
	}

	// Install: version.txt next to the EXE, then CShell.dll into the ShogoVR
	// overlay that wins over the copy inside SHOGO.REZ (see BuildArgs).
	const std::wstring overlayDll = gameDir + L"ShogoVR\\CShell.dll";
	bool okVersion = CopyFileW(stageVersion.c_str(), versionPath.c_str(), FALSE) != 0;
	bool okDll = okVersion && CopyFileW(stageDll.c_str(), overlayDll.c_str(), FALSE) != 0;

	if (okVersion && okDll)
	{
		DeleteTree(stageDir);		// keep the folder itself, drop the files
		Log(exeDir, L"update: installed " + Widen(online) + L" (CShell.dll " + std::to_wstring(dllSize) + L" bytes)");
		return;
	}

	// Only one of the two went in.  Put the old version.txt back so the update
	// is offered again next launch instead of silently sticking forever.
	if (okVersion)
	{
		if (haveLocal) WriteFileBytes(versionPath, local);
		else DeleteFileW(versionPath.c_str());
	}
	Log(exeDir, L"update: copy failed (version.txt=" + std::wstring(okVersion ? L"ok" : L"failed")
		+ L", CShell.dll=" + std::wstring(okDll ? L"ok" : L"failed") + L"), will be offered again");
}

// --- launching -------------------------------------------------------------

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
	std::wstring self = SelfDir();
	std::wstring game, client;
	if (!FindClientDir(self, game, client)) return 1;

	TryUpdate(self, game);

	std::wstring cmdLine = L"\"" + client + L"\" " + BuildArgs(game);
	std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back(L'\0');

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(client.c_str(), &cmdBuf[0], nullptr, nullptr, FALSE, 0, nullptr,
			game.c_str(), &si, &pi))
	{
		Log(self, L"could not start the game (Client.exe)");
		return 1;
	}

	CloseHandle(pi.hThread);
	DWORD code = 0;
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	return (int)code;
}
