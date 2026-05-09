/* hxediter-updater-helper
 *
 * Waits for the parent hxediter to exit (so NSIS can delete the installed
 * binary), then launches the NSIS installer with UAC elevation, then waits
 * for the installer subprocess and reports its exit code.
 *
 * Argv:
 *   [1] absolute path to HxEditer-X.Y.Z-win64.exe inside %TEMP%
 *   [2] parent hxediter PID (decimal)
 *   [3] expected lowercase-hex SHA256 of the installer (64 chars)
 *   [4] (optional) absolute path to the currently-running hxediter.exe,
 *       used as a fallback for the post-install relaunch when the
 *       registry lookup that NSIS populates ($INSTDIR under
 *       Software\Romansolja\HxEditer) is unavailable. Older parent
 *       binaries that don't pass this still work — the helper prefers
 *       the registry value anyway.
 *
 * The installer path is strictly validated: it must live under %TEMP% and
 * match the HxEditer-*-win64.exe naming. Without this, anything that can
 * spawn the helper could get an arbitrary exe elevated via the "runas"
 * verb below.
 *
 * The expected SHA256 is also re-verified against the file's contents
 * here in the helper, immediately before ShellExecute. The parent
 * already verified the download — but the parent runs unprivileged and
 * the file lives in user-writable %TEMP%, so an attacker running as the
 * user could swap the file between the parent's check and the UAC
 * prompt, getting arbitrary code elevated under the user's consent.
 * The recompute eliminates that TOCTOU window.
 *
 * On any failure (UAC declined, NSIS aborted, AV killed the installer),
 * writes a one-shot UTF-8 marker at
 * %LOCALAPPDATA%\HxEditer\last_update_failure.txt; the main app consumes
 * it on the next startup and surfaces the message in Settings -> Updates.
 * The same marker is also used when the install succeeds but the
 * post-install auto-relaunch can't reach a runnable editor (resolution
 * failures DON'T write a marker — those are diagnose-from-the-log bugs,
 * not user-actionable conditions; only ShellExecute failures do).
 *
 * Every step also writes a line to %LOCALAPPDATA%\HxEditer\update_debug.log
 * so the next failure is diagnosable from a single file without rebuilding.
 *
 * Exit codes:
 *   0  installer ran and exited 0
 *   1  bad args / installer path rejected by safety check / SHA mismatch
 *   2  ShellExecute failed to launch (UAC declined, file missing, etc.)
 *   3  installer subprocess exited non-zero (NSIS error, user cancel, AV kill)
 */

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <shlobj.h>

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

/* RegGetValueW view-selector flags. Documented since Windows 8; the
 * helper targets _WIN32_WINNT 0x0601 (Win 7) for ABI breadth, so define
 * them here when missing. Values are stable across SDK versions.
 *
 * Why both: NSIS itself is a 32-bit installer. On 64-bit Windows its
 * registry writes land in HKLM\Software\WOW6432Node\... via WoW64
 * redirection — NOT the native 64-bit view. A 64-bit helper that reads
 * with WOW6464KEY only will miss everything NSIS just wrote. We probe
 * the 32-bit view first (today's reality) and fall back to 64-bit so
 * the code is invariant if NSIS ever ships a 64-bit installer. */
#ifndef RRF_SUBKEY_WOW6464KEY
#  define RRF_SUBKEY_WOW6464KEY 0x00010000
#endif
#ifndef RRF_SUBKEY_WOW6432KEY
#  define RRF_SUBKEY_WOW6432KEY 0x00020000
#endif

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

    /* Atomic-append via single-shot WriteFile under FILE_APPEND_DATA.
     * The helper writes to this file concurrently with the main app
     * (which is the entire point of the helper); std::ofstream in
     * append mode does not guarantee write atomicity, so two writers
     * could otherwise interleave inside one line. NTFS guarantees
     * atomic append for writes shorter than the volume sector size. */
    char line[1280];
    int  ln = std::snprintf(line, sizeof(line), "%s [helper]  %s\r\n", ts, body);
    if (ln <= 0) return;
    /* On truncation, snprintf NUL-terminates at sizeof(line)-1; clamp
     * to that so we never write the trailing NUL byte into the log. */
    if (ln >= (int)sizeof(line)) ln = (int)sizeof(line) - 1;

    HANDLE h = CreateFileW(path.c_str(),
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(h, line, (DWORD)ln, &wrote, nullptr);
    CloseHandle(h);
}

