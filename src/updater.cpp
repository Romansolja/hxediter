#include "updater.h"

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifndef APP_VERSION
#  define APP_VERSION "0.0.0"
#endif

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace updater {

namespace {

constexpr const char* kRepoOwner = "Romansolja";
constexpr const char* kRepoName  = "hxediter";
/* Force APP_VERSION to a wide literal for UA concatenation; mixing L"a" "b"
 * is compiler-dependent. */
#define UPDATER_WIDEN_INNER(x) L##x
#define UPDATER_WIDEN(x) UPDATER_WIDEN_INNER(x)
constexpr const wchar_t* kUserAgent = L"hxediter/" UPDATER_WIDEN(APP_VERSION);
constexpr int64_t kDebounceSeconds = 6 * 60 * 60;

/* Hard caps so a malicious or misbehaving server can't exhaust memory or
 * disk. GitHub releases JSON is ~10KB; installers are under 20MB. */
constexpr size_t   kMaxJsonBytes      = 4 * 1024 * 1024;
constexpr uint64_t kMaxInstallerBytes = 200ull * 1024 * 1024;

std::mutex g_mx;
Snapshot   g_snap;

/* shared_ptr so a worker outliving the main thread still has a valid flag
 * to read. RequestAbandon() flips it; workers check before writes. */
std::shared_ptr<std::atomic<bool>> g_abandon = std::make_shared<std::atomic<bool>>(false);

std::atomic<bool> g_check_in_flight{false};
std::atomic<bool> g_download_in_flight{false};

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), need);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                    nullptr, 0, nullptr, nullptr);
    std::string s(need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                         s.data(), need, nullptr, nullptr);
    return s;
}

template <typename F>
void WithSnap(F&& f) {
    std::lock_guard<std::mutex> lk(g_mx);
    f(g_snap);
}

bool AbandonRequested() { return g_abandon->load(); }

std::optional<std::tuple<int,int,int>> ParseVersion(const std::string& raw) {
    const char* p = raw.c_str();
    if (*p == 'v' || *p == 'V') ++p;
    int parts[3] = {0, 0, 0};
    int i = 0;
    while (i < 3) {
        if (*p < '0' || *p > '9') return std::nullopt;
        int n = 0;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            if (n > 1'000'000) return std::nullopt;
            ++p;
        }
        parts[i++] = n;
        if (*p == '.') { ++p; continue; }
        break;
    }
    return std::make_tuple(parts[0], parts[1], parts[2]);
}

int CompareVersions(const std::tuple<int,int,int>& a,
                    const std::tuple<int,int,int>& b) {
    if (a < b) return -1;
    if (a > b) return  1;
    return 0;
}

