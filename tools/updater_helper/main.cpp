/* hxediter-updater-helper
 *
 * Waits for the parent hxediter to exit (so NSIS can delete the installed
 * binary), then launches the NSIS installer with UAC elevation, then waits
 * for the installer subprocess and reports its exit code.
 *
 * Argv:
 *   [1] absolute path to HxEditer-X.Y.Z-win64.exe inside %TEMP%
 *   [2] parent hxediter PID (decimal)
 *
 * The installer path is strictly validated: it must live under %TEMP% and
 * match the HxEditer-*-win64.exe naming. Without this, anything that can
 * spawn the helper could get an arbitrary exe elevated via the "runas"
 * verb below.
 *
 * On any failure (UAC declined, NSIS aborted, AV killed the installer),
 * writes a one-shot UTF-8 marker at
 * %LOCALAPPDATA%\HxEditer\last_update_failure.txt; the main app consumes
 * it on the next startup and surfaces the message in Settings -> Updates.
 *
 * Every step also writes a line to %LOCALAPPDATA%\HxEditer\update_debug.log
 * so the next failure is diagnosable from a single file without rebuilding.
 *
 * Exit codes:
 *   0  installer ran and exited 0
 *   1  bad args / installer path rejected by safety check
 *   2  ShellExecute failed to launch (UAC declined, file missing, etc.)
 *   3  installer subprocess exited non-zero (NSIS error, user cancel, AV kill)
 */

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <fstream>
#include <string>
#include <shlobj.h>

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
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        if (path) CoTaskMemFree(path);
        return L"";
    }
    std::wstring dir = path;
    CoTaskMemFree(path);
    dir += L"\\HxEditer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static void WriteFailureLog(const char* msg, INT_PTR code) {
    std::wstring dir = LocalAppDataDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\last_update_failure.txt";
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return; /* Best-effort marker file. */
    out << "runas launch failed: " << (msg ? msg : "unknown")
        << " (code " << (long long)code << ")";
}

static void ClearFailureLog() {
    std::wstring dir = LocalAppDataDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\last_update_failure.txt";
    DeleteFileW(path.c_str());
}

static void DebugLog(const char* fmt, ...) {
    std::wstring dir = LocalAppDataDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\update_debug.log";

    SYSTEMTIME st;
    GetSystemTime(&st);
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);

    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    std::ofstream out(path.c_str(), std::ios::binary | std::ios::app);
    if (!out) return;
    out << ts << " [helper]  " << body << "\r\n";
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
    DebugLog("WinMain entry argc=%d", argc);
    if (!argv || argc < 3) {
        WriteFailureLog("bad args (argc < 3)", argc);
        DebugLog("exit 1: bad args");
        if (argv) LocalFree(argv);
        return 1;
    }

    LPCWSTR installer_path = argv[1];
    DWORD   parent_pid     = (DWORD)_wtoi(argv[2]);

    /* Convert installer_path to UTF-8 once for diagnostic logging; reused
     * by the safety-check failure marker below. */
    std::string installer_utf8;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, installer_path, -1,
                                    nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            installer_utf8.resize(n - 1);
            WideCharToMultiByte(CP_UTF8, 0, installer_path, -1,
                                installer_utf8.data(), n, nullptr, nullptr);
        }
    }
    DebugLog("argv[1]=\"%s\" parent_pid=%lu",
             installer_utf8.c_str(), (unsigned long)parent_pid);

    if (!InstallerPathLooksSafe(installer_path)) {
        std::string msg = "installer path rejected by safety check: " + installer_utf8;
        WriteFailureLog(msg.c_str(), 0);
        DebugLog("exit 1: %s", msg.c_str());
        LocalFree(argv);
        return 1;
    }
    DebugLog("InstallerPathLooksSafe -> ok");

    if (parent_pid != 0) {
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
        if (parent) {
            DWORD wait = WaitForSingleObject(parent, 30000);
            DebugLog("parent wait returned %lu (0=signaled, 0x102=timeout)",
                     (unsigned long)wait);
            CloseHandle(parent);
        } else {
            DebugLog("OpenProcess(parent_pid=%lu) returned NULL (already exited?)",
                     (unsigned long)parent_pid);
        }
    }

    /* Give the OS a moment to release the handle on hxediter.exe so NSIS's
     * uninstaller can delete it. AV scanners can hold the image open after
     * the process exits; 1500ms is conservative for typical hosts. */
    Sleep(1500);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize      = sizeof(sei);
    sei.fMask       = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb      = L"runas";
    sei.lpFile      = installer_path;
    /* /S puts NSIS in silent mode: skips the "uninstall first?" prompt,
     * skips the wizard, auto-confirms everything. The user already
     * consented by clicking "Install and restart" + accepting UAC; a
     * third confirmation dialog is noise. Failures still surface via
     * the installer's exit code (caught below). */
    sei.lpParameters = L"/S";
    sei.nShow       = SW_HIDE;

    DebugLog("ShellExecuteExW begin verb=runas");
    BOOL ok = ShellExecuteExW(&sei);
    DWORD launch_err = ok ? 0 : GetLastError();
    DebugLog("ShellExecuteExW ret=%d hInstApp=%lld lastErr=%lu hProcess=%p",
             ok, (long long)(INT_PTR)sei.hInstApp,
             (unsigned long)launch_err, (void*)sei.hProcess);

    LocalFree(argv);

    if (!ok || (INT_PTR)sei.hInstApp <= 32) {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "ShellExecuteExW(runas) failed (hInstApp=%lld, GetLastError=%lu)",
            (long long)(INT_PTR)sei.hInstApp, (unsigned long)launch_err);
        WriteFailureLog(msg, (INT_PTR)sei.hInstApp);
        DebugLog("exit 2: %s", msg);
        return 2;
    }

    /* SEE_MASK_NOCLOSEPROCESS does not always yield a process handle (rare
     * DDE-resolved verbs). Without one we have nothing to wait on, so trust
     * the launch and clear the marker. */
    if (!sei.hProcess) {
        DebugLog("ShellExecuteExW ok but hProcess == NULL; assuming success");
        ClearFailureLog();
        return 0;
    }

    /* NSIS installers are interactive — the user may sit on the wizard for
     * minutes. INFINITE is correct: this helper exists solely to wait on
     * the elevated child. */
    DebugLog("waiting for installer pid=%lu",
             (unsigned long)GetProcessId(sei.hProcess));
    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(sei.hProcess, &exit_code)) {
        exit_code = (DWORD)-1;
    }
    CloseHandle(sei.hProcess);
    DebugLog("installer exited code=%lu", (unsigned long)exit_code);

    if (exit_code != 0) {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "Installer exited with code %lu (NSIS error, user cancel, or AV kill)",
            (unsigned long)exit_code);
        WriteFailureLog(msg, (INT_PTR)exit_code);
        DebugLog("exit 3: %s", msg);
        return 3;
    }

    ClearFailureLog();
    DebugLog("exit 0: success");
    return 0;
}
