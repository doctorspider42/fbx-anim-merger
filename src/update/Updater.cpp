#include "update/Updater.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

#include "util/Log.h"
#include "util/Version.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#include <winhttp.h>

// Present since Windows 8.1, but missing from some MinGW-w64 header vintages.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#endif

namespace fs = std::filesystem;

namespace fam {
namespace {

bool EndsWithNoCase(const std::string& text, const std::string& suffix) {
    if (suffix.size() > text.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

// ---------------------------------------------------------------------------
// Just enough JSON to read a GitHub release.
//
// Pulling in a parser for four string fields is not worth the dependency, and the
// shape of this document is fixed by GitHub's API contract. Everything here works
// on raw string values and never tries to understand the surrounding structure.
// ---------------------------------------------------------------------------

void AppendUtf8(std::string& out, unsigned int code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

unsigned int ReadHex4(const std::string& json, size_t at) {
    unsigned int value = 0;
    for (size_t i = at; i < at + 4 && i < json.size(); ++i) {
        const char c = json[i];
        value <<= 4;
        if (c >= '0' && c <= '9')      value |= static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
        else return 0;
    }
    return value;
}

// Reads the quoted string that begins at `pos` (which must be the opening quote)
// and leaves `pos` one past the closing quote.
bool ReadJsonString(const std::string& json, size_t& pos, std::string& out) {
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') return true;
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (pos >= json.size()) return false;
        const char esc = json[pos++];
        switch (esc) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                unsigned int code = ReadHex4(json, pos);
                pos += 4;
                // A character outside the BMP arrives as a surrogate pair.
                if (code >= 0xD800 && code <= 0xDBFF && pos + 1 < json.size() &&
                    json[pos] == '\\' && json[pos + 1] == 'u') {
                    const unsigned int low = ReadHex4(json, pos + 2);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        pos += 6;
                    }
                }
                AppendUtf8(out, code);
                break;
            }
            default: out.push_back(esc); break;  // covers \" \\ \/
        }
    }
    return false;
}

// Finds `"key":` at or after `from` and reads its string value. `searchedTo`
// receives the position just past the value, so repeated keys can be walked.
bool JsonString(const std::string& json, const char* key, std::string& out, size_t from = 0,
                size_t* searchedTo = nullptr) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = json.find(needle, from);
    while (at != std::string::npos) {
        size_t pos = at + needle.size();
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        if (pos < json.size() && json[pos] == ':') {
            ++pos;
            while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
            if (pos < json.size() && json[pos] == '"' && ReadJsonString(json, pos, out)) {
                if (searchedTo) *searchedTo = pos;
                return true;
            }
        }
        at = json.find(needle, at + needle.size());
    }
    return false;
}

std::vector<std::string> JsonStringAll(const std::string& json, const char* key) {
    std::vector<std::string> values;
    size_t from = 0;
    std::string value;
    size_t next = 0;
    while (JsonString(json, key, value, from, &next)) {
        values.push_back(value);
        from = next;
    }
    return values;
}

std::string StripTagPrefix(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) return tag.substr(1);
    return tag;
}

// A download URL is only ever fetched if GitHub itself serves it. The JSON comes
// from api.github.com over TLS, but treating any URL inside it as a place to
// download an executable from would be one compromised field away from a very
// bad day.
bool IsTrustedHost(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;
    const size_t start = 8;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    std::string host = url.substr(start, end - start);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (const size_t colon = host.find(':'); colon != std::string::npos) host.erase(colon);
    return host == "github.com" || EndsWithNoCase(host, ".github.com") ||
           EndsWithNoCase(host, ".githubusercontent.com");
}

bool ParseRelease(const std::string& json, ReleaseInfo& out) {
    if (!JsonString(json, "tag_name", out.tag) || out.tag.empty()) return false;
    out.version = StripTagPrefix(out.tag);
    JsonString(json, "body", out.notes);
    out.pageUrl = std::string("https://github.com/") + kAppRepository + "/releases/tag/" + out.tag;

    for (const std::string& url : JsonStringAll(json, "browser_download_url")) {
        if (!IsTrustedHost(url)) continue;
        if (EndsWithNoCase(url, "-setup.exe") && out.installerUrl.empty()) {
            out.installerUrl = url;
            const size_t slash = url.find_last_of('/');
            out.installerName = slash == std::string::npos ? "setup.exe" : url.substr(slash + 1);
        } else if (EndsWithNoCase(url, "-portable.zip") && out.portableUrl.empty()) {
            out.portableUrl = url;
        }
    }
    return true;
}

std::string ApiUrl() {
    return std::string("https://api.github.com/repos/") + kAppRepository + "/releases/latest";
}

#ifdef _WIN32

std::wstring Widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

struct WinHttpHandle {
    HINTERNET handle = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h) : handle(h) {}
    ~WinHttpHandle() { if (handle) WinHttpCloseHandle(handle); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    explicit operator bool() const { return handle != nullptr; }
};