static std::string HexLowerLocal(const uint8_t* data, size_t n) {
    static const char hex[] = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) {
        s[2*i]     = hex[data[i] >> 4];
        s[2*i + 1] = hex[data[i] & 0xF];
    }
    return s;
}

/* Compute SHA256 of `path` as lowercase hex. Empty string on any I/O or
 * BCrypt failure — caller treats that as "could not verify" and aborts
 * before ShellExecute. Mirrors updater.cpp::ComputeSha256 but standalone
 * to keep the helper a single translation unit. */
static std::string ComputeSha256Local(LPCWSTR path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return std::string();
    }
    DWORD hash_len = 0; ULONG rb = 0;
    if (!NT_SUCCESS(BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                                       (PUCHAR)&hash_len, sizeof(hash_len), &rb, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return std::string();
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!NT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return std::string();
    }
    /* Open with FILE_SHARE_READ only — explicitly DON'T share write. If
     * another process has the file open for write right now, this fails
     * and we abort, which is the right call: a writer mid-stream means
     * we cannot trust the bytes we're about to elevate. */
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return std::string();
    }
    std::vector<uint8_t> buf(64 * 1024);
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr)) { ok = false; break; }
        if (got == 0) break;
        if (!NT_SUCCESS(BCryptHashData(hash, buf.data(), got, 0))) { ok = false; break; }
    }
    CloseHandle(f);
    std::string out;
    if (ok) {
        std::vector<uint8_t> digest(hash_len);
        if (NT_SUCCESS(BCryptFinishHash(hash, digest.data(),
                                        (ULONG)digest.size(), 0))) {
            out = HexLowerLocal(digest.data(), digest.size());
        }
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

/* Constant-time comparison of two hex strings of equal length. Avoids
 * the early-exit tell of memcmp on a path that an attacker has any
 * influence over; cheap insurance for a 64-char compare. */
static bool ConstantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char acc = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ai = (unsigned char)a[i];
        unsigned char bi = (unsigned char)b[i];
        if (ai >= 'A' && ai <= 'Z') ai = (unsigned char)(ai - 'A' + 'a');
        if (bi >= 'A' && bi <= 'Z') bi = (unsigned char)(bi - 'A' + 'a');
        acc |= (unsigned char)(ai ^ bi);
    }
    return acc == 0;
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

/* Strip a trailing '\' from `s`. NSIS doesn't normally write one into
 * $INSTDIR but the registry contract doesn't promise its absence. */
static void StripTrailingSlash(std::wstring& s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) {
        s.pop_back();
    }
}

/* Case-insensitively check whether `s` ends with `suffix`. Used to strip
 * \Uninstall.exe from the UninstallString registry value — NSIS produces
 * title-case but a hand-edited registry could be anything. */