std::wstring LocalAppDataDir() {
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


std::wstring LaunchFailureMarkerFile() {
    std::wstring d = LocalAppDataDir();
    if (d.empty()) return L"";
    return d + L"\\last_update_failure.txt";
}

std::wstring DebounceStateFile() {
    std::wstring d = LocalAppDataDir();
    if (d.empty()) return L"";
    return d + L"\\update_state.json";
}

std::wstring TempInstallerPath(const std::string& version) {
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring path = tmp;
    path += L"HxEditer-installer-";
    path += Utf8ToWide(version);
    path += L".exe";
    return path;
}

int64_t NowUnix() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

int64_t LoadLastCheckUnix() {
    std::wstring f = DebounceStateFile();
    if (f.empty()) return 0;
    /* MinGW-w64 libstdc++ accepts const wchar_t* but not std::wstring. */
    std::ifstream in(f.c_str());
    if (!in) return 0;
    std::stringstream ss; ss << in.rdbuf();
    try {
        auto j = nlohmann::json::parse(ss.str(), nullptr, false);
        if (j.is_discarded()) return 0;
        return j.value("last_check_unix", (int64_t)0);
    } catch (...) { return 0; }
}

void SaveLastCheckUnix(int64_t v) {
    std::wstring f = DebounceStateFile();
    if (f.empty()) return;
    nlohmann::json j;
    j["last_check_unix"] = v;
    std::ofstream out(f.c_str(), std::ios::binary | std::ios::trunc);
    if (out) out << j.dump();
}

struct HSession { HINTERNET h = nullptr; ~HSession() { if (h) WinHttpCloseHandle(h); } };
struct HConn    { HINTERNET h = nullptr; ~HConn()    { if (h) WinHttpCloseHandle(h); } };
struct HReq     { HINTERNET h = nullptr; ~HReq()     { if (h) WinHttpCloseHandle(h); } };

bool OpenSession(HSession& out) {
    out.h = WinHttpOpen(kUserAgent,
                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!out.h) return false;
    /* Default on older Win10 is TLS 1.0; pin to 1.2+. */
    DWORD secure = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    secure |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(out.h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secure, sizeof(secure));
    return true;
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
};

bool CrackUrl(const std::wstring& url, ParsedUrl& out) {
    URL_COMPONENTS uc{};
    uc.dwStructSize      = sizeof(uc);
    wchar_t host_buf[256]; wchar_t path_buf[2048];
    uc.lpszHostName      = host_buf; uc.dwHostNameLength     = 256;
    uc.lpszUrlPath       = path_buf; uc.dwUrlPathLength      = 2048;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
    out.host   = std::wstring(uc.lpszHostName, uc.dwHostNameLength);
    out.path   = std::wstring(uc.lpszUrlPath,  uc.dwUrlPathLength);
    out.port   = uc.nPort;
    out.secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

bool OpenRequest(HSession& sess, const ParsedUrl& u, HConn& conn, HReq& req) {
    conn.h = WinHttpConnect(sess.h, u.host.c_str(), u.port, 0);
    if (!conn.h) return false;
    DWORD flags = u.secure ? WINHTTP_FLAG_SECURE : 0;
    req.h = WinHttpOpenRequest(conn.h, L"GET", u.path.c_str(), nullptr,
                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                flags);
    if (!req.h) return false;

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    /* Without these, upstream proxies can serve stale "you're up to date". */
    const wchar_t* no_cache =
        L"Cache-Control: no-cache, no-store, max-age=0\r\n"
        L"Pragma: no-cache\r\n"
        L"Accept: application/vnd.github+json\r\n";
    WinHttpAddRequestHeaders(req.h, no_cache, (DWORD)-1,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    return true;
}

bool HttpGetString(const std::wstring& url, std::string& out_body, std::string& err) {
    ParsedUrl u;
    if (!CrackUrl(url, u)) { err = "bad url"; return false; }
    HSession sess;
    if (!OpenSession(sess)) { err = "WinHttpOpen failed"; return false; }
    HConn conn; HReq req;
    if (!OpenRequest(sess, u, conn, req)) { err = "WinHttpOpenRequest failed"; return false; }

    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        err = "WinHttpSendRequest failed"; return false;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) {
        err = "WinHttpReceiveResponse failed"; return false;
    }

    DWORD status = 0; DWORD status_sz = sizeof(status);
    WinHttpQueryHeaders(req.h,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "HTTP %lu", (unsigned long)status);
        err = buf;
        return false;
    }

    out_body.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) { err = "QueryDataAvailable failed"; return false; }
        if (avail == 0) break;
        if (out_body.size() + avail > kMaxJsonBytes) {
            err = "response too large";
            return false;
        }
        size_t old = out_body.size();
        out_body.resize(old + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, out_body.data() + old, avail, &got)) {
            err = "ReadData failed"; return false;
        }
        out_body.resize(old + got);
        if (AbandonRequested()) { err = "abandoned"; return false; }
    }
    return true;
}

