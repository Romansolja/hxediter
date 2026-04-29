#pragma once

/* triage::hasher — content-hash helper for duplicate detection.
 *
 * Backed by xxh3-64 (vendored at third_party/xxhash). Non-cryptographic;
 * we're not defending against adversarial collisions, only catching
 * accidental duplicates among benign files. Combined with the
 * scanner's same-size grouping, the false-positive rate is in the
 * 2^-64 ballpark per pair, which is well below filesystem corruption
 * rates over the same data. */

#include <cstdint>
#include <filesystem>

namespace triage {

struct HashResult {
    std::uint64_t hash = 0;          /* xxh3-64; 0 means !ok or genuine 0      */
    std::uint64_t bytes_hashed = 0;  /* what was actually fed in               */
    bool          ok = false;        /* false on I/O error or vanished file    */
};

/* Hash up to max_bytes of `path` from offset 0. If max_bytes is greater
 * than the file size, the whole file is hashed. Pass kHashFullFile to
 * mean "hash the whole file regardless of size."
 *
 * I/O failures (file vanished mid-scan, permission flipped) yield
 * ok == false, hash == 0, bytes_hashed == 0. Never throws. */
constexpr std::uint64_t kHashFullFile = static_cast<std::uint64_t>(-1);

HashResult HashFile(const std::filesystem::path& path, std::uint64_t max_bytes);

/* Convenience for the scanner's first pass — first 64 KiB. */
constexpr std::uint64_t kHashHeadBytes = 64 * 1024;
inline HashResult HashFileHead(const std::filesystem::path& p) {
    return HashFile(p, kHashHeadBytes);
}

}  /* namespace triage */