static bool EndsWithCI(const std::wstring& s, const wchar_t* suffix) {
    size_t sl = wcslen(suffix);
    if (s.size() < sl) return false;
    for (size_t i = 0; i < sl; ++i) {
        wchar_t a = s[s.size() - sl + i];
        wchar_t b = suffix[i];
        if (a >= L'A' && a <= L'Z') a = wchar_t(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = wchar_t(b - L'A' + L'a');
        if (a != b) return false;
    }
    return true;
}

/* Single-view read of a REG_SZ value into `out`. Uses RegGetValueW, which
 * validates the value type and ensures NUL-termination. `view_flag` must
 * be RRF_SUBKEY_WOW6432KEY or RRF_SUBKEY_WOW6464KEY. Returns true on
 * success and a non-empty string. */
static bool ReadRegSZView(HKEY hive, LPCWSTR subkey, LPCWSTR value,
                          DWORD view_flag, std::wstring& out) {
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS s = RegGetValueW(hive, subkey, value,
                             RRF_RT_REG_SZ | view_flag,
                             &type, nullptr, &bytes);
    if (s != ERROR_SUCCESS || bytes == 0 || (bytes % sizeof(wchar_t)) != 0) {
        return false;
    }
    std::wstring buf(bytes / sizeof(wchar_t), L'\0');
    s = RegGetValueW(hive, subkey, value,
                     RRF_RT_REG_SZ | view_flag,
                     nullptr, buf.data(), &bytes);
    if (s != ERROR_SUCCESS) return false;
    /* RegGetValueW NUL-terminates; trim trailing NULs from the size-
     * derived length so we don't carry embedded zeros downstream. */
    while (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    if (buf.empty()) return false;
    out = std::move(buf);
    return true;
}

/* Read a REG_SZ value, trying both registry views. WoW6432Node first
 * because that's where today's 32-bit NSIS installer lands its writes
 * on 64-bit Windows; native 64-bit second so the code keeps working if
 * NSIS ever ships a 64-bit installer. The previous single-view read
 * (WOW6464KEY only) silently failed every probe in production — see the
 * polyfill comment block above for the full reasoning. */
static bool ReadRegSZ(HKEY hive, LPCWSTR subkey, LPCWSTR value,
                      std::wstring& out) {
    return ReadRegSZView(hive, subkey, value, RRF_SUBKEY_WOW6432KEY, out)
        || ReadRegSZView(hive, subkey, value, RRF_SUBKEY_WOW6464KEY, out);
}

/* True if `path` is a regular file (exists, not a directory, not a
 * reparse point). Used to gate registry-resolved paths before we hand
 * them to ShellExecuteW. */
static bool IsRegularFile(LPCWSTR path) {
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return false;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    return true;
}

/* Resolve the installed hxediter.exe by reading where the NSIS installer
 * just wrote $INSTDIR. Probe order:
 *
 *   1. HKLM Software\Romansolja\HxEditer (default value) — the explicit
 *      install-folder write at NSIS project.nsi:664. SHCTX resolves to
 *      HKLM today (RequestExecutionLevel admin + no SetShellVarContext
 *      current). If a future installer flips to per-user this is HKCU,
 *      hence the secondary probe below.
 *   2. HKCU Software\Romansolja\HxEditer (default value) — same key
 *      under the user hive. Currently never written; defensive against
 *      a future per-user installer.
 *   3. HKLM/HKCU Uninstall\HxEditer\UninstallString — strip enclosing
 *      quotes, strip trailing \Uninstall.exe (case-insensitively), use
 *      the parent dir.
 *   4. The `fallback` hint (argv[4] from the parent app), if it's a
 *      regular file.
 *
 * Returns the empty string when nothing yields a runnable file. */
static std::wstring ResolveInstalledExe(LPCWSTR fallback) {
    auto try_dir = [](std::wstring dir) -> std::wstring {
        StripTrailingSlash(dir);
        if (dir.empty()) return L"";
        std::wstring exe = dir + L"\\hxediter.exe";
        return IsRegularFile(exe.c_str()) ? exe : std::wstring();
    };

    /* Each successful return logs `relaunch path source=<probe>`. The
     * argv[4] code path can't be exercised by an in-app update test (the
     * NSIS installer always rewrites the registry mid-flight, so by the
     * time we resolve the registry probes succeed and argv[4] is never
     * consulted). The source tag is the only signal that argv[4] ever
     * fired in production — keep it concise so log greps stay simple. */

    /* (1) and (2): direct $INSTDIR value under both hives. */
    std::wstring v;
    if (ReadRegSZ(HKEY_LOCAL_MACHINE,
                  L"Software\\Romansolja\\HxEditer", L"", v)) {
        std::wstring p = try_dir(v);
        if (!p.empty()) {
            DebugLog("relaunch path source=hklm-romansolja");
            return p;
        }
    }
    if (ReadRegSZ(HKEY_CURRENT_USER,
                  L"Software\\Romansolja\\HxEditer", L"", v)) {
        std::wstring p = try_dir(v);
        if (!p.empty()) {
            DebugLog("relaunch path source=hkcu-romansolja");
            return p;
        }
    }

    /* (3): UninstallString, both hives. Strip enclosing quotes (NSIS
     * writes `"<INSTDIR>\Uninstall.exe"`), then strip the
     * \Uninstall.exe tail to get the install dir. */
    auto strip_uninstall = [](std::wstring s) -> std::wstring {
        if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
            s = s.substr(1, s.size() - 2);
        }
        if (EndsWithCI(s, L"\\Uninstall.exe")) {
            s.resize(s.size() - wcslen(L"\\Uninstall.exe"));
        }
        return s;
    };
    static const wchar_t kUninstSub[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\HxEditer";
    if (ReadRegSZ(HKEY_LOCAL_MACHINE, kUninstSub, L"UninstallString", v)) {
        std::wstring p = try_dir(strip_uninstall(v));
        if (!p.empty()) {
            DebugLog("relaunch path source=hklm-uninstall");
            return p;
        }
    }
    if (ReadRegSZ(HKEY_CURRENT_USER, kUninstSub, L"UninstallString", v)) {
        std::wstring p = try_dir(strip_uninstall(v));
        if (!p.empty()) {
            DebugLog("relaunch path source=hkcu-uninstall");
            return p;
        }
    }

    /* (4): argv[4] hint as the recovery fallback. */
    if (fallback && *fallback && IsRegularFile(fallback)) {
        DebugLog("relaunch path source=argv4-fallback");
        return std::wstring(fallback);
    }
    DebugLog("relaunch path source=none (all probes failed)");
    return std::wstring();
}

/* One-shot validator for the resolved relaunch path. Hard-fails ONLY
 * malformed-input cases (empty, GetFullPathNameW failure, doesn't end
 * .exe). Existence and AV-quarantine are NOT checked here; those are
 * recoverable through the ShellExecuteW retry loop and reading them
 * here would conflate transient with permanent failure. The validator
 * exists for failure clarity in update_debug.log, not security — the
 * helper is medium-IL by this point and can't escalate.
 *
 * The MAX_PATH buffer is intentional: the default install root
 * ($PROGRAMFILES64\HxEditer\hxediter.exe) is well under the limit. If
 * long-path support is ever a goal, widen to a heap buffer with
 * GetFullPathNameW's two-call sizing pattern; today, a >MAX_PATH
 * resolved path is likely an attack or a misconfiguration and a
 * silent reject is the right outcome. */
static bool RelaunchPathLooksSafe(LPCWSTR path) {
    if (!path || !*path) return false;

    wchar_t full[MAX_PATH];
    DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) return false;

    /* Case-insensitive .exe suffix check. */
    size_t len = wcslen(full);
    if (len < 4) return false;
    const wchar_t* tail = full + (len - 4);
    if (!((tail[0] == L'.') &&
          (tail[1] == L'e' || tail[1] == L'E') &&
          (tail[2] == L'x' || tail[2] == L'X') &&
          (tail[3] == L'e' || tail[3] == L'E'))) {
        return false;
    }
    return true;
}

/* Closed list of ShellExecuteW failure codes that warrant a retry.
 * Returning hInstApp is documented as "an SE_ERR_* value or one of the
 * Windows error codes" with the boundary at 32 (>32 == success). The
 * AV-hold case for a freshly-written exe almost always surfaces as 26
 * (SE_ERR_SHARE); occasionally as 5 (SE_ERR_ACCESSDENIED). Anything
 * else is permanent — file-not-found, bad-format, no-association — and
 * retrying just delays the failure. */
static bool IsTransientShellError(INT_PTR hInstApp) {
    return hInstApp == SE_ERR_SHARE ||
           hInstApp == SE_ERR_ACCESSDENIED;
}

/* WinMain (narrow) so MinGW's default crt0 finds the entry point without
 * -municode. Args are still read from GetCommandLineW, so encoding is
 * Unicode-correct. */
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DebugLog("WinMain entry argc=%d", argc);
    /* argv[3] (expected SHA256) became required when the TOCTOU
     * recheck was added. Older parent binaries that pass only argc==3
     * are rejected by design — they would otherwise be missing the
     * integrity guard that closes the elevation race. */
    if (!argv || argc < 4) {
        WriteFailureLog("bad args (argc < 4, missing expected sha256)", argc);
        DebugLog("exit 1: bad args (argc=%d)", argc);
        if (argv) LocalFree(argv);
        return 1;
    }

    LPCWSTR installer_path = argv[1];
    DWORD   parent_pid     = (DWORD)_wtoi(argv[2]);
    LPCWSTR expected_sha_w = argv[3];

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

    /* argv[4] (optional): fallback hxediter.exe path for the post-install
     * relaunch. Older parent binaries don't pass it; that's fine — the
     * helper prefers the registry value NSIS just wrote, and only consults
     * argv[4] if all registry probes come up empty. Copy into a wstring
     * up front because argv is LocalFree'd before the success-path
     * relaunch code runs, so a raw pointer would dangle. */
    std::wstring relaunch_fallback;
    if (argc >= 5 && argv[4] && *argv[4]) {
        relaunch_fallback = argv[4];
        std::string u8;
        int rn = WideCharToMultiByte(CP_UTF8, 0, argv[4], -1,
                                     nullptr, 0, nullptr, nullptr);
        if (rn > 1) {
            u8.resize(rn - 1);
            WideCharToMultiByte(CP_UTF8, 0, argv[4], -1,
                                u8.data(), rn, nullptr, nullptr);
        }
        DebugLog("argv[4]=\"%s\" (relaunch fallback hint)", u8.c_str());
    } else {
        DebugLog("argv[4] absent — relaunch will use registry only");
    }

    if (!InstallerPathLooksSafe(installer_path)) {
        std::string msg = "installer path rejected by safety check: " + installer_utf8;
        WriteFailureLog(msg.c_str(), 0);
        DebugLog("exit 1: %s", msg.c_str());
        LocalFree(argv);
        return 1;
    }
    DebugLog("InstallerPathLooksSafe -> ok");

    /* Convert expected SHA256 to ASCII once for the post-wait recheck. */
    std::string expected_sha;
    expected_sha.reserve(64);
    for (int i = 0; expected_sha_w[i]; ++i) {
        wchar_t c = expected_sha_w[i];
        if (c > 0x7F) { expected_sha.clear(); break; }
        expected_sha.push_back(static_cast<char>(c));
    }
    if (expected_sha.size() != 64) {
        std::string msg = "expected sha256 is not 64 hex chars";
        WriteFailureLog(msg.c_str(), (INT_PTR)expected_sha.size());
        DebugLog("exit 1: %s (got len=%zu)", msg.c_str(), expected_sha.size());
        LocalFree(argv);
        return 1;
    }

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

    /* TOCTOU close-out: re-hash the file at `installer_path` immediately
     * before elevation. The parent already verified the download, but
     * %TEMP% is user-writable, and the safety check + Sleep above gave
     * a process running as the user a window to swap the file. If the
     * recompute mismatches the expected SHA256, abort — letting an
     * unverified payload reach UAC under the legitimate publisher's
     * displayed metadata is exactly the elevation-of-privilege bug. */
    std::string got_sha = ComputeSha256Local(installer_path);
    if (got_sha.empty()) {
        std::string msg = "could not hash installer (file vanished or locked): "
                          + installer_utf8;
        WriteFailureLog(msg.c_str(), 0);
        DebugLog("exit 1: %s", msg.c_str());
        LocalFree(argv);
        return 1;
    }
    if (!ConstantTimeEqual(got_sha, expected_sha)) {
        std::string msg = "installer SHA256 mismatch immediately before launch (TOCTOU?). "
                          "Expected " + expected_sha + " got " + got_sha;
        WriteFailureLog(msg.c_str(), 0);
        DebugLog("exit 1: %s", msg.c_str());
        DeleteFileW(installer_path);  /* drop the swapped file */
        LocalFree(argv);
        return 1;
    }
    DebugLog("sha256 recheck ok");

    /* Residual TOCTOU note: between ComputeSha256Local closing its
     * FILE_SHARE_READ handle and ShellExecuteExW reopening the file
     * by name to launch it, a process running as the user could
     * still swap %TEMP%\HxEditer-X.Y.Z-win64.exe. The window is
     * microseconds — bounded by the kernel's inter-syscall latency
     * — and the swap requires both the timing and the privileges of
     * a process already running as the same user. Closing it fully
     * would require something like `LoadLibraryEx + CreateProcess
     * with the existing handle`, but `runas` (the UAC verb) takes a
     * path, not a handle. Documenting honestly: this is a narrowed
     * window, not a closed one. SECURITY.md spells the same thing
     * out for users. */
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

    /* Installer exited 0. Resolve the installed exe and attempt relaunch.
     * The helper is medium-IL (only the runas child was elevated); a
     * ShellExecuteW from here puts the editor in the user's desktop session
     * at the correct integrity level — no inadvertent admin rights. */
    std::wstring exe = ResolveInstalledExe(
        relaunch_fallback.empty() ? nullptr : relaunch_fallback.c_str());

    bool relaunch_ok = true;
    if (exe.empty()) {
        DebugLog("relaunch skipped: no install path resolved "
                 "(registry empty and argv[4] fallback unusable)");
        /* Update succeeded; only auto-relaunch is unavailable. Don't
         * surface to the user — they can launch from Start menu. */
    } else if (!RelaunchPathLooksSafe(exe.c_str())) {
        DebugLog("relaunch skipped: resolved path failed safety check");
        /* Same: don't write a marker. A path being malformed is a bug to
         * diagnose from update_debug.log, not a user-actionable error. */
    } else {
        /* Derive lpDirectory by stripping the filename — without it the
         * relaunched editor inherits the helper's CWD (%TEMP%, since the
         * helper was launched from there), making first file-open
         * dialogs default to %TEMP%. */
        std::wstring exe_dir = exe;
        size_t slash = exe_dir.find_last_of(L'\\');
        if (slash != std::wstring::npos) exe_dir.resize(slash);

        /* UTF-8 just for the log line. */
        std::string exe_utf8;
        int u8 = WideCharToMultiByte(CP_UTF8, 0, exe.c_str(), -1,
                                      nullptr, 0, nullptr, nullptr);
        if (u8 > 1) {
            exe_utf8.resize(u8 - 1);
            WideCharToMultiByte(CP_UTF8, 0, exe.c_str(), -1,
                                exe_utf8.data(), u8, nullptr, nullptr);
        }

        DebugLog("relaunch begin path=\"%s\"", exe_utf8.c_str());

        /* Retry loop on transient ShellExecute failures — sharing
         * violation / access denied is the AV scan-on-write signature
         * for a freshly-written exe. Backoff total ≈ 3.85s worst case
         * (100+250+500+1000+2000 ms). Each backoff is gated on a real
         * IsTransientShellError code; permanent failures (file-not-
         * found, bad-format, no-association) bail immediately.
         *
         * TODO: single-instance mutex. If the user manually opens
         * HxEditer via Start menu while we're mid-install, this
         * relaunch produces a second instance. Today (no relaunch)
         * the user opens it themselves after — duplicate instances
         * are a new failure mode introduced here. Track separately. */
        static const DWORD backoff_ms[] = {100, 250, 500, 1000, 2000};
        INT_PTR last = 0;
        bool launched = false;
        for (size_t i = 0;
             i < sizeof(backoff_ms) / sizeof(backoff_ms[0]); ++i) {
            HINSTANCE rh = ShellExecuteW(nullptr, L"open", exe.c_str(),
                                          nullptr, exe_dir.c_str(),
                                          SW_SHOWNORMAL);
            last = (INT_PTR)rh;
            DebugLog("relaunch attempt=%zu hInstApp=%lld",
                     i, (long long)last);
            if (last > 32) { launched = true; break; }
            if (!IsTransientShellError(last)) break;
            Sleep(backoff_ms[i]);
        }

        if (!launched) {
            char msg[200];
            std::snprintf(msg, sizeof(msg),
                "Update installed but auto-relaunch failed (code %lld). "
                "Open HxEditer from the Start menu.", (long long)last);
            WriteFailureLog(msg, last);
            relaunch_ok = false;
        }
    }

    /* ClearFailureLog only on relaunch success. The shell returning >32
     * means it accepted the launch, but if the freshly-installed editor
     * then crashes during its own startup we have no signal — observability
     * blind spot acknowledged in SECURITY.md. The exit code remains 0:
     * the install itself succeeded; relaunch is a UX bonus, not a
     * correctness gate. */
    if (relaunch_ok) ClearFailureLog();
    DebugLog("exit 0: success (relaunch=%s)",
             exe.empty() ? "skipped" : (relaunch_ok ? "ok" : "failed"));
    return 0;
}
