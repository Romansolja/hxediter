#include "triage/signatures.h"

#include <array>
#include <cstring>

/* Magic-byte signature table.
 *
 * Layout: each entry is { id, useful, magic_bytes, magic_len, offset }.
 * `magic_bytes` is checked against `data + offset`, which must lie within
 * the buffer. The first matching entry wins, so order specific-then-
 * general (e.g. ZIP-based formats currently all map to "zip"; if/when
 * we split docx/jar/apk out via secondary heuristics, those entries go
 * BEFORE the bare ZIP entry).
 *
 * Two formats need post-match disambiguation because the magic bytes
 * are shared by multiple file types: ZIP (docx/xlsx/jar/apk all start
 * PK\x03\x04) and ISO base-media-file-format (mp4/mov/heic/avif/3gp
 * all have "ftyp" at offset 4). For ZIP we currently accept the loss
 * of specificity; for ISO-BMFF we look at the major_brand at offset 8
 * and promote the id, falling back to "iso-bmff" for unknown brands.
 *
 * Reference for the byte values: https://en.wikipedia.org/wiki/List_of_file_signatures
 * Cross-checked against the file(1) magic database for the formats below.
 *
 * 26 entries — see plan signature-table policy. Count is enforced at
 * compile time by the std::array<Sig, N> declaration below. */

namespace triage {

namespace {

struct Sig {
    const char*         id;
    bool                useful;
    const std::uint8_t* magic;
    std::size_t         magic_len;
    std::size_t         offset;
};

/* Magic-byte literals. Defined as anonymous-namespace globals so the
 * Sig table can hold pointers to them with static lifetime. */
constexpr std::uint8_t kMZ[]      = {0x4D, 0x5A};                       /* MZ */
constexpr std::uint8_t kELF[]     = {0x7F, 0x45, 0x4C, 0x46};           /* \x7FELF */
constexpr std::uint8_t kMachO32[] = {0xFE, 0xED, 0xFA, 0xCE};
constexpr std::uint8_t kMachO64[] = {0xFE, 0xED, 0xFA, 0xCF};
constexpr std::uint8_t kMachO32R[]= {0xCE, 0xFA, 0xED, 0xFE};
constexpr std::uint8_t kMachO64R[]= {0xCF, 0xFA, 0xED, 0xFE};
constexpr std::uint8_t kPDF[]     = {0x25, 0x50, 0x44, 0x46, 0x2D};     /* %PDF- */
constexpr std::uint8_t kZIP[]     = {0x50, 0x4B, 0x03, 0x04};           /* PK\x03\x04 — covers Office docx/xlsx/pptx, jar, apk, epub */
constexpr std::uint8_t kSevenZ[]  = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};
constexpr std::uint8_t kRAR[]     = {0x52, 0x61, 0x72, 0x21, 0x1A, 0x07};
constexpr std::uint8_t kGZIP[]    = {0x1F, 0x8B};
constexpr std::uint8_t kPNG[]     = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::uint8_t kJPEG[]    = {0xFF, 0xD8, 0xFF};
constexpr std::uint8_t kGIF87[]   = {0x47, 0x49, 0x46, 0x38, 0x37, 0x61}; /* GIF87a */
constexpr std::uint8_t kGIF89[]   = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61}; /* GIF89a */
constexpr std::uint8_t kBMP[]     = {0x42, 0x4D};                         /* BM */
constexpr std::uint8_t kICO[]     = {0x00, 0x00, 0x01, 0x00};
constexpr std::uint8_t kTTF[]     = {0x00, 0x01, 0x00, 0x00};
constexpr std::uint8_t kOTF[]     = {0x4F, 0x54, 0x54, 0x4F};             /* OTTO */
constexpr std::uint8_t kTTC[]     = {0x74, 0x74, 0x63, 0x66};             /* ttcf */
constexpr std::uint8_t kID3[]     = {0x49, 0x44, 0x33};                   /* ID3 — MP3 */
constexpr std::uint8_t kFTYP[]    = {0x66, 0x74, 0x79, 0x70};             /* "ftyp" at offset 4; ISO base-media-file-format envelope (MP4, MOV, HEIC, AVIF, 3GP, JP2, CR3...). LookupSignature reads the major_brand at offset 8 to promote the id to a specific format. */
constexpr std::uint8_t kRIFF[]    = {0x52, 0x49, 0x46, 0x46};             /* RIFF — WAV/AVI/WEBP */
constexpr std::uint8_t kOgg[]     = {0x4F, 0x67, 0x67, 0x53};             /* OggS */
constexpr std::uint8_t kFLAC[]    = {0x66, 0x4C, 0x61, 0x43};             /* fLaC */
constexpr std::uint8_t kSQLite[]  = {0x53, 0x51, 0x4C, 0x69, 0x74, 0x65,
                                     0x20, 0x66, 0x6F, 0x72, 0x6D, 0x61,
                                     0x74, 0x20, 0x33, 0x00};             /* SQLite format 3\0 */