bool HttpDownloadToFile(const std::wstring& url,
                       const std::wstring& dest_path,
                       std::string& err) {
    ParsedUrl u;
    if (!CrackUrl(url, u)) { err = "bad url"; return false; }
    HSession sess;
    if (!OpenSession(sess)) { err = "WinHttpOpen failed"; return false; }
    HConn conn; HReq req;
    if (!OpenRequest(sess, u, conn, req)) { err = "WinHttpOpenRequest failed"; return false; }

    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        err = "WinHttpSendRequest failed"; return false;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) {
        err = "WinHttpReceiveResponse failed"; return false;
    }

    DWORD status = 0; DWORD ssz = sizeof(status);
    WinHttpQueryHeaders(req.h,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &ssz, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "HTTP %lu", (unsigned long)status);
        err = buf;
        return false;
    }

    /* Content-Length may be absent on chunked responses; zero is fine. */
    uint64_t total = 0;
    {
        wchar_t lenbuf[32] = {0}; DWORD lsz = sizeof(lenbuf);
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                lenbuf, &lsz, WINHTTP_NO_HEADER_INDEX)) {
            total = (uint64_t)_wtoi64(lenbuf);
        }
    }
    if (total > kMaxInstallerBytes) {
        err = "installer exceeds max allowed size";
        return false;
    }
    WithSnap([&](Snapshot& s) { s.bytes_total = total; s.bytes_received = 0; });

    HANDLE file = CreateFileW(dest_path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { err = "CreateFile failed"; return false; }

    uint64_t got_total = 0;
    uint64_t last_tick = 0;
    constexpr uint64_t kTickEvery = 256 * 1024;
    std::vector<uint8_t> buf(64 * 1024);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) {
            CloseHandle(file); err = "QueryDataAvailable failed"; return false;
        }
        if (avail == 0) break;
        while (avail > 0) {
            DWORD want = avail > buf.size() ? (DWORD)buf.size() : avail;
            DWORD got = 0;
            if (!WinHttpReadData(req.h, buf.data(), want, &got) || got == 0) {
                CloseHandle(file); err = "ReadData failed"; return false;
            }
            if (got_total + got > kMaxInstallerBytes) {
                CloseHandle(file);
                DeleteFileW(dest_path.c_str());
                err = "installer exceeds max allowed size";
                return false;
            }
            DWORD written = 0;
            if (!WriteFile(file, buf.data(), got, &written, nullptr) || written != got) {
                CloseHandle(file); err = "WriteFile failed"; return false;
            }
            got_total += got;
            avail     -= got;
            if (got_total - last_tick >= kTickEvery) {
                WithSnap([&](Snapshot& s) { s.bytes_received = got_total; });
                last_tick = got_total;
            }
            if (AbandonRequested()) {
                CloseHandle(file);
                DeleteFileW(dest_path.c_str());
                err = "abandoned";
                return false;
            }
        }
    }
    CloseHandle(file);
    WithSnap([&](Snapshot& s) {
        s.bytes_received = got_total;
        if (s.bytes_total == 0) s.bytes_total = got_total;
    });
    return true;
}

std::string HexLower(const uint8_t* data, size_t n) {
    static const char hex[] = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) {
        s[2*i]     = hex[data[i] >> 4];
        s[2*i + 1] = hex[data[i] & 0xF];
    }
    return s;
}

std::optional<std::string> ComputeSha256(const std::wstring& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return std::nullopt;
    }
    struct AlgGuard { BCRYPT_ALG_HANDLE& a; ~AlgGuard() { if (a) BCryptCloseAlgorithmProvider(a, 0); } } ag{alg};

    DWORD hash_len = 0; ULONG rb = 0;
    if (!NT_SUCCESS(BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                                       (PUCHAR)&hash_len, sizeof(hash_len), &rb, 0))) {
        return std::nullopt;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!NT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        return std::nullopt;
    }
    struct HashGuard { BCRYPT_HASH_HANDLE& h; ~HashGuard() { if (h) BCryptDestroyHash(h); } } hg{hash};

    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::nullopt;
    std::vector<uint8_t> buf(64 * 1024);
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr)) {
            CloseHandle(f); return std::nullopt;
        }
        if (got == 0) break;
        if (!NT_SUCCESS(BCryptHashData(hash, buf.data(), got, 0))) {
            CloseHandle(f); return std::nullopt;
        }
    }
    CloseHandle(f);
    std::vector<uint8_t> out(hash_len);
    if (!NT_SUCCESS(BCryptFinishHash(hash, out.data(), (ULONG)out.size(), 0))) {
        return std::nullopt;
    }
    return HexLower(out.data(), out.size());
}

/* sha256sum format: "<64-hex>  <filename>\n". */
std::optional<std::string> ExtractExpectedSha(const std::string& sums_body,
                                              const std::string& wanted_filename) {
    std::stringstream ss(sums_body);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        size_t i = 0;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i != 64) continue;
        std::string hex = line.substr(0, 64);
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '*')) ++i;
        std::string name = line.substr(i);
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n' ||
                                 name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        if (name == wanted_filename) return hex;
    }
    return std::nullopt;
}

