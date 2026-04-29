#pragma once

/* triage::signatures — magic-byte file-type detection.
 *
 * Internal to the triage module; the public ClassifyFile() in
 * classifier.h is the user-facing entry point that consumes this. */

#include <cstddef>
#include <cstdint>
#include <string>

namespace triage {

/* Result of a signature lookup against the built-in table.
 *
 * signature_id is empty ("") when no entry matched. `useful` is the
 * baseline policy hint for matched types — currently always true (every
 * recognised type is treated as content worth keeping unless overridden
 * by dup classification). The flag exists so future entries can carry a
 * different default without changing the API. */
struct SignatureMatch {
    std::string signature_id;  /* e.g. "pdf", "pe", "macho", "riff", "" */
    bool        useful = true;
};

/* Need at least this many leading bytes for full table coverage. Smaller
 * is fine (small files just won't match the longest signatures). */
constexpr std::size_t kSignaturePeekBytes = 20;

/* Match leading bytes of a file against the built-in signature table.
 * `data` may be nullptr when len == 0. */
SignatureMatch LookupSignature(const std::uint8_t* data, std::size_t len);

}  /* namespace triage */
