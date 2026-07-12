#include "XVatsim/modules/terminal_authority/TerminalAuthorityResolver.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iterator>
#include <sstream>
#include <utility>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace xvatsim::modules::terminal_authority {
namespace {

constexpr const wchar_t* kUserAgent = L"XVatsim/1.2.2";
constexpr const wchar_t* kTerminalBoundaryUrl =
    L"https://github.com/vatsimnetwork/simaware-tracon-project/releases/latest/download/TRACONBoundaries.geojson";
constexpr long long kRefreshCadenceSeconds = 6 * 60 * 60;
constexpr long long kFailureBackoffSeconds = 60;
constexpr std::size_t kMaxTerminalPayloadBytes = 16 * 1024 * 1024;
constexpr int kMaxRedirects = 5;

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

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

long long ElapsedMicrosecondsSince(
    const std::chrono::steady_clock::time_point& started) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - started)
        .count();
}

std::string NormalizeToken(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        } else if (!normalized.empty() && normalized.back() != '_') {
            normalized.push_back('_');
        }
    }
    while (!normalized.empty() && normalized.back() == '_') {
        normalized.pop_back();
    }
    return normalized;
}

bool IsTerminalRoleSuffix(const std::string& suffix) {
    return suffix == "APP" || suffix == "DEP";
}

std::vector<std::string> BuildAirportTokens(const std::string& airportIcao) {
    std::vector<std::string> tokens;
    const auto airport = NormalizeToken(airportIcao);
    if (!airport.empty()) {
        tokens.push_back(airport);
    }
    if (airport.size() == 4 && (airport[0] == 'K' || airport[0] == 'P')) {
        tokens.push_back(airport.substr(1));
    }
    return tokens;
}

std::string TerminalRoleSuffixFromToken(const std::string& value) {
    const auto token = NormalizeToken(value);
    if (token.empty()) {
        return {};
    }

    const auto lastSeparator = token.rfind('_');
    if (lastSeparator == std::string::npos ||
        lastSeparator >= token.size() - 1) {
        return {};
    }

    const auto suffix = token.substr(lastSeparator + 1);
    return IsTerminalRoleSuffix(suffix) ? suffix : std::string{};
}

void AppendUnique(std::vector<std::string>* values, std::string value) {
    if (values == nullptr || value.empty()) {
        return;
    }
    value = NormalizeToken(std::move(value));
    if (value.empty() ||
        std::find(values->begin(), values->end(), value) != values->end()) {
        return;
    }
    values->push_back(std::move(value));
}

std::string TerminalServiceToken(
    const std::string& terminalPrefix,
    const std::string& roleSuffix) {
    const auto prefix = NormalizeToken(terminalPrefix);
    const auto suffix = NormalizeToken(roleSuffix);
    if (prefix.empty() || !IsTerminalRoleSuffix(suffix)) {
        return {};
    }
    return prefix + "_" + suffix;
}

std::string TerminalServiceTokenFromLookupKey(const std::string& value) {
    const auto token = NormalizeToken(value);
    return TerminalRoleSuffixFromToken(token).empty() ? std::string{} : token;
}

std::string ExplicitSimAwareRoleSuffixFromSourceRecord(
    const core::authority::AuthorityPolygon& polygon) {
    if (polygon.source != core::authority::AuthoritySource::SimAwareTracon ||
        polygon.sourceRecord.empty()) {
        return {};
    }

    try {
        const auto feature =
            winrt::Windows::Data::Json::JsonObject::Parse(
                winrt::to_hstring(polygon.sourceRecord));
        const auto properties = feature.GetNamedObject(
            L"properties",
            winrt::Windows::Data::Json::JsonObject{});
        if (!properties.HasKey(L"suffix")) {
            return {};
        }
        const auto suffix = NormalizeToken(
            winrt::to_string(properties.GetNamedString(L"suffix", L"")));
        return IsTerminalRoleSuffix(suffix) ? suffix : std::string{};
    } catch (...) {
        return {};
    }
}

bool SimAwarePolygonIsSharedAppDep(
    const core::authority::AuthorityPolygon& polygon) {
    return polygon.source == core::authority::AuthoritySource::SimAwareTracon &&
           !polygon.sourceRecord.empty() &&
           ExplicitSimAwareRoleSuffixFromSourceRecord(polygon).empty();
}

