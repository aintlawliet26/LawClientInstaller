#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../assets/resource.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
constexpr wchar_t kWindowClass[] = L"LawClientInstallerWindow";
constexpr wchar_t kWindowTitle[] = L"LawClient Installer";
constexpr wchar_t kManifestUrl[] = L"https://lawclient.online/api/v1/launcher/installer/windows-x86_64";
constexpr wchar_t kAllowedHost[] = L"lawclient.online";
constexpr wchar_t kAllowedHostWww[] = L"www.lawclient.online";
constexpr UINT WM_APP_STATUS = WM_APP + 1;
constexpr UINT WM_APP_PROGRESS = WM_APP + 2;
constexpr UINT WM_APP_DONE = WM_APP + 3;
constexpr UINT WM_APP_ERROR = WM_APP + 4;
constexpr int kWindowWidth = 560;
constexpr int kWindowHeight = 380;

HWND g_window = nullptr;
HFONT g_fontRegular = nullptr;
HFONT g_fontMedium = nullptr;
HFONT g_fontBold = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSmall = nullptr;
HICON g_icon = nullptr;
std::wstring g_status = L"Ready";
std::wstring g_detail;
std::wstring g_buttonText = L"Install LawClient";
std::filesystem::path g_installDirectory;
std::atomic<bool> g_busy{false};
std::atomic<int> g_progress{0};
std::atomic<bool> g_finished{false};
bool g_error = false;
bool g_openAfterInstall = true;
RECT g_buttonRect{40, 300, 520, 344};
RECT g_closeRect{510, 12, 544, 46};
RECT g_changeRect{424, 225, 504, 257};
RECT g_locationRect{40, 218, 520, 264};
RECT g_openCheckboxHitRect{40, 214, 250, 248};
POINT g_dragOrigin{};
bool g_dragging = false;

COLORREF rgb(unsigned hex) {
    return RGB((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff);
}

void set_text_color(HDC dc, COLORREF color) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
}

void fill_round_rect(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void draw_text(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT flags) {
    auto old = SelectObject(dc, font);
    set_text_color(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
    SelectObject(dc, old);
}

void post_status(const std::wstring& status, const std::wstring& detail) {
    auto payload = new std::pair<std::wstring, std::wstring>(status, detail);
    PostMessageW(g_window, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(payload));
}

void post_progress(int value) {
    value = std::clamp(value, 0, 100);
    PostMessageW(g_window, WM_APP_PROGRESS, static_cast<WPARAM>(value), 0);
}

std::wstring trim(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && std::iswspace(value.back())) value.pop_back();
    return value;
}

std::string trim_ascii(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::wstring utf8_to_wide(const std::string& input) {
    if (input.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring output(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), output.data(), needed);
    return output;
}

std::string wide_to_utf8(const std::wstring& input) {
    if (input.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string output(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), needed, nullptr, nullptr);
    return output;
}

std::string json_string(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) return {};
    pos++;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    if (pos >= json.size() || json[pos] != '"') return {};
    pos++;
    std::string result;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        char ch = json[pos];
        if (escaped) {
            switch (ch) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(ch); break;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return result;
        } else {
            result.push_back(ch);
        }
    }
    return {};
}

uint64_t json_u64(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    size_t end = pos;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) end++;
    if (end == pos) return 0;
    try { return std::stoull(json.substr(pos, end - pos)); } catch (...) { return 0; }
}

bool is_sha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
};

bool parse_url(const std::wstring& url, ParsedUrl& out) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) return false;
    out.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    out.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength && parts.lpszExtraInfo) out.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (out.path.empty()) out.path = L"/";
    out.port = parts.nPort;
    out.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