std::string LastErrorText(const char* what) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s (Windows error %lu)", what, GetLastError());
    return buffer;
}

// One GET, following redirects, with the response handed to `sink` in chunks.
// `onLength` is called once with the content length, or 0 when the server did not
// send one. Returns false and fills `error` on any failure, including a non-200.
bool HttpGet(const std::string& url, const wchar_t* extraHeaders,
             const std::function<void(long long)>& onLength,
             const std::function<bool(const char*, size_t)>& sink, std::string& error) {
    const std::wstring wideUrl = Widen(url);

    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    std::wstring host(256, L'\0');
    std::wstring path(4096, L'\0');
    std::wstring extra(4096, L'\0');
    parts.lpszHostName = host.data();
    parts.dwHostNameLength = static_cast<DWORD>(host.size());
    parts.lpszUrlPath = path.data();
    parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    parts.lpszExtraInfo = extra.data();
    parts.dwExtraInfoLength = static_cast<DWORD>(extra.size());
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        error = LastErrorText("malformed URL");
        return false;
    }
    host.resize(parts.dwHostNameLength);
    const std::wstring target = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                                std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    // GitHub rejects requests without a User-Agent outright.
    const std::wstring agent = std::wstring(L"FbxAnimMerger/") + Widen(kAppVersion);
    WinHttpHandle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        error = LastErrorText("could not start a HTTPS session");
        return false;
    }
    // Per operation, not per transfer, so these bound how long closing the window
    // can wait on a check that is still in flight - a download of any size is
    // unaffected as long as data keeps arriving.
    WinHttpSetTimeouts(session.handle, 8000, 8000, 15000, 15000);

    WinHttpHandle connection(WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0));
    if (!connection) {
        error = LastErrorText("could not reach the server");
        return false;
    }

    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(connection.handle, L"GET", target.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             flags));
    if (!request) {
        error = LastErrorText("could not build the request");
        return false;
    }

    if (extraHeaders &&
        !WinHttpAddRequestHeaders(request.handle, extraHeaders, static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD)) {
        error = LastErrorText("could not set the request headers");
        return false;
    }

    if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        error = LastErrorText("the request failed");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        error = LastErrorText("no response status");
        return false;
    }
    if (status != 200) {
        char buffer[128];
        // 404 is the ordinary answer for a repository that has not published a
        // stable release yet, so it gets a message that says so.
        if (status == 404) {
            std::snprintf(buffer, sizeof(buffer), "no published release was found");
        } else {
            std::snprintf(buffer, sizeof(buffer), "the server answered HTTP %lu", status);
        }
        error = buffer;
        return false;
    }

    if (onLength) {
        long long length = 0;
        DWORD lengthSize = sizeof(length);
        if (!WinHttpQueryHeaders(request.handle,
                                 WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize,
                                 WINHTTP_NO_HEADER_INDEX)) {
            length = 0;
        }
        onLength(length);
    }

    std::vector<char> chunk(64 * 1024);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, chunk.data(), static_cast<DWORD>(chunk.size()),
                             &read)) {
            error = LastErrorText("the transfer was interrupted");
            return false;
        }
        if (read == 0) break;
        if (!sink(chunk.data(), read)) {
            error = "cancelled";
            return false;
        }
    }
    return true;
}

fs::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()));
        if (written == 0) return fs::path();
        if (written < buffer.size()) {
            buffer.resize(written);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

#endif  // _WIN32

}  // namespace

// ---------------------------------------------------------------------------
// Platform-independent helpers
// ---------------------------------------------------------------------------

int CompareVersions(const std::string& a, const std::string& b) {
    auto component = [](const std::string& text, size_t& pos) -> long {
        long value = 0;
        bool any = false;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            value = value * 10 + (text[pos] - '0');
            if (value > 1000000000L) value = 1000000000L;  // absurd input, not an overflow
            ++pos;
            any = true;
        }
        // Skip to just past the next separator, dropping any suffix on the way.
        while (pos < text.size() && text[pos] != '.') ++pos;
        if (pos < text.size()) ++pos;
        return any ? value : 0;
    };

    size_t posA = 0;
    size_t posB = 0;
    for (int i = 0; i < 4; ++i) {
        const long left = posA < a.size() ? component(a, posA) : 0;
        const long right = posB < b.size() ? component(b, posB) : 0;
        if (left != right) return left < right ? -1 : 1;
    }
    return 0;
}

bool IsPortableBuild() {
#ifdef _WIN32
    const fs::path exe = ExecutablePath();
    if (exe.empty()) return false;
    std::error_code ec;
    const fs::path dir = exe.parent_path();
    return fs::exists(dir / "PORTABLE", ec) || fs::exists(dir / "portable.txt", ec);
#else
    return true;
#endif
}