std::string TerminalServiceBase(const std::string& serviceToken);

std::vector<std::string> TerminalServiceTokensFromLookupKey(
    const std::string& value,
    bool sharedAppDep) {
    std::vector<std::string> tokens;
    const auto token = TerminalServiceTokenFromLookupKey(value);
    if (token.empty()) {
        return tokens;
    }

    AppendUnique(&tokens, token);
    if (sharedAppDep) {
        const auto base = TerminalServiceBase(token);
        const auto suffix = TerminalRoleSuffixFromToken(token);
        if (!base.empty() && suffix == "APP") {
            AppendUnique(&tokens, TerminalServiceToken(base, "DEP"));
        } else if (!base.empty() && suffix == "DEP") {
            AppendUnique(&tokens, TerminalServiceToken(base, "APP"));
        }
    }
    return tokens;
}

std::string TerminalServiceBase(const std::string& serviceToken) {
    const auto token = NormalizeToken(serviceToken);
    const auto lastSeparator = token.rfind('_');
    if (lastSeparator == std::string::npos ||
        lastSeparator >= token.size() - 1 ||
        !IsTerminalRoleSuffix(token.substr(lastSeparator + 1))) {
        return {};
    }
    return token.substr(0, lastSeparator);
}

std::string TerminalServiceRoot(const std::string& serviceToken) {
    const auto base = TerminalServiceBase(serviceToken);
    const auto firstSeparator = base.find('_');
    return firstSeparator == std::string::npos
               ? base
               : base.substr(0, firstSeparator);
}

std::vector<std::string> TerminalRoleSuffixesFromPolygon(
    const core::authority::AuthorityPolygon& polygon) {
    if (polygon.source == core::authority::AuthoritySource::SimAwareTracon &&
        !polygon.sourceRecord.empty()) {
        const auto explicitSuffix =
            ExplicitSimAwareRoleSuffixFromSourceRecord(polygon);
        if (!explicitSuffix.empty()) {
            return {explicitSuffix};
        }
        return {"APP", "DEP"};
    }

    auto suffix = TerminalRoleSuffixFromToken(polygon.id);
    if (!suffix.empty()) {
        return {suffix};
    }
    for (const auto& lookupKey : polygon.lookupKeys) {
        suffix = TerminalRoleSuffixFromToken(lookupKey);
        if (!suffix.empty()) {
            return {suffix};
        }
    }

    return {"APP"};
}

bool TerminalServiceMatchesAirportTokens(
    const std::string& serviceToken,
    const std::vector<std::string>& airportTokens) {
    const auto base = TerminalServiceBase(serviceToken);
    if (base.empty()) {
        return false;
    }

    return std::any_of(
        airportTokens.begin(),
        airportTokens.end(),
        [&](const auto& token) {
            return base == token ||
                   (base.size() > token.size() &&
                    base.compare(0, token.size(), token) == 0 &&
                    (base[token.size()] == '_' ||
                     base[token.size()] == '-'));
        });
}

bool IsSharedTerminalOwnerKey(const std::string& rawKey) {
    const auto key = NormalizeToken(rawKey);
    return !key.empty() && key.find('_') == std::string::npos;
}

bool TerminalServiceMatchesSharedPolygonKey(
    const std::string& serviceToken,
    const std::string& polygonKey) {
    const auto key = NormalizeToken(polygonKey);
    if (!IsSharedTerminalOwnerKey(key)) {
        return false;
    }

    const auto base = TerminalServiceBase(serviceToken);
    const auto root = TerminalServiceRoot(serviceToken);
    return base == key || root == key;
}

double UnwrapLongitudeRelativeDeg(double referenceLongitudeDeg, double longitudeDeg) {
    auto delta = longitudeDeg - referenceLongitudeDeg;
    while (delta > 180.0) {
        delta -= 360.0;
    }
    while (delta < -180.0) {
        delta += 360.0;
    }
    return referenceLongitudeDeg + delta;
}

