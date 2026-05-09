# Security model

This document describes the threat model for HxEditer and the design
decisions behind its update path. It is meant for users evaluating
whether to enable the in-app updater on a managed system, and for
contributors who want to understand the trust assumptions before
proposing changes.

## Reporting a vulnerability

Please open a GitHub issue for non-sensitive findings. For anything
that could be weaponised before a release lands, contact the
maintainer privately via the email on the GitHub profile page so we
can coordinate a fix and a release before public disclosure.

## What HxEditer does and does not protect

HxEditer is a single-user, local hex editor for Windows. It does not
run a network listener, does not collect telemetry, and does not load
remote content into the editor view. The only outbound network traffic
is the auto-updater, described below.

The editor opens user-selected files with shared read+write access so
external tools can keep saving over the same file. Writes go through
short-lived write handles; the long-lived read handle never holds
write access. Concurrent external writes are detected via an
mtime+size token and surfaced in a conflict dialog. None of this is a
defence against malicious files — opening an attacker-supplied file
is no more dangerous than opening it in Notepad, but no less either.

## Auto-updater trust model

The in-app updater is the single most attack-relevant component, and
its trust model is **intentionally narrow**:

1. **HTTPS to api.github.com.** The session pins TLS 1.2 (and 1.3
   where available) and disables HTTPS-to-HTTP redirects. The pin is
   *enforced* — if the WinHTTP option call fails, the session is
   discarded rather than silently falling back to the OS default
   (which on legacy Windows 10 hosts can include TLS 1.0).
2. **GitHub Releases as the source of authority.** The updater fetches
   the latest release JSON, picks the asset matching
   `HxEditer-*-win64.exe` (first match wins, deterministic), and reads
   `SHA256SUMS.txt` from the same release. The installer is hashed
   after download and compared against the SHA256SUMS entry; any
   mismatch aborts and discards the file.
3. **TOCTOU recheck before elevation.** The updater runs unprivileged
   and the installer lives in user-writable `%TEMP%`. Between the
   parent's hash verify and the UAC prompt, a process running as the
   user could swap the file. The elevated helper recomputes the SHA256
   under a `FILE_SHARE_READ`-only handle immediately before
   `ShellExecute("runas")` and aborts on mismatch.

### What this defends against

* A network attacker on the user's wire who can intercept HTTPS but
  cannot break TLS 1.2.
* A man-in-the-middle who can swap the SHA256SUMS file (mitigated by
  the same TLS that protects the asset URL — both manifests transit
  the same pinned channel).
* A local attacker running as the same user who can write to `%TEMP%`
  and tries to swap the staged installer between download and UAC.

### What this does not defend against

* **Compromise of the release-publishing GitHub account.** SHA256SUMS
  is uploaded by the maintainer alongside the installer. Anyone with
  push or release-asset-write access to the repo can produce a
  matching pair. This is the realistic threat model — the README
  markets the auto-updater as a feature and the user clicks "Install
  and restart" with a single confirmation. **Until releases are
  signed with a code-signing certificate (or sigstore / a transparent
  log), an account takeover or stolen OAuth token can ship arbitrary
  code to every user on the next update tick.**
* A privileged attacker on the local machine. If they can write to
  `%PROGRAMFILES%\HxEditer`, they don't need the updater.
* Supply-chain attacks on the build toolchain (MinGW, FetchContent
  upstreams). A vendored, version-pinned dep would help here; we
  currently fetch `glfw 3.4`, `imgui v1.91.8`, and `nlohmann/json
  v3.11.3` by tag, which is name-pinned but not commit-pinned.

### Mitigations under consideration

* **Authenticode signing.** Single largest defence; would let users
  verify the publisher independently of GitHub. Cost is the
  certificate.
* **Manual update default.** The current updater auto-checks every
  6 hours and surfaces "Install and restart" as a one-click prompt.
  A "notify only, never download" mode would shrink the blast radius
  of a release-account compromise to "users who chose to upgrade in
  the relevant window" instead of "every active user".
* **Detached signature on SHA256SUMS** verifiable against an
  out-of-band public key shipped with the installed app (similar to
  how Linux distros sign their `Release` files). Would survive a
  release-asset compromise as long as the signing key isn't on the
  same machine.

## Per-component notes

### `updater.cpp`
* User agent: `hxediter/<version>`. No additional headers leak local
  state.
* JSON response capped at 4 MiB; installer download capped at
  200 MiB. Caps exist so a malicious or misbehaving server cannot
  exhaust memory or disk before the SHA check would have rejected the
  payload anyway.
* Installer filename is mirrored from the GitHub release asset name
  (`HxEditer-X.Y.Z-win64.exe`) to keep the on-disk file recognisable.
  The TOCTOU recheck does not depend on the filename.

### `updater_helper`
* Validates that the installer path is under `%TEMP%` and matches
  `HxEditer-*-win64.exe`. Without this check, anything that can spawn
  the helper could get an arbitrary `.exe` elevated via the `runas`
  verb.
* Re-hashes the file under `FILE_SHARE_READ` only — explicitly does
  not share write — so a writer mid-stream causes the hash to fail
  rather than producing a partial-bytes hash.
* Constant-time SHA256 comparison. Cheap insurance for a 64-char
  compare.
* **Post-install relaunch.** After the installer exits 0, the helper
  attempts to reopen the installed editor at medium integrity. The
  helper itself is unelevated by this point (only the NSIS child was
  elevated via `runas`), so `ShellExecuteW` from here lands the
  editor in the user's normal desktop session — no privilege is
  conferred on the launched process.
  The relaunch path is resolved from the registry the NSIS installer
  writes (`Software\Romansolja\HxEditer` default value, with
  `Uninstall\HxEditer\UninstallString` as a fallback), probed under
  HKLM and HKCU. The argv[4] hint passed by the parent app is a
  last-resort recovery fallback only; the running editor's location
  is not trusted as authoritative.
  **Observability caveat:** the helper considers the relaunch
  successful when `ShellExecuteW` returns `> 32`. The shell only
  confirms it *started* the process — if the freshly-installed
  editor then crashes during its own startup, that failure is not
  surfaced via the `last_update_failure.txt` marker (today's
  no-relaunch behavior would have surfaced it the moment the user
  clicked the Start-menu icon). A startup-breadcrumb mechanism is
  tracked as a follow-up.

### Audit log
The triage feature writes a per-batch audit log in JSON Lines under
`<root>/<junk_subfolder>/triage-log-<ts>.jsonl`. This is not security-
sensitive (it just records `src -> dst` for moves the user already
authorised), but it is structured rather than pipe-delimited so paths
containing `|` round-trip cleanly.
