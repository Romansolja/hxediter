/* xxhash single-header pattern: XXH_INLINE_ALL turns every API symbol
 * into static inline within this TU. hasher.cpp is the only consumer,
 * so there's no ODR risk and no need for the separate-implementation
 * unit pattern. */
#define XXH_INLINE_ALL
#include "xxhash.h"

#include "triage/hasher.h"

#include <array>
#include <cstdio>
#include <system_error>

namespace triage {

namespace {

/* Streaming chunk size for the read loop. 64 KiB matches kHashHeadBytes
 * so a head-only hash is exactly one read; larger files use multiple
 * reads but never load more than this in memory at once. */
constexpr std::size_t kReadChunk = 64 * 1024;

}  /* namespace */

HashResult HashFile(const std::filesystem::path& path, std::uint64_t max_bytes) {
    HashResult out;

    /* Use C stdio directly so we get a portable binary read on every
     * platform; std::ifstream's binary mode has historical quirks on
     * MinGW with paths that include non-ASCII bytes — fopen with the
     * UTF-8 string the path holds works reliably here. */
#if defined(_WIN32)
    /* Windows wide-fopen for Unicode paths. */
    std::FILE* f = ::_wfopen(path.wstring().c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
#endif
    if (!f) return out;  /* ok stays false */

    XXH3_state_t* st = XXH3_createState();
    if (!st) {
        std::fclose(f);
        return out;
    }
    if (XXH3_64bits_reset(st) == XXH_ERROR) {
        XXH3_freeState(st);
        std::fclose(f);
        return out;
    }

    std::array<std::uint8_t, kReadChunk> buf{};
    std::uint64_t remaining = max_bytes;  /* may be kHashFullFile (UINT64_MAX) */
    std::uint64_t total = 0;

    while (remaining > 0) {
        const std::size_t want =
            (remaining > kReadChunk) ? kReadChunk
                                     : static_cast<std::size_t>(remaining);
        const std::size_t got = std::fread(buf.data(), 1, want, f);

        if (got > 0) {
            if (XXH3_64bits_update(st, buf.data(), got) == XXH_ERROR) {
                XXH3_freeState(st);
                std::fclose(f);
                return out;  /* ok stays false */
            }
            total += got;
        }

        /* A short read is either EOF or an error mid-stream. Distinguish
         * via ferror: if it's set, this is a transient I/O failure (flaky
         * disk, removable media yanked, network share dropped) and we
         * MUST NOT report a hash for the prefix we managed to read —
         * doing so would let dup detection group a partial-read file
         * with whatever happens to share its prefix bytes, and the user
         * would agree to "move duplicates" of files we never finished
         * reading. Fail loud here so the scan classifies the file as
         * Error rather than silently mis-grouping it.
         *
         * EOF (got < want, no ferror) is the normal way to finish a
         * file smaller than `remaining` — fall through, return success. */
        if (got < want) {
            if (std::ferror(f)) {
                XXH3_freeState(st);
                std::fclose(f);
                return out;  /* ok stays false */
            }
            break;  /* EOF */
        }
        remaining -= got;
    }

    out.hash         = XXH3_64bits_digest(st);
    out.bytes_hashed = total;
    out.ok           = true;

    XXH3_freeState(st);
    std::fclose(f);
    return out;
}

}  /* namespace triage */