bool PointInRing(
    double latitudeDeg,
    double longitudeDeg,
    const core::authority::AuthorityPolygonRing& ring) {
    if (ring.points.size() < 3) {
        return false;
    }

    bool inside = false;
    std::size_t previousIndex = ring.points.size() - 1;
    for (std::size_t index = 0; index < ring.points.size(); ++index) {
        const auto& current = ring.points[index];
        const auto& previous = ring.points[previousIndex];
        const auto currentLon =
            UnwrapLongitudeRelativeDeg(longitudeDeg, current.longitudeDeg);
        const auto previousLon =
            UnwrapLongitudeRelativeDeg(longitudeDeg, previous.longitudeDeg);
        const auto currentLat = current.latitudeDeg;
        const auto previousLat = previous.latitudeDeg;

        const auto crossesLatitude =
            (currentLat > latitudeDeg) != (previousLat > latitudeDeg);
        if (crossesLatitude) {
            const auto denominator = previousLat - currentLat;
            if (std::fabs(denominator) > 1e-12) {
                const auto intersectLon =
                    (previousLon - currentLon) *
                        (latitudeDeg - currentLat) / denominator +
                    currentLon;
                if (longitudeDeg < intersectLon) {
                    inside = !inside;
                }
            }
        }
        previousIndex = index;
    }
    return inside;
}

bool PointInPolygon(
    double latitudeDeg,
    double longitudeDeg,
    const core::authority::AuthorityPolygon& polygon) {
    if (polygon.rings.empty()) {
        return false;
    }
    if (!PointInRing(latitudeDeg, longitudeDeg, polygon.rings.front())) {
        return false;
    }
    for (std::size_t index = 1; index < polygon.rings.size(); ++index) {
        if (PointInRing(latitudeDeg, longitudeDeg, polygon.rings[index])) {
            return false;
        }
    }
    return true;
}

std::string BuildAirportCacheKey(
    const brain::BrainTerminalAuthorityWorkerInput& input,
    std::uint64_t generation) {
    std::ostringstream stream;
    stream << NormalizeToken(input.airportIcao) << '|'
           << generation << '|'
           << std::llround(input.airportLatitudeDeg * 100000.0) << '|'
           << std::llround(input.airportLongitudeDeg * 100000.0);
    return stream.str();
}

std::wstring QueryRedirectLocation(HINTERNET request) {
    DWORD locationSize = 0;
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_LOCATION,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        &locationSize,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        locationSize == 0) {
        return {};
    }

    std::wstring location(locationSize / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            location.data(),
            &locationSize,
            WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    if (!location.empty() && location.back() == L'\0') {
        location.pop_back();
    }
    return location;
}

std::string DownloadHttpsPayload(const wchar_t* initialUrl) {
    std::wstring currentUrl(initialUrl == nullptr ? L"" : initialUrl);
    for (int redirectCount = 0;
         redirectCount <= kMaxRedirects && !currentUrl.empty();
         ++redirectCount) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(currentUrl.c_str(), 0, 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS) {
        return {};
    }

    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.handle == nullptr) {
        return {};
    }
    WinHttpSetTimeouts(session.handle, 5000, 5000, 10000, 15000);

    WinHttpHandle connection(WinHttpConnect(
        session.handle,
        std::wstring(components.lpszHostName, components.dwHostNameLength).c_str(),
        components.nPort,
        0));
    if (connection.handle == nullptr) {
        return {};
    }

    std::wstring objectPath(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0) {
        objectPath.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.handle,
        L"GET",
        objectPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (request.handle == nullptr) {
        return {};
    }

    if (!WinHttpSendRequest(
            request.handle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        return {};
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
        return {};
    }

    if (statusCode == 301 ||
        statusCode == 302 ||
        statusCode == 303 ||
        statusCode == 307 ||
        statusCode == 308) {
        const auto redirectLocation = QueryRedirectLocation(request.handle);
        if (redirectLocation.rfind(L"https://", 0) != 0) {
            return {};
        }
        currentUrl = redirectLocation;
        continue;
    }

    if (statusCode != 200) {
        return {};
    }

    std::string payload;
    for (;;) {
        DWORD availableBytes = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &availableBytes)) {
            return {};
        }
        if (availableBytes == 0) {
            break;
        }
        if (payload.size() + availableBytes > kMaxTerminalPayloadBytes) {
            return {};
        }

        std::string buffer(availableBytes, '\0');
        DWORD downloadedBytes = 0;
        if (!WinHttpReadData(
                request.handle,
                buffer.data(),
                availableBytes,
                &downloadedBytes)) {
            return {};
        }
        buffer.resize(downloadedBytes);
        payload += buffer;
    }
    return payload;
    }
    return {};
}

