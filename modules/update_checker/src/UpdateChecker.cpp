#include "XVatsim/modules/update_checker/UpdateChecker.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

namespace xvatsim::modules::update_checker {

namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/1.0.3 UpdateChecker";
constexpr int kHttpResolveTimeoutMs = 2500;
constexpr int kHttpConnectTimeoutMs = 2500;
constexpr int kHttpSendTimeoutMs = 2500;
constexpr int kHttpReceiveTimeoutMs = 5000;
constexpr std::size_t kMaxManifestPayloadBytes = 64 * 1024;
constexpr std::size_t kMaxTextFieldChars = 4096;

struct WinHttpHandle {
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET value) : handle(value) {}
    ~WinHttpHandle() {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET handle = nullptr;
};

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
};

struct FetchResult {
    bool ok = false;
    std::string payload;
    std::string errorClass;
    int statusCode = 0;
};

std::string Trim(std::string value) {
    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isspace(c) == 0; }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [](unsigned char c) { return std::isspace(c) == 0; })
            .base(),
        value.end());
    return value;
}

std::string ToLowerCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string SanitizeField(std::string value) {
    value = Trim(std::move(value));
    value.erase(
        std::remove(value.begin(), value.end(), '\0'),
        value.end());
    for (auto& character : value) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    if (value.size() > kMaxTextFieldChars) {
        value.resize(kMaxTextFieldChars);
    }
    return value;
}

bool StartsWithHttps(const std::string& value) {
    const auto lowered = ToLowerCopy(Trim(value));
    return lowered.rfind("https://", 0) == 0;
}

std::wstring WideFromUtf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }

    std::wstring wideValue(static_cast<std::size_t>(required), L'\0');
    const auto written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        wideValue.data(),
        required);
    if (written != required) {
        return {};
    }
    return wideValue;
}

std::optional<ParsedUrl> ParseHttpsUrl(const std::string& url) {
    if (!StartsWithHttps(url)) {
        return std::nullopt;
    }

    const auto wideUrl = WideFromUtf8(url);
    if (wideUrl.empty()) {
        return std::nullopt;
    }

    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    wchar_t extra[2048] = {};
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));

    if (!WinHttpCrackUrl(
            wideUrl.c_str(),
            static_cast<DWORD>(wideUrl.size()),
            0,
            &components)) {
        return std::nullopt;
    }

    if (components.nScheme != INTERNET_SCHEME_HTTPS ||
        components.dwHostNameLength == 0) {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.host.assign(host, components.dwHostNameLength);
    parsed.path.assign(path, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        parsed.path.append(extra, components.dwExtraInfoLength);
    }
    if (parsed.path.empty()) {
        parsed.path = L"/";
    }
    parsed.port = components.nPort == 0
        ? INTERNET_DEFAULT_HTTPS_PORT
        : components.nPort;
    return parsed;
}

FetchResult DownloadManifest(const std::string& manifestUrl) {
    FetchResult result;
    const auto parsedUrl = ParseHttpsUrl(manifestUrl);
    if (!parsedUrl.has_value()) {
        result.errorClass = "invalid-url";
        return result;
    }

    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.handle == nullptr) {
        result.errorClass = "http-open-failed";
        return result;
    }
    if (!WinHttpSetTimeouts(
            session.handle,
            kHttpResolveTimeoutMs,
            kHttpConnectTimeoutMs,
            kHttpSendTimeoutMs,
            kHttpReceiveTimeoutMs)) {
        result.errorClass = "http-timeout-config-failed";
        return result;
    }

    WinHttpHandle connection(WinHttpConnect(
        session.handle,
        parsedUrl->host.c_str(),
        parsedUrl->port,
        0));
    if (connection.handle == nullptr) {
        result.errorClass = "http-connect-failed";
        return result;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.handle,
        L"GET",
        parsedUrl->path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (request.handle == nullptr) {
        result.errorClass = "http-request-open-failed";
        return result;
    }

    if (!WinHttpSendRequest(
            request.handle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        result.errorClass = "http-send-failed";
        return result;
    }

    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
        result.errorClass = "http-response-failed";
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            request.handle,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX)) {
        result.errorClass = "http-status-unavailable";
        return result;
    }
    result.statusCode = static_cast<int>(statusCode);
    if (statusCode != 200) {
        result.errorClass = "http-status-" + std::to_string(statusCode);
        return result;
    }

    std::string payload;
    for (;;) {
        DWORD availableBytes = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &availableBytes)) {
            result.errorClass = "http-read-available-failed";
            return result;
        }
        if (availableBytes == 0) {
            break;
        }
        if (payload.size() + static_cast<std::size_t>(availableBytes) >
            kMaxManifestPayloadBytes) {
            result.errorClass = "payload-too-large";
            return result;
        }

        std::vector<char> buffer(availableBytes);
        DWORD downloadedBytes = 0;
        if (!WinHttpReadData(
                request.handle,
                buffer.data(),
                availableBytes,
                &downloadedBytes)) {
            result.errorClass = "http-read-failed";
            return result;
        }
        if (payload.size() + static_cast<std::size_t>(downloadedBytes) >
            kMaxManifestPayloadBytes) {
            result.errorClass = "payload-too-large";
            return result;
        }
        payload.append(buffer.data(), downloadedBytes);
    }

    result.ok = true;
    result.payload = std::move(payload);
    return result;
}

