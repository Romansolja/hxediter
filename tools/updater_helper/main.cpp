/* hxediter-updater-helper
 *
 * Tiny companion executable that runs after hxediter spawns it (from a
 * %TEMP% copy of itself), waits for the parent hxediter process to exit
 * so NSIS can delete the installed binary, then launches the NSIS
 * installer with UAC elevation.
 *
 * Argv:
 *   [1] absolute path to the NSIS installer (HxEditer-X.Y.Z-win64.exe)
 *   [2] parent hxediter PID (decimal)
 *
 * Exit codes:
 *   0 — installer spawned successfully
 *   1 — bad args
 *   2 — ShellExecute failed (likely UAC declined)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#include <cstdlib>

/* Use WinMain (narrow) rather than wWinMain so MinGW's default crt0 finds
 * the entry point without needing -municode. Command-line args are read
 * from GetCommandLineW so argument encoding is still Unicode-correct. */
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) {
        if (argv) LocalFree(argv);
        return 1;
    }

    LPCWSTR installer_path = argv[1];
    DWORD   parent_pid     = (DWORD)_wtoi(argv[2]);

    /* Open the parent for synchronization so we can wait for it to exit.
     * If it's already gone, OpenProcess returns NULL — that's fine; we
     * proceed straight to the installer launch. */
    if (parent_pid != 0) {
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
        if (parent) {
            WaitForSingleObject(parent, 30000);  /* up to 30s */
            CloseHandle(parent);
        }
    }

    /* Small extra guard so the OS has released the file handle on the
     * old hxediter.exe before NSIS's uninstaller tries to delete it. */
    Sleep(500);

    /* "runas" verb raises the UAC prompt so the NSIS installer (which
     * requires admin per RequestExecutionLevel) can run. */
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", installer_path,
                                nullptr, nullptr, SW_SHOWNORMAL);

    LocalFree(argv);

    /* ShellExecute returns an HINSTANCE where values <= 32 are errors.
     * Most common: SE_ERR_ACCESSDENIED (5) when the user declines UAC. */
    if ((INT_PTR)r <= 32) return 2;
    return 0;
}