struct HttpHandle {
    HINTERNET value = nullptr;
    HttpHandle() = default;
    explicit HttpHandle(HINTERNET v) : value(v) {}
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    HttpHandle(HttpHandle&& other) noexcept : value(other.value) { other.value = nullptr; }
    HttpHandle& operator=(HttpHandle&& other) noexcept {
        if (this != &other) {
            if (value) WinHttpCloseHandle(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    explicit operator bool() const { return value != nullptr; }
};

bool query_status(HINTERNET request, DWORD& status) {
    DWORD size = sizeof(status);
    return WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
}

uint64_t query_content_length(HINTERNET request) {
    wchar_t buffer[64]{};
    DWORD size = sizeof(buffer);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
        WINHTTP_HEADER_NAME_BY_INDEX, buffer, &size, WINHTTP_NO_HEADER_INDEX)) return 0;
    try { return std::stoull(buffer); } catch (...) { return 0; }
}

std::wstring request_final_url(HINTERNET request) {
    DWORD size = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &size);
    if (!size) return {};
    std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 2, L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, buffer.data(), &size)) return {};
    return std::wstring(buffer.data());
}

bool begin_request(const std::wstring& url, HttpHandle& session, HttpHandle& connection, HttpHandle& request, std::wstring& error) {
    ParsedUrl parsed;
    if (!parse_url(url, parsed) || !parsed.secure) {
        error = L"Only HTTPS downloads are allowed.";
        return false;
    }

    session = HttpHandle(WinHttpOpen(L"LawClientInstaller/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        error = L"Could not initialize Windows networking.";
        return false;
    }
    WinHttpSetTimeouts(session.value, 10000, 10000, 30000, 30000);

    connection = HttpHandle(WinHttpConnect(session.value, parsed.host.c_str(), parsed.port, 0));
    if (!connection) {
        error = L"Could not connect to the LawClient server.";
        return false;
    }

    request = HttpHandle(WinHttpOpenRequest(connection.value, L"GET", parsed.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH));
    if (!request) {
        error = L"Could not create the download request.";
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    const wchar_t headers[] = L"Cache-Control: no-cache, no-store\r\nPragma: no-cache\r\n";
    if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        error = L"LawClient server did not respond.";
        return false;
    }

    DWORD status = 0;
    if (!query_status(request.value, status) || status < 200 || status >= 300) {
        std::wstringstream ss;
        ss << L"LawClient server returned HTTP " << status << L".";
        error = ss.str();
        return false;
    }
    return true;
}

bool http_get_text(const std::wstring& url, std::string& body, std::wstring& error) {
    HttpHandle session, connection, request;
    if (!begin_request(url, session, connection, request, error)) return false;
    body.clear();
    std::array<char, 16 * 1024> buffer{};
    while (true) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            error = L"Could not read the LawClient manifest.";
            return false;
        }
        if (read == 0) break;
        if (body.size() + read > 1024 * 1024) {
            error = L"LawClient manifest is unexpectedly large.";
            return false;
        }
        body.append(buffer.data(), read);
    }
    return true;
}

std::string bytes_to_hex(const std::array<UCHAR, 32>& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (auto byte : bytes) stream << std::setw(2) << static_cast<int>(byte);
    return stream.str();
}

bool host_allowed(const std::wstring& url) {
    ParsedUrl parsed;
    if (!parse_url(url, parsed) || !parsed.secure) return false;
    std::wstring host = parsed.host;
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return host == kAllowedHost || host == kAllowedHostWww;
}