std::optional<std::vector<int>> ParseVersion(const std::string& version) {
    const auto trimmedVersion = Trim(version);
    if (trimmedVersion.empty()) {
        return std::nullopt;
    }

    std::vector<int> parts;
    std::string current;
    for (const auto character : trimmedVersion) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            current.push_back(character);
            if (current.size() > 6) {
                return std::nullopt;
            }
            continue;
        }
        if (character != '.' || current.empty()) {
            return std::nullopt;
        }
        parts.push_back(std::stoi(current));
        current.clear();
        if (parts.size() > 4) {
            return std::nullopt;
        }
    }
    if (current.empty()) {
        return std::nullopt;
    }
    parts.push_back(std::stoi(current));
    if (parts.empty() || parts.size() > 4) {
        return std::nullopt;
    }
    return parts;
}

int CompareVersions(
    const std::vector<int>& left,
    const std::vector<int>& right) {
    const auto count = std::max(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto leftPart = index < left.size() ? left[index] : 0;
        const auto rightPart = index < right.size() ? right[index] : 0;
        if (leftPart < rightPart) {
            return -1;
        }
        if (leftPart > rightPart) {
            return 1;
        }
    }
    return 0;
}

UpdateCheckResult FailedResult(
    const UpdateCheckRequest& request,
    std::string errorClass) {
    UpdateCheckResult result;
    result.status = UpdateStatus::CheckFailed;
    result.source = request.source;
    result.installedVersion = request.installedVersion;
    result.manifestUrl = request.manifestUrl;
    result.errorClass = std::move(errorClass);
    return result;
}

bool TryGetString(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    std::string* outValue,
    bool required) {
    if (outValue == nullptr) {
        return false;
    }
    const auto hKey = winrt::hstring(key);
    if (!object.HasKey(hKey)) {
        return !required;
    }
    const auto value = object.Lookup(hKey);
    if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) {
        return false;
    }
    *outValue = SanitizeField(winrt::to_string(value.GetString()));
    return true;
}

bool TryGetBool(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    bool* outValue,
    bool required) {
    if (outValue == nullptr) {
        return false;
    }
    const auto hKey = winrt::hstring(key);
    if (!object.HasKey(hKey)) {
        return !required;
    }
    const auto value = object.Lookup(hKey);
    if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Boolean) {
        return false;
    }
    *outValue = value.GetBoolean();
    return true;
}

bool TryGetReleaseNotes(
    const winrt::Windows::Data::Json::JsonObject& object,
    std::string* outValue) {
    if (outValue == nullptr) {
        return false;
    }
    const auto key = winrt::hstring(L"release_notes");
    if (!object.HasKey(key)) {
        return true;
    }
    const auto value = object.Lookup(key);
    if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Array) {
        return false;
    }
    const auto array = value.GetArray();
    std::ostringstream stream;
    for (uint32_t index = 0; index < array.Size(); ++index) {
        const auto item = array.GetAt(index);
        if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) {
            return false;
        }
        if (index > 0) {
            stream << " | ";
        }
        stream << SanitizeField(winrt::to_string(item.GetString()));
        if (stream.str().size() > kMaxTextFieldChars) {
            break;
        }
    }
    *outValue = SanitizeField(stream.str());
    return true;
}

UpdateCheckResult FetchAndEvaluate(const UpdateCheckRequest& request) {
    const auto fetchResult = DownloadManifest(request.manifestUrl);
    if (!fetchResult.ok) {
        auto result = FailedResult(request, fetchResult.errorClass);
        if (fetchResult.statusCode > 0) {
            result.errorClass += ":" + std::to_string(fetchResult.statusCode);
        }
        return result;
    }
    return EvaluateUpdateManifestPayload(request, fetchResult.payload);
}

}  // namespace

const char* ToString(UpdateCheckSource source) {
    switch (source) {
        case UpdateCheckSource::Automatic:
            return "automatic";
        case UpdateCheckSource::Manual:
            return "manual";
    }
    return "automatic";
}

const char* ToString(UpdateStatus status) {
    switch (status) {
        case UpdateStatus::Unknown:
            return "unknown";
        case UpdateStatus::Current:
            return "current";
        case UpdateStatus::Available:
            return "available";
        case UpdateStatus::CheckFailed:
            return "check-failed";
        case UpdateStatus::InProgress:
            return "in-progress";
    }
    return "unknown";
}