struct ReleaseInfo {
    std::string tag_name;
    std::string installer_url;
    std::string installer_name;
    std::string sums_url;
};

std::optional<ReleaseInfo> ParseRelease(const std::string& body, std::string& err) {
    try {
        auto j = nlohmann::json::parse(body);
        ReleaseInfo r;
        r.tag_name = j.value("tag_name", "");
        if (r.tag_name.empty()) { err = "no tag_name"; return std::nullopt; }
        if (!j.contains("assets") || !j["assets"].is_array()) {
            err = "no assets"; return std::nullopt;
        }
        for (const auto& a : j["assets"]) {
            std::string name = a.value("name", "");
            std::string url  = a.value("browser_download_url", "");
            if (name.empty() || url.empty()) continue;
            if (name.size() > 10 &&
                name.rfind("HxEditer-", 0) == 0 &&
                name.find("-win64.exe") != std::string::npos) {
                r.installer_url = url;
                r.installer_name = name;
            } else if (name == "SHA256SUMS.txt") {
                r.sums_url = url;
            }
        }
        if (r.installer_url.empty()) { err = "no installer asset"; return std::nullopt; }
        return r;
    } catch (const std::exception& e) {
        err = std::string("json: ") + e.what();
        return std::nullopt;
    }
}

void DoCheck(bool force) {
    (void)force;
    bool expected = false;
    if (!g_check_in_flight.compare_exchange_strong(expected, true)) return;
    struct Releaser { ~Releaser() { g_check_in_flight.store(false); } } rel;

    WithSnap([](Snapshot& s) {
        s.check = CheckState::InProgress;
        s.error_message.clear();
    });

    std::string api_url_u8 = std::string("https://api.github.com/repos/")
        + kRepoOwner + "/" + kRepoName + "/releases/latest";
    std::string body; std::string err;
    if (!HttpGetString(Utf8ToWide(api_url_u8), body, err)) {
        if (AbandonRequested()) return;
        WithSnap([&](Snapshot& s) {
            s.check = CheckState::NetworkError;
            s.error_message = err;
        });
        return;
    }

    auto rel_info = ParseRelease(body, err);
    if (!rel_info) {
        if (AbandonRequested()) return;
        WithSnap([&](Snapshot& s) {
            s.check = CheckState::ParseError;
            s.error_message = err;
        });
        return;
    }

    auto latest = ParseVersion(rel_info->tag_name);
    auto ours   = ParseVersion(APP_VERSION);
    if (!latest || !ours) {
        if (AbandonRequested()) return;
        WithSnap([&](Snapshot& s) {
            s.check = CheckState::ParseError;
            s.error_message = "could not parse version strings";
        });
        return;
    }

    SaveLastCheckUnix(NowUnix());

    std::string latest_str;
    {
        char b[32];
        std::snprintf(b, sizeof(b), "%d.%d.%d",
            std::get<0>(*latest), std::get<1>(*latest), std::get<2>(*latest));
        latest_str = b;
    }

    if (AbandonRequested()) return;
    if (CompareVersions(*latest, *ours) > 0) {
        WithSnap([&](Snapshot& s) {
            s.check          = CheckState::UpdateAvailable;
            s.latest_version = latest_str;
        });
    } else {
        WithSnap([&](Snapshot& s) {
            s.check          = CheckState::UpToDate;
            s.latest_version = latest_str;
        });
    }
}