constexpr std::array<Sig, 26> kTable = {{
    /* Order matters: more specific patterns before more general ones. */
    {"pe",      true, kMZ,       sizeof(kMZ),       0},
    {"elf",     true, kELF,      sizeof(kELF),      0},
    {"macho",   true, kMachO32,  sizeof(kMachO32),  0},
    {"macho",   true, kMachO64,  sizeof(kMachO64),  0},
    {"macho",   true, kMachO32R, sizeof(kMachO32R), 0},
    {"macho",   true, kMachO64R, sizeof(kMachO64R), 0},
    {"pdf",     true, kPDF,      sizeof(kPDF),      0},
    {"zip",     true, kZIP,      sizeof(kZIP),      0},
    {"7z",      true, kSevenZ,   sizeof(kSevenZ),   0},
    {"rar",     true, kRAR,      sizeof(kRAR),      0},
    {"gzip",    true, kGZIP,     sizeof(kGZIP),     0},
    {"png",     true, kPNG,      sizeof(kPNG),      0},
    {"jpeg",    true, kJPEG,     sizeof(kJPEG),     0},
    {"gif",     true, kGIF87,    sizeof(kGIF87),    0},
    {"gif",     true, kGIF89,    sizeof(kGIF89),    0},
    {"bmp",     true, kBMP,      sizeof(kBMP),      0},
    {"ico",     true, kICO,      sizeof(kICO),      0},
    {"ttf",     true, kTTF,      sizeof(kTTF),      0},
    {"otf",     true, kOTF,      sizeof(kOTF),      0},
    {"ttc",     true, kTTC,      sizeof(kTTC),      0},
    {"mp3",     true, kID3,      sizeof(kID3),      0},
    {"iso-bmff",true, kFTYP,     sizeof(kFTYP),     4},  /* "ftyp" at offset 4; brand at 8 promotes id */
    {"riff",    true, kRIFF,     sizeof(kRIFF),     0},  /* WAV/AVI/WEBP all start RIFF */
    {"ogg",     true, kOgg,      sizeof(kOgg),      0},
    {"flac",    true, kFLAC,     sizeof(kFLAC),     0},
    {"sqlite",  true, kSQLite,   sizeof(kSQLite),   0},
}};

/* Promote a generic "iso-bmff" match to a brand-specific id by reading
 * the 4-byte major_brand at offset 8. Returns "iso-bmff" if the brand
 * isn't one we recognise — better than mislabelling a HEIC file as
 * "mp4". `brand` must point to at least 4 readable bytes. */
const char* ClassifyIsoBmffBrand(const std::uint8_t* brand) {
    auto eq = [&](const char* s) {
        return brand[0] == (std::uint8_t)s[0]
            && brand[1] == (std::uint8_t)s[1]
            && brand[2] == (std::uint8_t)s[2]
            && brand[3] == (std::uint8_t)s[3];
    };

    /* QuickTime first — the only one with all-lowercase + spaces. */
    if (eq("qt  ")) return "mov";

    /* Apple HEIC family + generic HEIF. */
    if (eq("heic") || eq("heix") || eq("hevc") || eq("hevx")
     || eq("mif1") || eq("msf1") || eq("heim") || eq("heis")
     || eq("hevm") || eq("hevs")) return "heic";

    /* AV1 still / sequence. */
    if (eq("avif") || eq("avis")) return "avif";

    /* 3GPP / 3GPP2: any brand starting "3g". */
    if (brand[0] == '3' && brand[1] == 'g') return "3gp";

    /* Canon CR3 raw. */
    if (eq("crx ")) return "cr3";

    /* JPEG 2000 family. */
    if (eq("jp2 ")) return "jp2";
    if (eq("jpx ")) return "jpx";
    if (eq("jpm ")) return "jpm";

    /* MP4 / M4* + the generic "isom"/"iso2".../"iso9"/"dash"/"avc1"
     * brands that streaming players use. */
    if (eq("mp41") || eq("mp42") || eq("mp71")
     || eq("M4V ") || eq("M4A ") || eq("M4P ") || eq("M4B ")
     || eq("isom") || eq("iso2") || eq("iso3") || eq("iso4")
     || eq("iso5") || eq("iso6") || eq("iso7") || eq("iso8") || eq("iso9")
     || eq("dash") || eq("avc1") || eq("f4v ")) return "mp4";

    /* Unknown brand: report the family. The user's panel + JSONL output
     * are honest, the file isn't mislabelled, and we don't lose the
     * "this is a recognised media container" signal. */
    return "iso-bmff";
}

}  /* namespace */

SignatureMatch LookupSignature(const std::uint8_t* data, std::size_t len) {
    SignatureMatch result;
    if (data == nullptr || len == 0) return result;

    for (const Sig& s : kTable) {
        const std::size_t need = s.offset + s.magic_len;
        if (len < need) continue;
        if (std::memcmp(data + s.offset, s.magic, s.magic_len) == 0) {
            result.signature_id = s.id;
            result.useful       = s.useful;

            /* Post-match disambiguation for ISO base-media-file-format.
             * The kTable entry id is "iso-bmff" as a placeholder; we
             * promote it here to a brand-specific label when the
             * major_brand at offset 8 is recognised. Need 12 readable
             * bytes total (4 size + 4 "ftyp" + 4 major_brand). */
            if (result.signature_id == "iso-bmff" && len >= 12) {
                result.signature_id = ClassifyIsoBmffBrand(data + 8);
            }

            return result;
        }
    }
    return result;  /* signature_id stays "" */
}

}  /* namespace triage */