bool OpenInBrowser(const std::string& url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return false;
    // The URL reaches a shell on some platforms, so refuse anything carrying
    // characters that could end the argument and start a command.
    if (url.find_first_of(" \t\r\n\"'`$&;|<>\\") != std::string::npos) return false;

#ifdef _WIN32
    const std::wstring wide = Widen(url);
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif defined(__APPLE__)
    return std::system(("open \"" + url + "\"").c_str()) == 0;
#else
    return std::system(("xdg-open \"" + url + "\" >/dev/null 2>&1 &").c_str()) == 0;
#endif
}

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------

Updater::~Updater() {
    m_cancel.store(true, std::memory_order_release);
    Join();
}

void Updater::Join() {
    if (m_worker.joinable()) m_worker.join();
}

bool Updater::Busy() const {
    const UpdateState state = State();
    return state == UpdateState::Checking || state == UpdateState::Downloading;
}

ReleaseInfo Updater::Release() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_release;
}

std::string Updater::Error() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

void Updater::Finish(UpdateState state, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = error;
    }
    m_state.store(state, std::memory_order_release);
}

void Updater::CheckAsync() {
    if (Busy()) return;
    Join();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error.clear();
        m_release = ReleaseInfo();
        m_downloadedFile.clear();
    }
    m_progress.store(0.0f, std::memory_order_relaxed);
    m_state.store(UpdateState::Checking, std::memory_order_release);

#ifdef _WIN32
    m_worker = std::thread([this] {
        std::string body;
        std::string error;
        const bool ok = HttpGet(
            ApiUrl(),
            L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
            nullptr,
            [&body, this](const char* data, size_t size) {
                if (m_cancel.load(std::memory_order_acquire)) return false;
                if (body.size() + size > 4u * 1024u * 1024u) return false;
                body.append(data, size);
                return true;
            },
            error);
        if (!ok) {
            Finish(UpdateState::Failed, error);
            return;
        }

        ReleaseInfo info;
        if (!ParseRelease(body, info)) {
            Finish(UpdateState::Failed, "GitHub's answer did not contain a release");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_release = info;
        }
        const bool newer = CompareVersions(info.version, kAppVersion) > 0;
        Finish(newer ? UpdateState::Available : UpdateState::UpToDate, "");
    });
#else
    Finish(UpdateState::Failed, "automatic updates are only implemented on Windows");
#endif
}

void Updater::DownloadAsync() {
    if (Busy()) return;
    if (State() != UpdateState::Available) return;
    const ReleaseInfo info = Release();
    if (info.installerUrl.empty()) return;
    Join();

    m_progress.store(0.0f, std::memory_order_relaxed);
    m_state.store(UpdateState::Downloading, std::memory_order_release);

#ifdef _WIN32
    m_worker = std::thread([this, info] {
        std::error_code ec;
        const fs::path directory = fs::temp_directory_path(ec) / "FbxAnimMerger-update";
        if (ec) {
            Finish(UpdateState::Failed, "no writable temporary directory");
            return;
        }
        fs::create_directories(directory, ec);

        // The name is taken from our own asset naming, not from the response, so a
        // crafted URL cannot steer the write anywhere but into this directory.
        const fs::path target = directory / fs::path(info.installerName).filename();
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            Finish(UpdateState::Failed, "could not write to " + target.string());
            return;
        }

        long long expected = 0;
        long long received = 0;
        std::string error;
        const bool ok = HttpGet(
            info.installerUrl, nullptr,
            [&expected](long long length) { expected = length; },
            [&](const char* data, size_t size) {
                if (m_cancel.load(std::memory_order_acquire)) return false;
                out.write(data, static_cast<std::streamsize>(size));
                if (!out) return false;
                received += static_cast<long long>(size);
                m_progress.store(expected > 0
                                     ? static_cast<float>(static_cast<double>(received) /
                                                          static_cast<double>(expected))
                                     : -1.0f,
                                 std::memory_order_relaxed);
                return true;
            },
            error);
        out.close();

        if (!ok) {
            fs::remove(target, ec);
            Finish(UpdateState::Failed, error);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_downloadedFile = target.string();
        }
        m_progress.store(1.0f, std::memory_order_relaxed);
        Finish(UpdateState::ReadyToInstall, "");
    });
#else
    Finish(UpdateState::Failed, "automatic updates are only implemented on Windows");
#endif
}

bool Updater::LaunchInstaller() {
    std::string installer;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        installer = m_downloadedFile;
    }
    if (installer.empty()) return false;

#ifdef _WIN32
    // /UPDATE is ours: the script uses it to tell an update apart from a scripted
    // silent deployment, and relaunches the application only for the former.
    const std::wstring file = Widen(installer);
    const std::wstring arguments = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE";
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", file.c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        Finish(UpdateState::Failed, LastErrorText("could not start the installer"));
        return false;
    }
    LogInfo("Installer started; the application will close so it can be replaced.");
    return true;
#else
    return false;
#endif
}

}  // namespace fam