std::string JsonString(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key) {
    return winrt::to_string(object.GetNamedString(key, L""));
}

std::vector<std::string> JsonStringArray(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key) {
    std::vector<std::string> values;
    const auto array = object.GetNamedArray(
        key,
        winrt::Windows::Data::Json::JsonArray{});
    for (uint32_t index = 0; index < array.Size(); ++index) {
        const auto value = array.GetStringAt(index);
        if (!value.empty()) {
            values.push_back(winrt::to_string(value));
        }
    }
    return values;
}

core::authority::AuthorityPolygonRing ParseRing(
    const winrt::Windows::Data::Json::JsonArray& ringArray) {
    core::authority::AuthorityPolygonRing ring;
    for (uint32_t index = 0; index < ringArray.Size(); ++index) {
        const auto point = ringArray.GetArrayAt(index);
        if (point.Size() < 2) {
            continue;
        }
        ring.points.push_back({
            point.GetNumberAt(1),
            point.GetNumberAt(0),
        });
    }
    return ring;
}

std::vector<core::authority::AuthorityPolygonRing> ParsePolygonRings(
    const winrt::Windows::Data::Json::JsonArray& polygonArray) {
    std::vector<core::authority::AuthorityPolygonRing> rings;
    for (uint32_t ringIndex = 0; ringIndex < polygonArray.Size(); ++ringIndex) {
        auto ring = ParseRing(polygonArray.GetArrayAt(ringIndex));
        if (ring.points.size() >= 3) {
            rings.push_back(std::move(ring));
        }
    }
    return rings;
}

std::vector<core::authority::AuthorityPolygonSourceRecord>
ParseTerminalGeoJsonRecords(const std::string& payload) {
    std::vector<core::authority::AuthorityPolygonSourceRecord> records;
    if (payload.empty()) {
        return records;
    }

    static const bool kWinRtInitialized = []() {
        try {
            winrt::init_apartment();
        } catch (...) {
        }
        return true;
    }();
    (void)kWinRtInitialized;

    try {
        const auto root =
            winrt::Windows::Data::Json::JsonObject::Parse(
                winrt::to_hstring(payload));
        const auto features = root.GetNamedArray(
            L"features",
            winrt::Windows::Data::Json::JsonArray{});
        for (uint32_t featureIndex = 0; featureIndex < features.Size(); ++featureIndex) {
            const auto feature = features.GetObjectAt(featureIndex);
            const auto properties = feature.GetNamedObject(
                L"properties",
                winrt::Windows::Data::Json::JsonObject{});
            const auto geometry = feature.GetNamedObject(
                L"geometry",
                winrt::Windows::Data::Json::JsonObject{});
            const auto geometryType = geometry.GetNamedString(L"type", L"");
            const auto coordinates = geometry.GetNamedArray(
                L"coordinates",
                winrt::Windows::Data::Json::JsonArray{});

            auto buildRecord =
                [&](std::vector<core::authority::AuthorityPolygonRing> rings) {
                    if (rings.empty()) {
                        return;
                    }
                    core::authority::AuthorityPolygonSourceRecord record;
                    record.source =
                        core::authority::AuthoritySource::SimAwareTracon;
                    record.id = JsonString(properties, L"id");
                    if (record.id.empty()) {
                        record.id = JsonString(properties, L"identifier");
                    }
                    record.name = JsonString(properties, L"name");
                    record.suffix = JsonString(properties, L"suffix");
                    record.prefixes = JsonStringArray(properties, L"prefix");
                    if (record.prefixes.empty()) {
                        record.prefixes = JsonStringArray(properties, L"prefixes");
                    }
                    record.sourceRecord = winrt::to_string(feature.Stringify());
                    record.rings = std::move(rings);
                    if (!record.id.empty()) {
                        records.push_back(std::move(record));
                    }
                };

            if (geometryType == L"Polygon") {
                buildRecord(ParsePolygonRings(coordinates));
            } else if (geometryType == L"MultiPolygon") {
                for (uint32_t polygonIndex = 0;
                     polygonIndex < coordinates.Size();
                     ++polygonIndex) {
                    buildRecord(
                        ParsePolygonRings(
                            coordinates.GetArrayAt(polygonIndex)));
                }
            }
        }
    } catch (...) {
        records.clear();
    }
    return records;
}