bool download_and_verify(const std::wstring& url, const std::string& expectedSha, const std::filesystem::path& destination,
                         uint64_t advertisedSize, std::wstring& error) {
    if (!host_allowed(url)) {
        error = L"The installer URL did not match lawclient.online.";
        return false;
    }

    HttpHandle session, connection, request;
    if (!begin_request(url, session, connection, request, error)) return false;
    const std::wstring finalUrl = request_final_url(request.value);
    if (!finalUrl.empty() && !host_allowed(finalUrl)) {
        error = L"The installer download redirected outside lawclient.online.";
        return false;
    }
    uint64_t total = query_content_length(request.value);
    if (!total) total = advertisedSize;
    if (total > 350ull * 1024ull * 1024ull) {
        error = L"The installer is unexpectedly large.";
        return false;
    }

    HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not create the temporary installer file.";
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD propertySize = 0;
    bool ok = false;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ||
        !BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &propertySize, 0)) ||
        objectLength == 0) {
        error = L"Could not initialize SHA-256 verification.";
        CloseHandle(file);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        DeleteFileW(destination.c_str());
        return false;
    }
    std::vector<UCHAR> hashObject(objectLength);
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, hashObject.data(),
        static_cast<ULONG>(hashObject.size()), nullptr, 0, 0))) {
        error = L"Could not initialize SHA-256 verification.";
        CloseHandle(file);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        DeleteFileW(destination.c_str());
        return false;
    }

    std::array<UCHAR, 128 * 1024> buffer{};
    uint64_t downloaded = 0;
    post_progress(5);
    while (true) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            error = L"The installer download was interrupted.";
            break;
        }
        if (read == 0) {
            std::array<UCHAR, 32> digest{};
            if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) {
                error = L"Could not finish SHA-256 verification.";
                break;
            }
            const std::string actual = bytes_to_hex(digest);
            std::string expected = expectedSha;
            std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (actual != expected) {
                error = L"Security check failed: installer SHA-256 did not match.";
                break;
            }
            if (advertisedSize && downloaded != advertisedSize) {
                error = L"Installer size did not match the published release.";
                break;
            }
            ok = true;
            break;
        }

        DWORD written = 0;
        if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
            error = L"Could not save the installer to disk.";
            break;
        }
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) {
            error = L"Could not verify the installer while downloading.";
            break;
        }
        downloaded += read;
        if (downloaded > 350ull * 1024ull * 1024ull) {
            error = L"The installer exceeded the allowed size.";
            break;
        }
        if (total > 0) {
            int percent = 5 + static_cast<int>((downloaded * 78) / total);
            post_progress(std::clamp(percent, 5, 83));
        }
    }

    FlushFileBuffers(file);
    CloseHandle(file);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) DeleteFileW(destination.c_str());
    return ok;
}

bool run_silent_installer(const std::filesystem::path& installer,
                          const std::filesystem::path& installDirectory,
                          DWORD& exitCode,
                          std::wstring& error) {
    if (installDirectory.empty() || !installDirectory.is_absolute()) {
        error = L"Choose a valid installation folder.";
        return false;
    }

    std::wstring target = installDirectory.lexically_normal().wstring();
    while (target.size() > 3 && (target.back() == L'\\' || target.back() == L'/')) {
        target.pop_back();
    }

    // NSIS requires /D= to be the final argument and the path must not be quoted,
    // even when it contains spaces.
    std::wstring command = L"\"" + installer.wstring() + L"\" /S /D=" + target;
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(installer.c_str(), writable.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, installer.parent_path().c_str(), &startup, &process)) {
        error = L"Could not start the LawClient installer.";
        return false;
    }

    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) exitCode = 1;
    CloseHandle(process.hProcess);

    if (exitCode != 0) {
        std::wstringstream ss;
        ss << L"Setup stopped with code " << exitCode << L".";
        error = ss.str();
        return false;
    }
    return true;
}

std::filesystem::path roaming_app_data() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        std::filesystem::path result(raw);
        CoTaskMemFree(raw);
        return result;
    }

    wchar_t fallback[32768]{};
    DWORD length = GetEnvironmentVariableW(L"APPDATA", fallback, static_cast<DWORD>(std::size(fallback)));
    if (length > 0 && length < std::size(fallback)) return std::filesystem::path(fallback);
    return {};
}

std::filesystem::path default_install_directory() {
    auto root = roaming_app_data();
    if (root.empty()) return {};
    return root / L"LawClient";
}

bool choose_install_directory(HWND owner) {
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(L"Choose LawClient install folder");
    dialog->SetOkButtonLabel(L"Select Folder");

    std::filesystem::path initial = g_installDirectory;
    std::error_code existsError;
    if (initial.empty() || !std::filesystem::exists(initial, existsError) || existsError) initial = initial.parent_path();
    if (!initial.empty()) {
        IShellItem* initialItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&initialItem))) && initialItem) {
            dialog->SetFolder(initialItem);
            initialItem->Release();
        }
    }

    bool changed = false;
    hr = dialog->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result) {
            PWSTR path = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                std::filesystem::path selected(path);
                if (selected.is_absolute()) {
                    g_installDirectory = selected.lexically_normal();
                    changed = true;
                }
                CoTaskMemFree(path);
            }
            result->Release();
        }
    }

    dialog->Release();
    return changed;
}

