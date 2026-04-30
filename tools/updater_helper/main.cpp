/* hxediter-updater-helper
 *
 * Waits for the parent hxediter to exit (so NSIS can delete the installed
 * binary), then launches the NSIS installer with UAC elevation.
 *
 * Argv:
 *   [1] absolute path to HxEditer-X.Y.Z-win64.exe inside %TEMP%
 *   [2] parent hxediter PID (decimal)
 *
 * The installer path is strictly validated: it must live under %TEMP% and
 * match the HxEditer-*-win64.exe naming. Without this, anything that can
 * spawn the helper could get an arbitrary exe elevated via the "runas"
 * verb below.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#include <cstdlib>
#include <cstdio>
#include <string>

static bool StartsWithCI(const wchar_t* s, const wchar_t* prefix) {
    while (*prefix) {
        wchar_t a = *s++, b = *prefix++;
        if (a >= L'A' && a <= L'Z') a = wchar_t(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = wchar_t(b - L'A' + L'a');
        if (a != b) return false;
    }
    return true;
}


static std::wstring LocalAppDataDir() {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring dir = base;
    dir += L"\\HxEditer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static void WriteFailureLog(const wchar_t* msg, INT_PTR code) {
    std::wstring dir = LocalAppDataDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\updater.log";
    FILE* fp = _wfopen(path.c_str(), L"wb");
    if (!fp) return;
    wchar_t line[256];
    swprintf(line, 256, L"runas launch failed: %s (code %lld)\r\n",
             msg ? msg : L"unknown", (long long)code);
    fputws(line, fp);
    fclose(fp);
}

static void ClearFailureLog() {
    std::wstring dir = LocalAppDataDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\updater.log";
    DeleteFileW(path.c_str());
}

static bool InstallerPathLooksSafe(LPCWSTR path) {
    if (!path || !*path) return false;

    wchar_t full[MAX_PATH];
    DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) return false;

    wchar_t temp[MAX_PATH];
    DWORD tn = GetTempPathW(MAX_PATH, temp);
    if (tn == 0 || tn >= MAX_PATH) return false;

    if (!StartsWithCI(full, temp)) return false;

    const wchar_t* name = wcsrchr(full, L'\\');
    name = name ? name + 1 : full;
    if (!StartsWithCI(name, L"HxEditer-")) return false;

    const wchar_t* tail = wcsstr(name, L"-win64.exe");
    if (!tail) return false;
    if (wcslen(tail) != wcslen(L"-win64.exe")) return false;

    DWORD attrs = GetFileAttributesW(full);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return false;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return false;

    return true;
}

/* WinMain (narrow) so MinGW's default crt0 finds the entry point without
 * -municode. Args are still read from GetCommandLineW, so encoding is
 * Unicode-correct. */
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) {
        if (argv) LocalFree(argv);
        return 1;
    }

    LPCWSTR installer_path = argv[1];
    DWORD   parent_pid     = (DWORD)_wtoi(argv[2]);

    if (!InstallerPathLooksSafe(installer_path)) {
        LocalFree(argv);
        return 1;
    }

    if (parent_pid != 0) {
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
        if (parent) {
            WaitForSingleObject(parent, 30000);
            CloseHandle(parent);
        }
    }

    /* Give the OS a moment to release the handle on hxediter.exe so NSIS's
     * uninstaller can delete it. */
    Sleep(500);

    HINSTANCE r = ShellExecuteW(nullptr, L"runas", installer_path,
                                nullptr, nullptr, SW_SHOWNORMAL);

    LocalFree(argv);

    if ((INT_PTR)r <= 32) {
        WriteFailureLog(L"ShellExecuteW(runas)", (INT_PTR)r);
        return 2;
    }

    ClearFailureLog();
    return 0;
}