TerminalAuthorityCatalog BuildCatalogFromPayload(
    const std::string& payload,
    std::uint64_t generation) {
    TerminalAuthorityCatalog catalog;
    catalog.generation = generation;
    if (payload.empty()) {
        catalog.status = "terminal-authority-empty-payload";
        return catalog;
    }

    const auto records = ParseTerminalGeoJsonRecords(payload);
    const auto polygonCatalog =
        core::authority::CompileAuthorityPolygons(records);
    catalog.polygons = polygonCatalog.polygons;
    catalog.available = !catalog.polygons.empty();
    catalog.status =
        catalog.available ? "terminal-authority-source-ready"
                          : "terminal-authority-no-polygons";
    return catalog;
}

}  // namespace

TerminalAuthorityResolver::~TerminalAuthorityResolver() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
}

void TerminalAuthorityResolver::Reset() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    catalog_ = {};
    pendingCatalog_ = {};
    hasPendingCatalog_ = false;
    lastFetchTickSeconds_ = 0;
    lastFetchSucceeded_ = false;
    fetchInProgress_ = false;
    airportCache_.clear();
}

void TerminalAuthorityResolver::LoadPayloadForTesting(
    const std::string& terminalBoundaryGeoJson) {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    catalog_ = BuildCatalogFromPayload(terminalBoundaryGeoJson, catalog_.generation + 1);
    if (catalog_.generation == 0) {
        catalog_.generation = 1;
    }
    pendingCatalog_ = {};
    hasPendingCatalog_ = false;
    fetchInProgress_ = false;
    lastFetchSucceeded_ = catalog_.available;
    lastFetchTickSeconds_ = CurrentTickSeconds();
    airportCache_.clear();
}

brain::BrainTerminalAuthorityWorkerOutput
TerminalAuthorityResolver::ResolveAirportTerminalOwner(
    const brain::BrainTerminalAuthorityWorkerInput& input) {
    const auto started = std::chrono::steady_clock::now();
    HarvestPendingFetch();

    brain::BrainTerminalAuthorityWorkerOutput output;
    output.airportIcao = NormalizeToken(input.airportIcao);
    output.source = "SIMAWARE_TRACON";

    if (output.airportIcao.empty()) {
        output.status = "terminal-authority-missing-airport";
        output.lookupUs = ElapsedMicrosecondsSince(started);
        return output;
    }
    if (!input.hasAirportCoordinates) {
        output.status = "terminal-authority-missing-airport-coordinates";
        output.lookupUs = ElapsedMicrosecondsSince(started);
        return output;
    }

    TerminalAuthorityCatalog catalog;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        catalog = catalog_;
        const auto cacheKey = BuildAirportCacheKey(input, catalog.generation);
        const auto cached = airportCache_.find(cacheKey);
        if (cached != airportCache_.end()) {
            output = cached->second;
            output.cacheStatus = "terminal-authority-airport-cache-hit";
            output.lookupUs = ElapsedMicrosecondsSince(started);
            return output;
        }
    }

    const auto nowSeconds =
        input.nowSeconds > 0 ? input.nowSeconds : CurrentTickSeconds();
    if (!catalog.available) {
        const auto cadenceSeconds =
            lastFetchSucceeded_ ? kRefreshCadenceSeconds : kFailureBackoffSeconds;
        const auto shouldFetch =
            lastFetchTickSeconds_ == 0 ||
            (nowSeconds - lastFetchTickSeconds_) >= cadenceSeconds;
        if (shouldFetch && !fetchInProgress_) {
            (void)StartAsyncFetch(nowSeconds);
        }
        output.pending = fetchInProgress_;
        output.status = output.pending ? "terminal-authority-source-pending"
                                       : "terminal-authority-source-unavailable";
        output.cacheStatus = "terminal-authority-source-miss";
        output.lookupUs = ElapsedMicrosecondsSince(started);
        return output;
    }

    output = ResolveFromCatalog(input, catalog);
    output.lookupUs = ElapsedMicrosecondsSince(started);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        airportCache_[BuildAirportCacheKey(input, catalog.generation)] = output;
    }
    return output;
}

