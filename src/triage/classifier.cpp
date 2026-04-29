#include "triage/classifier.h"
#include "triage/signatures.h"

#include "path_utils.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace triage {

const std::array<const char*, 5> kKnownJunkBasenames = {
    "Thumbs.db",
    "desktop.ini",
    ".DS_Store",
    ".localized",
    "Icon\r",   /* legacy macOS folder-icon stub; basename ends in CR (0x0D) */
};

const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::Useful:    return "Useful";
        case Verdict::Junk:      return "Junk";
        case Verdict::Duplicate: return "Duplicate";
        case Verdict::Unknown:   return "Unknown";
        case Verdict::Empty:     return "Empty";
        case Verdict::Error:     return "Error";
    }
    return "Unknown";
}

namespace {

/* ASCII case-insensitive equality. All reserved-device names and
 * known-junk basenames are pure ASCII so we don't need locale-aware
 * folding. */
bool EqualsAsciiCaseInsensitive(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return i == a.size() && b[i] == '\0';
}

/* Windows reserved device names (case-insensitive). Creating a folder
 * with one of these names succeeds on most filesystems but the OS
 * intercepts open() / CreateFile() afterwards and routes to the device,
 * which manifests as confusing "permission denied" / "invalid handle"
 * errors deep inside ExecuteMoves. Reject up front. */
const char* const kReservedDeviceNames[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM0", "COM1", "COM2", "COM3", "COM4",
    "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT0", "LPT1", "LPT2", "LPT3", "LPT4",
    "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

bool IsReservedDeviceName(const std::string& name) {
    /* Match the bare name OR <name>.<ext> — Windows treats "CON.txt"
     * the same as "CON" for the device routing. */
    std::string base = name;
    auto dot = base.find('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    for (const char* r : kReservedDeviceNames) {
        if (EqualsAsciiCaseInsensitive(base, r)) return true;
    }
    return false;
}

}  /* namespace */

std::string ValidateSubfolderName(const std::string& name) {
    if (name.empty()) return "must not be empty";
    if (name == ".")  return "\".\" is not allowed";
    if (name == "..") return "\"..\" is not allowed";

    /* Reject path-separator / drive-letter / null-byte characters. The
     * path-escape guard in move_actions is the second line of defense,
     * but each rule below cleanly maps to one observable reason — when
     * a user pastes "C:/Trash" we tell them ":", not the disjunction. */
    for (char c : name) {
        if (c == '/' || c == '\\') return "must not contain '/' or '\\'";
        if (c == ':')               return "must not contain ':'";
        if (c == '\0')              return "must not contain a null byte";
    }

    /* Windows reserved device names. Reject on every platform — a
     * cross-platform user staging a junk folder under "NUL" probably
     * didn't mean to ship something that explodes on Windows. The
     * "CON.txt" form is also caught (Windows treats it the same). */
    if (IsReservedDeviceName(name)) {
        return "reserved Windows device name (CON, NUL, COM*, LPT*, etc.)";
    }

    /* Trailing space or dot: Windows silently strips them when creating
     * the directory, so "junk " and "junk" become the same folder.
     * Surfacing the rejection here is friendlier than the surprise. */
    if (name.back() == ' ') return "must not end with a space (Windows strips it)";
    if (name.back() == '.') return "must not end with a dot (Windows strips it)";

    return "";  /* OK */
}

void ValidateConfig(const Config& cfg) {
    auto check = [](const char* field_label, const std::string& value) {
        const std::string reason = ValidateSubfolderName(value);
        if (!reason.empty()) {
            throw ConfigError(std::string("triage::Config: invalid ") +
                              field_label + " (\"" + value + "\"): " + reason);
        }
    };
    check("junk_subfolder",       cfg.junk_subfolder);
    check("review_subfolder",     cfg.review_subfolder);
    check("duplicates_subfolder", cfg.duplicates_subfolder);
}

bool IsKnownJunkBasename(const std::filesystem::path& p) {
    /* path::filename() returns just the last component. .string()
     * converts to the platform-native narrow string; for ASCII names
     * (which all kKnownJunkBasenames are, including the trailing CR
     * in "Icon\r"), this is byte-stable across platforms.
     *
     * Comparison goes through PlatformBasenameEquals so a Windows user
     * with "thumbs.db" (whatever app touched it last lowered the case)
     * still matches the canonical "Thumbs.db" in our table — NTFS treats
     * them as the same file. */
    const std::string name = p.filename().string();
    for (const char* known : kKnownJunkBasenames) {
        if (PlatformBasenameEquals(name, known)) return true;
    }
    return false;
}

namespace {

/* Read up to kSignaturePeekBytes from the start of `path` into `out`.
 * Returns the number of bytes actually read (may be less than requested
 * for short files). Returns 0 and sets *io_failed = true on open or
 * read failure (vanished file, permission denied). Never throws. */
std::size_t ReadHead(const std::filesystem::path& path,
                     std::uint8_t* out,
                     std::size_t cap,
                     bool* io_failed) {
    *io_failed = false;
#if defined(_WIN32)
    std::FILE* f = ::_wfopen(path.wstring().c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
#endif
    if (!f) {
        *io_failed = true;
        return 0;
    }
    const std::size_t got = std::fread(out, 1, cap, f);
    /* If got < cap and we hit ferror (not EOF), the file changed under
     * us. Promote to io_failed so the caller produces Verdict::Error
     * rather than "no signature matched on a 3-byte read of an unreadable
     * file." */
    if (got < cap && std::ferror(f)) *io_failed = true;
    std::fclose(f);
    return got;
}

}  /* namespace */

FileVerdict ClassifyFile(const std::filesystem::path& p,
                         const Config& cfg,
                         std::uint64_t known_size) {
    FileVerdict v;
    v.path = PathToGenericUtf8(p);  /* UTF-8, forward-slash separators */

    /* Resolve size: prefer the caller's value (free from directory_entry
     * during a directory walk); fall back to a stat. */
    std::uint64_t size = known_size;
    if (size == kSizeUnknown) {
        std::error_code ec;
        const auto sz = std::filesystem::file_size(p, ec);
        if (ec) {
            v.verdict = Verdict::Error;
            v.reason  = "stat failed: " + ec.message();
            return v;
        }
        size = static_cast<std::uint64_t>(sz);
    }
    v.size = size;

    /* Rule 1: known-junk basename — wins over Empty (rule 2) and
     * Duplicate (rule 3 — handled by scanner). The scanner still fills
     * dup_group / content_hash / dup_canonical for inspection, but the
     * final verdict stays Junk. */
    if (IsKnownJunkBasename(p)) {
        v.verdict = Verdict::Junk;
        v.reason  = "known-junk basename";
        return v;
    }

    /* Rule 2: zero-byte file. */
    if (size == 0) {
        v.verdict = Verdict::Empty;
        v.reason  = "empty file";
        return v;
    }

    /* Rule 4: signature lookup. Skipped if the user disabled
     * signatures — every file then falls through to the size-based
     * Junk/Unknown split. */
    if (cfg.enable_signatures) {
        std::array<std::uint8_t, kSignaturePeekBytes> head{};
        bool io_failed = false;
        const std::size_t cap = (size < kSignaturePeekBytes)
            ? static_cast<std::size_t>(size) : kSignaturePeekBytes;
        const std::size_t got = ReadHead(p, head.data(), cap, &io_failed);
        if (io_failed) {
            v.verdict = Verdict::Error;
            v.reason  = "I/O error reading file header";
            return v;
        }
        const SignatureMatch m = LookupSignature(head.data(), got);
        if (!m.signature_id.empty()) {
            v.signature_id = m.signature_id;
            v.verdict = Verdict::Useful;
            v.reason  = "signature: " + m.signature_id;
            return v;
        }
    }

    /* Rule 5: unmatched + small. */
    if (size <= cfg.junk_max_bytes) {
        v.verdict = Verdict::Junk;
        v.reason  = "unmatched, small (<= junk_max_bytes)";
        return v;
    }

    /* Rule 6: unmatched + larger. */
    v.verdict = Verdict::Unknown;
    v.reason  = "unmatched, above junk_max_bytes";
    return v;
}

}  /* namespace triage */