bool launch_installed_lawclient(const std::filesystem::path& installDirectory) {
    if (installDirectory.empty()) return false;
    const std::array<std::filesystem::path, 2> candidates = {
        installDirectory / L"lawclient.exe",
        installDirectory / L"LawClient.exe",
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec) || ec) continue;
        auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", candidate.c_str(), nullptr,
            installDirectory.c_str(), SW_SHOWNORMAL));
        if (result > 32) return true;
    }
    return false;
}

std::filesystem::path temporary_installer_path() {
    wchar_t temp[MAX_PATH + 1]{};
    DWORD len = GetTempPathW(MAX_PATH, temp);
    if (!len || len > MAX_PATH) return {};
    std::wstringstream name;
    name << L"LawClientSetup-" << GetCurrentProcessId() << L".exe";
    return std::filesystem::path(temp) / name.str();
}

void install_worker(std::filesystem::path installDirectory) {
    post_status(L"Preparing setup", L"Checking installation files...");
    post_progress(2);

    std::string manifest;
    std::wstring error;
    if (!http_get_text(kManifestUrl, manifest, error)) {
        auto payload = new std::wstring(error);
        PostMessageW(g_window, WM_APP_ERROR, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    const std::string urlUtf8 = json_string(manifest, "url");
    const std::string sha = trim_ascii(json_string(manifest, "sha256"));
    const std::string versionUtf8 = trim_ascii(json_string(manifest, "version"));
    const uint64_t size = json_u64(manifest, "size");
    if (urlUtf8.empty() || !is_sha256(sha) || versionUtf8.empty()) {
        auto payload = new std::wstring(L"Installation files are temporarily unavailable.");
        PostMessageW(g_window, WM_APP_ERROR, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    const std::wstring installerUrl = utf8_to_wide(urlUtf8);
    post_status(L"Downloading LawClient", L"Downloading installation files...");

    const auto temporary = temporary_installer_path();
    if (temporary.empty()) {
        auto payload = new std::wstring(L"Windows temporary folder is unavailable.");
        PostMessageW(g_window, WM_APP_ERROR, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    DeleteFileW(temporary.c_str());
    if (!download_and_verify(installerUrl, sha, temporary, size, error)) {
        auto payload = new std::wstring(error);
        PostMessageW(g_window, WM_APP_ERROR, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    post_progress(88);
    post_status(L"Installing LawClient", L"Installing to your selected folder...");
    DWORD exitCode = 0;
    if (!run_silent_installer(temporary, installDirectory, exitCode, error)) {
        DeleteFileW(temporary.c_str());
        auto payload = new std::wstring(error);
        PostMessageW(g_window, WM_APP_ERROR, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    DeleteFileW(temporary.c_str());
    post_progress(98);
    post_status(L"Finishing setup", L"Almost done...");
    post_progress(100);
    PostMessageW(g_window, WM_APP_DONE, 0, 0);
}

void start_install() {
    if (g_busy.exchange(true)) return;
    if (g_installDirectory.empty() || !g_installDirectory.is_absolute()) {
        g_busy = false;
        g_error = true;
        g_detail = L"Choose a valid installation folder.";
        g_buttonText = L"Try Again";
        InvalidateRect(g_window, nullptr, FALSE);
        return;
    }

    g_finished = false;
    g_error = false;
    g_progress = 0;
    g_buttonText = L"Installing...";
    const auto installDirectory = g_installDirectory;
    std::thread([installDirectory]() { install_worker(installDirectory); }).detach();
}

void apply_window_effects(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const DWORD cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &cornerPreference, sizeof(cornerPreference));
    HRGN region = CreateRoundRectRgn(0, 0, kWindowWidth + 1, kWindowHeight + 1, 24, 24);
    if (region) SetWindowRgn(hwnd, region, TRUE);
}

void draw_checkmark(HDC dc, const RECT& box) {
    HPEN pen = CreatePen(PS_SOLID, 2, rgb(0xFFFFFF));
    auto oldPen = SelectObject(dc, pen);
    MoveToEx(dc, box.left + 4, box.top + 9, nullptr);
    LineTo(dc, box.left + 8, box.top + 13);
    LineTo(dc, box.left + 15, box.top + 5);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);

    // One edge-to-edge installer surface. The window region itself provides
    // the rounded outer shape, so there is no second inset card/frame.
    HBRUSH background = CreateSolidBrush(rgb(0x101319));
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT topLine{0, 0, client.right, 2};
    HBRUSH accent = CreateSolidBrush(rgb(0x2E7CF6));
    FillRect(dc, &topLine, accent);
    DeleteObject(accent);

    if (g_icon) DrawIconEx(dc, 40, 38, g_icon, 34, 34, 0, nullptr, DI_NORMAL);
    RECT brand{86, 34, 330, 61};
    draw_text(dc, L"LAWCLIENT", brand, g_fontBold, rgb(0xF4F7FB), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT brandSub{86, 57, 340, 82};
    draw_text(dc, L"WINDOWS INSTALLER", brandSub, g_fontSmall, rgb(0x77808C), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT closeGlyph = g_closeRect;
    draw_text(dc, L"x", closeGlyph, g_fontMedium, rgb(0x7A828D), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    std::wstring heading = L"Install LawClient";
    std::wstring subheading = L"Fast. Clean. Ready to launch.";
    COLORREF subColor = rgb(0x929AA6);

    if (g_busy) {
        heading = L"Setting up LawClient";
        subheading = g_status;
        subColor = rgb(0xD4D9E0);
    } else if (g_finished) {
        heading = L"LawClient is ready";
        subheading = L"Installation completed successfully.";
        subColor = rgb(0xAAB2BD);
    } else if (g_error) {
        heading = L"Setup could not finish";
        subheading = g_detail.empty() ? L"Please try again." : g_detail;
        subColor = rgb(0xAAB2BD);
    }

    RECT headingRect{40, 104, client.right - 40, 143};
    draw_text(dc, heading, headingRect, g_fontTitle, rgb(0xF7F9FC), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subheadingRect{40, 145, client.right - 40, 178};
    draw_text(dc, subheading, subheadingRect, g_fontMedium, subColor,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (!g_busy && !g_finished) {
        RECT label{40, 190, 220, 216};
        draw_text(dc, L"Install location", label, g_fontSmall, rgb(0x6F7884), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        fill_round_rect(dc, g_locationRect, 12, rgb(0x171B22));
        RECT pathRect{54, 221, 414, 261};
        draw_text(dc, g_installDirectory.wstring(), pathRect, g_fontRegular, rgb(0xAAB2BD),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
        fill_round_rect(dc, g_changeRect, 10, rgb(0x202631));
        RECT changeText = g_changeRect;
        draw_text(dc, L"Change", changeText, g_fontSmall, rgb(0xD7DCE3), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_busy) {
        RECT detailRect{40, 186, client.right - 40, 214};
        draw_text(dc, g_detail, detailRect, g_fontRegular, rgb(0x6F7884),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT track{40, 230, client.right - 40, 240};
        fill_round_rect(dc, track, 10, rgb(0x202631));
        int width = track.right - track.left;
        int progress = std::clamp(g_progress.load(), 0, 100);
        if (progress > 0) {
            RECT bar = track;
            bar.right = bar.left + std::max(10, (width * progress) / 100);
            fill_round_rect(dc, bar, 10, rgb(0x2E7CF6));
        }
        RECT percent{client.right - 95, 247, client.right - 40, 273};
        draw_text(dc, std::to_wstring(progress) + L"%", percent, g_fontSmall,
            rgb(0x7F8A98), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_finished) {
        RECT box{40, 218, 58, 236};
        fill_round_rect(dc, box, 5, g_openAfterInstall ? rgb(0x2E7CF6) : rgb(0x202631));
        if (g_openAfterInstall) draw_checkmark(dc, box);
        RECT openLabel{68, 211, 260, 244};
        draw_text(dc, L"Open LawClient", openLabel, g_fontRegular, rgb(0xCDD3DB),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    COLORREF buttonColor = rgb(0x2E7CF6);
    COLORREF buttonTextColor = rgb(0xFFFFFF);
    if (g_busy) {
        buttonColor = rgb(0x181D25);
        buttonTextColor = rgb(0x67717E);
    }
    fill_round_rect(dc, g_buttonRect, 16, buttonColor);
    RECT buttonText = g_buttonRect;
    draw_text(dc, g_buttonText, buttonText, g_fontBold, buttonTextColor,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            apply_window_effects(hwnd);
            return 0;

        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PtInRect(&g_closeRect, point)) {
                if (!g_busy) DestroyWindow(hwnd);
                return 0;
            }

            if (!g_busy && !g_finished && PtInRect(&g_changeRect, point)) {
                if (choose_install_directory(hwnd)) {
                    g_error = false;
                    g_buttonText = L"Install LawClient";
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }

            if (g_finished && PtInRect(&g_openCheckboxHitRect, point)) {
                g_openAfterInstall = !g_openAfterInstall;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (PtInRect(&g_buttonRect, point)) {
                if (g_finished) {
                    if (g_openAfterInstall) launch_installed_lawclient(g_installDirectory);
                    DestroyWindow(hwnd);
                } else if (!g_busy) {
                    start_install();
                }
                return 0;
            }

            if (point.y < 94) {
                g_dragging = true;
                g_dragOrigin = point;
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (g_dragging && (wParam & MK_LBUTTON)) {
                POINT cursor{};
                GetCursorPos(&cursor);
                RECT rect{};
                GetWindowRect(hwnd, &rect);
                SetWindowPos(hwnd, nullptr, cursor.x - g_dragOrigin.x, cursor.y - g_dragOrigin.y,
                    0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (g_dragging) {
                g_dragging = false;
                ReleaseCapture();
            }
            return 0;

        case WM_APP_STATUS: {
            auto payload = reinterpret_cast<std::pair<std::wstring, std::wstring>*>(lParam);
            if (payload) {
                g_status = payload->first;
                g_detail = payload->second;
                delete payload;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_APP_PROGRESS:
            g_progress = static_cast<int>(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_APP_DONE:
            g_finished = true;
            g_busy = false;
            g_error = false;
            g_status = L"Installed successfully";
            g_detail.clear();
            g_buttonText = L"Done";
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_APP_ERROR: {
            auto payload = reinterpret_cast<std::wstring*>(lParam);
            g_busy = false;
            g_finished = false;
            g_error = true;
            g_progress = 0;
            g_status = L"Installation failed";
            g_detail = payload ? *payload : L"An unexpected error occurred.";
            g_buttonText = L"Try Again";
            delete payload;
            InvalidateRect(hwnd, nullptr, FALSE);
            MessageBeep(MB_ICONERROR);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint(hwnd);
            return 0;
        case WM_CLOSE:
            if (!g_busy) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

HFONT make_font(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_fontRegular = make_font(14, FW_NORMAL);
    g_fontSmall = make_font(13, FW_MEDIUM);
    g_fontMedium = make_font(15, FW_SEMIBOLD);
    g_fontBold = make_font(15, FW_BOLD);
    g_fontTitle = make_font(25, FW_SEMIBOLD);
    g_icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    g_installDirectory = default_install_directory();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hIcon = g_icon;
    wc.hIconSm = g_icon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - kWindowWidth) / 2;
    int y = work.top + ((work.bottom - work.top) - kWindowHeight) / 2;

    g_window = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, kWindowTitle,
        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x, y, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;

    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_fontRegular) DeleteObject(g_fontRegular);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    if (g_fontMedium) DeleteObject(g_fontMedium);
    if (g_fontBold) DeleteObject(g_fontBold);
    if (g_fontTitle) DeleteObject(g_fontTitle);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