brain::BrainTerminalAuthorityWorkerOutput
TerminalAuthorityResolver::ResolveFromCatalog(
    const brain::BrainTerminalAuthorityWorkerInput& input,
    const TerminalAuthorityCatalog& catalog) const {
    brain::BrainTerminalAuthorityWorkerOutput output;
    output.available = catalog.available;
    output.resolved = catalog.available;
    output.airportIcao = NormalizeToken(input.airportIcao);
    output.source = "SIMAWARE_TRACON";
    output.sourceGeneration = catalog.generation;
    output.cacheStatus = "terminal-authority-airport-resolved";
    const auto airportTokens = BuildAirportTokens(input.airportIcao);

    for (const auto& polygon : catalog.polygons) {
        if (!PointInPolygon(
                input.airportLatitudeDeg,
                input.airportLongitudeDeg,
                polygon)) {
            continue;
        }

        AppendUnique(&output.polygonKeys, polygon.polygonKey);
        const auto polygonRoleSuffixes =
            TerminalRoleSuffixesFromPolygon(polygon);
        const auto sharedAppDep = SimAwarePolygonIsSharedAppDep(polygon);
        for (const auto& polygonRoleSuffix : polygonRoleSuffixes) {
            const auto polygonServiceToken =
                TerminalServiceToken(polygon.polygonKey, polygonRoleSuffix);
            if (TerminalServiceMatchesSharedPolygonKey(
                    polygonServiceToken,
                    polygon.polygonKey) ||
                TerminalServiceMatchesAirportTokens(
                    polygonServiceToken,
                    airportTokens)) {
                AppendUnique(&output.ownerTokens, polygonServiceToken);
            }
        }
        for (const auto& lookupKey : polygon.lookupKeys) {
            for (const auto& serviceToken :
                 TerminalServiceTokensFromLookupKey(lookupKey, sharedAppDep)) {
                if (TerminalServiceMatchesSharedPolygonKey(
                        serviceToken,
                        polygon.polygonKey) ||
                    TerminalServiceMatchesAirportTokens(
                        serviceToken,
                        airportTokens)) {
                    AppendUnique(&output.ownerTokens, serviceToken);
                }
            }
        }
    }

    std::sort(output.ownerTokens.begin(), output.ownerTokens.end());
    output.ownerTokens.erase(
        std::unique(output.ownerTokens.begin(), output.ownerTokens.end()),
        output.ownerTokens.end());
    std::sort(output.polygonKeys.begin(), output.polygonKeys.end());
    output.polygonKeys.erase(
        std::unique(output.polygonKeys.begin(), output.polygonKeys.end()),
        output.polygonKeys.end());
    output.status =
        output.ownerTokens.empty()
            ? "terminal-authority-no-containing-terminal"
            : "terminal-authority-owner-resolved";
    return output;
}

bool TerminalAuthorityResolver::StartAsyncFetch(long long nowSeconds) {
    if (fetchInProgress_) {
        return false;
    }
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    lastFetchTickSeconds_ = nowSeconds;
    fetchInProgress_ = true;
    try {
        fetchThread_ = std::thread([this]() {
            const auto payload = DownloadHttpsPayload(kTerminalBoundaryUrl);
            TerminalAuthorityCatalog catalog;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                catalog = BuildCatalogFromPayload(payload, catalog_.generation + 1);
                if (catalog.generation == 0) {
                    catalog.generation = 1;
                }
                pendingCatalog_ = std::move(catalog);
                hasPendingCatalog_ = true;
            }
            fetchInProgress_ = false;
        });
    } catch (...) {
        fetchInProgress_ = false;
        return false;
    }
    return true;
}

void TerminalAuthorityResolver::HarvestPendingFetch() {
    if (fetchInProgress_) {
        return;
    }
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasPendingCatalog_) {
        return;
    }

    hasPendingCatalog_ = false;
    if (pendingCatalog_.available) {
        catalog_ = std::move(pendingCatalog_);
        lastFetchSucceeded_ = true;
        airportCache_.clear();
    } else {
        lastFetchSucceeded_ = false;
    }
    pendingCatalog_ = {};
}

}  // namespace xvatsim::modules::terminal_authority