void DoDownload() {
    bool expected = false;
    if (!g_download_in_flight.compare_exchange_strong(expected, true)) return;
    struct Releaser { ~Releaser() { g_download_in_flight.store(false); } } rel;

    Snapshot snap;
    WithSnap([&](Snapshot& s) {
        s.download = DownloadState::InProgress;
        s.bytes_received = 0;
        s.bytes_total    = 0;
        s.installer_path.clear();
        snap = s;
    });

    /* Re-fetch: browser_download_url carries short-lived redirect tokens,
     * and we need a fresh SHA256SUMS reference. */
    std::string api_url_u8 = std::string("https://api.github.com/repos/")
        + kRepoOwner + "/" + kRepoName + "/releases/latest";
    std::string body; std::string err;
    if (!HttpGetString(Utf8ToWide(api_url_u8), body, err)) {
        if (AbandonRequested()) return;
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "Could not re-fetch release: " + err;
        });
        return;
    }
    auto rel_info = ParseRelease(body, err);
    if (!rel_info) {
        if (AbandonRequested()) return;
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "Release parse failed: " + err;
        });
        return;
    }
    if (rel_info->sums_url.empty()) {
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "Release is missing SHA256SUMS.txt — cannot verify download. Ask the maintainer to re-release with integrity manifest.";
        });
        return;
    }

    std::string sums_body;
    if (!HttpGetString(Utf8ToWide(rel_info->sums_url), sums_body, err)) {
        if (AbandonRequested()) return;
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "Could not download SHA256SUMS.txt: " + err;
        });
        return;
    }
    auto expected_sha = ExtractExpectedSha(sums_body, rel_info->installer_name);
    if (!expected_sha) {
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "SHA256SUMS.txt has no entry for " + rel_info->installer_name;
        });
        return;
    }

    /* Derive from parsed version so no stray 'v' or pre-release suffix
     * lands in the filename. */
    auto ours = ParseVersion(rel_info->tag_name);
    if (!ours) {
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "Unparseable tag: " + rel_info->tag_name;
        });
        return;
    }
    char vs[32];
    std::snprintf(vs, sizeof(vs), "%d.%d.%d",
        std::get<0>(*ours), std::get<1>(*ours), std::get<2>(*ours));
    std::wstring dst = TempInstallerPath(vs);
    if (dst.empty()) {
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "GetTempPath failed";
        });
        return;
    }

    if (!HttpDownloadToFile(Utf8ToWide(rel_info->installer_url), dst, err)) {
        if (AbandonRequested()) return;
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "Download failed: " + err;
        });
        return;
    }

    if (AbandonRequested()) return;
    auto got_sha = ComputeSha256(dst);
    if (!got_sha) {
        DeleteFileW(dst.c_str());
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "SHA256 compute failed";
        });
        return;
    }
    if (*got_sha != *expected_sha) {
        DeleteFileW(dst.c_str());
        WithSnap([&](Snapshot& s) {
            s.download = DownloadState::Failed;
            s.error_message = "Integrity check failed — expected "
                + *expected_sha + ", got " + *got_sha
                + ". Download discarded.";
        });
        return;
    }

    WithSnap([&](Snapshot& s) {
        s.download       = DownloadState::Complete;
        s.installer_path = WideToUtf8(dst);
    });
}

} /* anonymous namespace */

void InitAndMaybeCheck() {
    int64_t last = LoadLastCheckUnix();
    int64_t now  = NowUnix();
    if (last > 0 && (now - last) < kDebounceSeconds) return;
    StartCheck();
}

void StartCheck() {
    std::thread([]{ DoCheck(/*force=*/true); }).detach();
}

void StartDownload() {
    std::thread([]{ DoDownload(); }).detach();
}

Snapshot GetSnapshot() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_snap;
}

void RequestAbandon() {
    g_abandon->store(true);
}

void SetLaunchError(std::string msg) {
    WithSnap([&](Snapshot& s) {
        s.download       = DownloadState::Failed;
        s.error_message  = std::move(msg);
        s.installer_path.clear();
    });
}

bool ConsumeInstallerPath(std::string& out_path) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_snap.download != DownloadState::Complete) return false;
    if (g_snap.installer_path.empty())               return false;
    out_path = std::move(g_snap.installer_path);
    /* Keep check-state so the UI still reflects "new version exists" while
     * the installer runs. */
    g_snap.download       = DownloadState::Idle;
    g_snap.bytes_received = 0;
    g_snap.bytes_total    = 0;
    return true;
}



bool ConsumeLastLaunchFailure(std::string& out_message) {
    std::wstring f = LaunchFailureMarkerFile();
    if (f.empty()) return false;

    std::ifstream in(f.c_str(), std::ios::binary);
    if (!in) return false;

    std::stringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    in.close();
    DeleteFileW(f.c_str());

    while (!raw.empty()) {
        char c = raw.back();
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
        raw.pop_back();
    }
    if (raw.empty()) return false;

    out_message = "Last update attempt failed: " + raw;
    return true;
}

} /* namespace updater */
