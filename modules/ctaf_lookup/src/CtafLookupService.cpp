#include "XVatsim/modules/ctaf_lookup/CtafLookupService.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

namespace xvatsim::modules::ctaf_lookup {

namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/1.0.2";
constexpr wchar_t kAipHost[] = L"my.vatsim.net";
constexpr long long kLookupRetryCadenceSeconds = 15;
constexpr long long kLookupFailureBackoffSeconds = 60;
constexpr int kHttpResolveTimeoutMs = 1000;
constexpr int kHttpConnectTimeoutMs = 1500;
constexpr int kHttpSendTimeoutMs = 1500;
constexpr int kHttpReceiveTimeoutMs = 2500;
constexpr std::size_t kMaxStationsPayloadBytes = 256 * 1024;

struct WinHttpHandle {
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle(handle) {}
    ~WinHttpHandle() {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET handle = nullptr;
};

struct DownloadStationsResult {
    bool requestSucceeded = false;
    DWORD statusCode = 0;
    std::string payload;
};

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string NormalizeIcao(std::string airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());

    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }

    return normalized;
}

bool TryParseManualCtafCommand(
    const std::string& commandText,
    std::string* outAirportIcao) {
    std::istringstream stream(commandText);
    std::string command;
    std::string airportIcao;
    std::string extraToken;
    stream >> command >> airportIcao >> extraToken;

    if (command.empty() || airportIcao.empty() || !extraToken.empty()) {
        return false;
    }

    if (!command.empty() && command.front() == '.') {
        command.erase(command.begin());
    }

    std::transform(
        command.begin(),
        command.end(),
        command.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (command != "ctaf") {
        return false;
    }

    const auto normalizedIcao = NormalizeIcao(airportIcao);
    if (normalizedIcao.size() < 3 || normalizedIcao.size() > 4) {
        return false;
    }

    *outAirportIcao = normalizedIcao;
    return true;
}

DownloadStationsResult DownloadStationsJson(const std::string& airportIcao) {
    DownloadStationsResult result;
    std::wstring path = L"/api/v2/aip/airports/";
    path += std::wstring(airportIcao.begin(), airportIcao.end());
    path += L"/stations";

    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.handle == nullptr) {
        return result;
    }
    if (!WinHttpSetTimeouts(
            session.handle,
            kHttpResolveTimeoutMs,
            kHttpConnectTimeoutMs,
            kHttpSendTimeoutMs,
            kHttpReceiveTimeoutMs)) {
        return result;
    }

    WinHttpHandle connection(WinHttpConnect(session.handle, kAipHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (connection.handle == nullptr) {
        return result;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.handle,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (request.handle == nullptr) {
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
        return result;
    }

    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
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
        return result;
    }

    result.requestSucceeded = true;
    result.statusCode = statusCode;

    std::string payload;
    for (;;) {
        DWORD availableBytes = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &availableBytes)) {
            result.requestSucceeded = false;
            result.payload.clear();
            return result;
        }

        if (availableBytes == 0) {
            break;
        }

        if (payload.size() + availableBytes > kMaxStationsPayloadBytes) {
            result.requestSucceeded = false;
            result.payload.clear();
            return result;
        }

        std::vector<char> buffer(availableBytes);
        DWORD downloadedBytes = 0;
        if (!WinHttpReadData(
                request.handle,
                buffer.data(),
                availableBytes,
                &downloadedBytes)) {
            result.requestSucceeded = false;
            result.payload.clear();
            return result;
        }

        if (payload.size() + downloadedBytes > kMaxStationsPayloadBytes) {
            result.requestSucceeded = false;
            result.payload.clear();
            return result;
        }

        payload.append(buffer.data(), downloadedBytes);
    }

    result.payload = std::move(payload);
    return result;
}

}  // namespace

CtafLookupService::~CtafLookupService() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
}

void CtafLookupService::Reset() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    cache_.clear();
    pendingFetchAirportIcao_.clear();
    pendingFetchEntry_ = {};
    hasPendingFetchEntry_ = false;
    fetchInProgress_ = false;
}