UpdateCheckResult EvaluateUpdateManifestPayload(
    const UpdateCheckRequest& request,
    const std::string& payload) {
    if (request.installedVersion.empty()) {
        return FailedResult(request, "missing-installed-version");
    }
    if (request.manifestUrl.empty()) {
        return FailedResult(request, "missing-manifest-url");
    }
    if (payload.empty() || payload.size() > kMaxManifestPayloadBytes) {
        return FailedResult(request, "invalid-payload");
    }

    winrt::Windows::Data::Json::JsonObject object = nullptr;
    if (!winrt::Windows::Data::Json::JsonObject::TryParse(
            winrt::to_hstring(payload),
            object)) {
        return FailedResult(request, "json-parse");
    }

    UpdateCheckResult result;
    result.source = request.source;
    result.installedVersion = request.installedVersion;
    result.manifestUrl = request.manifestUrl;

    std::string product;
    if (!TryGetString(object, L"product", &product, false)) {
        return FailedResult(request, "malformed-product");
    }
    if (!product.empty() && product != "XVatsim") {
        return FailedResult(request, "wrong-product");
    }

    std::string updatePolicy;
    if (!TryGetString(object, L"update_policy", &updatePolicy, false)) {
        return FailedResult(request, "malformed-update-policy");
    }
    if (!updatePolicy.empty() && ToLowerCopy(updatePolicy) != "notify_only") {
        return FailedResult(request, "unsupported-update-policy");
    }

    if (!TryGetString(object, L"latest_version", &result.latestVersion, true)) {
        return FailedResult(request, "missing-or-malformed-latest-version");
    }
    if (!TryGetString(
            object,
            L"minimum_supported_version",
            &result.minimumSupportedVersion,
            false)) {
        return FailedResult(request, "malformed-minimum-supported-version");
    }
    if (!TryGetString(object, L"package_sha256", &result.packageSha256, false)) {
        return FailedResult(request, "malformed-package-sha256");
    }
    if (!TryGetString(object, L"plugin_sha256", &result.pluginSha256, false)) {
        return FailedResult(request, "malformed-plugin-sha256");
    }
    if (!TryGetString(object, L"download_page_url", &result.downloadPageUrl, true)) {
        return FailedResult(request, "missing-or-malformed-download-page-url");
    }
    if (!TryGetBool(object, L"critical", &result.critical, false)) {
        return FailedResult(request, "malformed-critical");
    }
    if (!TryGetString(object, L"message", &result.message, false)) {
        return FailedResult(request, "malformed-message");
    }
    if (!TryGetReleaseNotes(object, &result.releaseNotes)) {
        return FailedResult(request, "malformed-release-notes");
    }

    if (!StartsWithHttps(result.downloadPageUrl)) {
        return FailedResult(request, "invalid-download-page-url");
    }

    const auto installedVersion = ParseVersion(request.installedVersion);
    const auto latestVersion = ParseVersion(result.latestVersion);
    if (!installedVersion.has_value() || !latestVersion.has_value()) {
        return FailedResult(request, "malformed-version");
    }
    if (!result.minimumSupportedVersion.empty() &&
        !ParseVersion(result.minimumSupportedVersion).has_value()) {
        return FailedResult(request, "malformed-minimum-supported-version");
    }

    result.validManifest = true;
    result.updateAvailable =
        CompareVersions(*installedVersion, *latestVersion) < 0;
    result.status =
        result.updateAvailable ? UpdateStatus::Available : UpdateStatus::Current;
    result.errorClass = "none";
    return result;
}

UpdateChecker::~UpdateChecker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool UpdateChecker::StartCheck(const UpdateCheckRequest& request) {
    if (worker_.joinable()) {
        bool shouldJoin = false;
        bool hasUnharvestedResult = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shouldJoin = !inProgress_;
            hasUnharvestedResult = resultReady_;
        }
        if (hasUnharvestedResult) {
            return false;
        }
        if (shouldJoin) {
            worker_.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inProgress_ || resultReady_) {
            return false;
        }
        inProgress_ = true;
        resultReady_ = false;
        result_ = {};
    }

    worker_ = std::thread([this, request]() {
        auto result = FetchAndEvaluate(request);
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = std::move(result);
        resultReady_ = true;
        inProgress_ = false;
    });
    return true;
}

std::optional<UpdateCheckResult> UpdateChecker::HarvestResult() {
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready = resultReady_;
    }
    if (!ready) {
        return std::nullopt;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!resultReady_) {
        return std::nullopt;
    }
    resultReady_ = false;
    return result_;
}

bool UpdateChecker::InProgress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inProgress_;
}

}  // namespace xvatsim::modules::update_checker