CtafLookupEntry CtafLookupService::Lookup(const std::string& airportIcao) {
    HarvestPendingFetch();

    const auto normalizedIcao = NormalizeIcao(airportIcao);
    if (normalizedIcao.empty()) {
        return {};
    }

    const auto nowSeconds = CurrentTickSeconds();
    auto& cacheEntry = cache_[normalizedIcao];
    if (cacheEntry.resolved) {
        return cacheEntry;
    }

    const auto retryCadenceSeconds =
        cacheEntry.failureCount > 0 ? kLookupFailureBackoffSeconds : kLookupRetryCadenceSeconds;
    if ((nowSeconds - cacheEntry.lastAttemptTickSeconds) < retryCadenceSeconds &&
        cacheEntry.lastAttemptTickSeconds != 0) {
        return cacheEntry;
    }

    if (!fetchInProgress_) {
        if (StartAsyncFetch(normalizedIcao, nowSeconds)) {
            cacheEntry.lastAttemptTickSeconds = nowSeconds;
        } else {
            cacheEntry.failureCount += 1;
            cacheEntry.lastAttemptTickSeconds = nowSeconds;
        }
    }

    return cacheEntry;
}

brain::ManualQuerySnapshot CtafLookupService::RunManualCtafQuery(
    const std::string& commandText) {
    brain::ManualQuerySnapshot snapshot;
    snapshot.visible = true;

    std::string airportIcao;
    if (!TryParseManualCtafCommand(commandText, &airportIcao)) {
        snapshot.line = "CTAF usage .ctaf KSEA";
        return snapshot;
    }

    const auto ctaf = LookupSync(airportIcao);
    cache_[airportIcao] = ctaf;
    if (ctaf.available) {
        snapshot.line = "CTAF " + airportIcao + " " + ctaf.frequency;
        return snapshot;
    }

    if (ctaf.resolved) {
        snapshot.line = "NO CTAF / UNICOM 122.800";
        return snapshot;
    }

    snapshot.line = "CTAF " + airportIcao + " lookup failed";
    return snapshot;
}

CtafLookupEntry CtafLookupService::LookupSync(const std::string& airportIcao) {
    const auto normalizedIcao = NormalizeIcao(airportIcao);
    if (normalizedIcao.empty()) {
        return {};
    }

    static const bool kWinRtInitialized = []() {
        try {
            winrt::init_apartment();
        } catch (...) {
        }
        return true;
    }();
    (void)kWinRtInitialized;

    CtafLookupEntry entry;
    entry.lastAttemptTickSeconds = CurrentTickSeconds();

    const auto response = DownloadStationsJson(normalizedIcao);
    if (!response.requestSucceeded) {
        entry.failureCount = 1;
        return entry;
    }

    if (response.statusCode == 404) {
        entry.resolved = true;
        entry.failureCount = 0;
        return entry;
    }

    if (response.statusCode != 200 || response.payload.empty()) {
        entry.failureCount = 1;
        return entry;
    }

    try {
        const auto root = winrt::Windows::Data::Json::JsonObject::Parse(
            winrt::to_hstring(response.payload));
        const auto stations = root.GetNamedArray(
            L"data",
            winrt::Windows::Data::Json::JsonArray{});
        entry.resolved = true;
        entry.failureCount = 0;

        for (uint32_t index = 0; index < stations.Size(); ++index) {
            const auto station = stations.GetObjectAt(index);
            if (!station.GetNamedBoolean(L"ctaf", false)) {
                continue;
            }

            entry.available = true;
            entry.frequency = winrt::to_string(
                station.GetNamedString(L"frequency", L""));
            break;
        }
    } catch (...) {
        entry.failureCount = 1;
        return entry;
    }

    return entry;
}

bool CtafLookupService::StartAsyncFetch(
    const std::string& airportIcao,
    long long requestTickSeconds) {
    if (fetchInProgress_) {
        return false;
    }

    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        pendingFetchAirportIcao_.clear();
        pendingFetchEntry_ = {};
        hasPendingFetchEntry_ = false;
    }

    fetchInProgress_ = true;
    try {
        fetchThread_ = std::thread([this, airportIcao, requestTickSeconds]() {
            CtafLookupEntry entry;
            try {
                entry = LookupSync(airportIcao);
            } catch (...) {
                entry.failureCount = 1;
            }
            entry.lastAttemptTickSeconds = requestTickSeconds;
            {
                std::lock_guard<std::mutex> lock(fetchMutex_);
                pendingFetchAirportIcao_ = airportIcao;
                pendingFetchEntry_ = entry;
                hasPendingFetchEntry_ = true;
            }
            fetchInProgress_ = false;
        });
    } catch (...) {
        fetchInProgress_ = false;
        return false;
    }

    return true;
}

void CtafLookupService::HarvestPendingFetch() {
    if (fetchInProgress_) {
        return;
    }

    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    if (!hasPendingFetchEntry_) {
        return;
    }

    cache_[pendingFetchAirportIcao_] = pendingFetchEntry_;
    hasPendingFetchEntry_ = false;
}

}  // namespace xvatsim::modules::ctaf_lookup
