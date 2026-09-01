#include "XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include "XVatsim/brain/BrainOwnedWorkerTypes.h"

namespace xvatsim::modules::vatsim_data_feed {

namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/1.2.3";
constexpr wchar_t kHost[] = L"data.vatsim.net";
constexpr wchar_t kPath[] = L"/v3/vatsim-data.json";
constexpr long long kRefreshCadenceSeconds = 15;
constexpr long long kFailureBackoffSeconds = 60;
constexpr long long kInProgressCacheGraceSeconds = 45;
constexpr int kHttpResolveTimeoutMs = 2500;
constexpr int kHttpConnectTimeoutMs = 2500;
constexpr int kHttpSendTimeoutMs = 2500;
constexpr int kHttpReceiveTimeoutMs = 5000;
constexpr std::size_t kMaxFeedPayloadBytes = 64 * 1024 * 1024;
constexpr std::size_t kMaxControllers = 5000;
constexpr std::size_t kMaxPilotPlans = 30000;
constexpr std::size_t kMaxCallsignChars = 32;
constexpr std::size_t kMaxFrequencyChars = 16;
constexpr std::size_t kMaxAirportChars = 8;
constexpr std::size_t kMaxAltitudeChars = 16;
constexpr std::size_t kMaxRouteTextChars = 4096;
constexpr std::size_t kMaxControllerTextAtisChars = 2048;
constexpr std::size_t kMaxRootAtisRecords = 256;
constexpr std::size_t kMaxRootAtisCallsignBytes = 32;
constexpr std::size_t kMaxRootAtisFrequencyBytes = 16;
constexpr std::size_t kMaxRootAtisInformationCodeBytes = 8;
constexpr std::size_t kMaxRootAtisTimestampBytes = 48;
constexpr std::size_t kMaxRootAtisTextLines = 64;
constexpr std::size_t kMaxRootAtisTextLineBytes = 512;
constexpr std::size_t kMaxRootAtisJoinedBodyBytes = 8 * 1024;
constexpr std::size_t kMaxRootAtisRetainedFieldBytes = 1024 * 1024;
constexpr std::size_t kMaxRootAtisConservativeBytes = 2 * 1024 * 1024;

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

long long CurrentTickSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string DownloadJsonDocument() {
    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.handle == nullptr) {
        return {};
    }
    if (!WinHttpSetTimeouts(
            session.handle,
            kHttpResolveTimeoutMs,
            kHttpConnectTimeoutMs,
            kHttpSendTimeoutMs,
            kHttpReceiveTimeoutMs)) {
        return {};
    }

    WinHttpHandle connection(WinHttpConnect(
        session.handle,
        kHost,
        INTERNET_DEFAULT_HTTPS_PORT,
        0));
    if (connection.handle == nullptr) {
        return {};
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.handle,
        L"GET",
        kPath,
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
            0)) {
        return {};
    }

    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
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

        if (payload.size() + availableBytes > kMaxFeedPayloadBytes) {
            return {};
        }

        std::vector<char> buffer(availableBytes);
        DWORD downloadedBytes = 0;
        if (!WinHttpReadData(
                request.handle,
                buffer.data(),
                availableBytes,
                &downloadedBytes)) {
            return {};
        }

        if (payload.size() + downloadedBytes > kMaxFeedPayloadBytes) {
            return {};
        }

        payload.append(buffer.data(), downloadedBytes);
    }

    return payload;
}

std::string NormalizeCallsign(const std::string& callsign) {
    std::string normalized;
    normalized.reserve(callsign.size());

    for (const auto character : callsign) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }

    if (normalized.size() > kMaxCallsignChars) {
        return {};
    }

    return normalized;
}

std::string NormalizeIcao(const std::string& airportIcao) {
    std::string normalized;
    normalized.reserve(airportIcao.size());

    for (const auto character : airportIcao) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }

    if (normalized.size() > kMaxAirportChars) {
        return {};
    }

    return normalized;
}

std::string TrimString(std::string value) {
    const auto nullPosition = value.find('\0');
    if (nullPosition != std::string::npos) {
        value.resize(nullPosition);
    }

    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string NormalizeControllerCallsign(const std::string& callsign) {
    std::string normalized;
    normalized.reserve(std::min(callsign.size(), kMaxCallsignChars));

    for (const auto character : callsign) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0 || value == '_' || value == '-') {
            normalized.push_back(static_cast<char>(std::toupper(value)));
        }
        if (normalized.size() > kMaxCallsignChars) {
            return {};
        }
    }

    return normalized;
}

std::string NormalizeControllerFrequency(const std::string& frequency) {
    std::string normalized;
    normalized.reserve(std::min(frequency.size(), kMaxFrequencyChars));

    bool sawDecimal = false;
    for (const auto character : frequency) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isdigit(value) != 0) {
            normalized.push_back(static_cast<char>(value));
        } else if (character == '.' && !sawDecimal) {
            normalized.push_back('.');
            sawDecimal = true;
        }

        if (normalized.size() > kMaxFrequencyChars) {
            return {};
        }
    }

    return normalized;
}

std::string NormalizeRouteText(const std::string& routeText) {
    if (routeText.size() > kMaxRouteTextChars) {
        return {};
    }

    std::string normalized;
    normalized.reserve(routeText.size());
    bool pendingSpace = false;
    for (const auto character : routeText) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isspace(value) != 0) {
            pendingSpace = !normalized.empty();
            continue;
        }
        if (std::iscntrl(value) != 0) {
            continue;
        }
        if (pendingSpace) {
            normalized.push_back(' ');
            pendingSpace = false;
        }
        normalized.push_back(static_cast<char>(std::toupper(value)));
    }

    return normalized;
}

std::string ToUpperCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

std::string GetOptionalJsonString(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    std::size_t maxChars) {
    using namespace winrt::Windows::Data::Json;

    if (!object.HasKey(key)) {
        return {};
    }

    const auto value = object.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::String) {
        return {};
    }

    auto text = TrimString(winrt::to_string(value.GetString()));
    if (text.size() > maxChars) {
        return {};
    }
    return text;
}

std::string GetOptionalJsonStringArrayText(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    std::size_t maxChars) {
    using namespace winrt::Windows::Data::Json;

    if (!object.HasKey(key)) {
        return {};
    }

    const auto value = object.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::Array) {
        return {};
    }

    const auto array = value.GetArray();
    std::string text;
    for (uint32_t index = 0; index < array.Size(); ++index) {
        const auto item = array.GetAt(index);
        if (item.ValueType() != JsonValueType::String) {
            continue;
        }

        auto line = TrimString(winrt::to_string(item.GetString()));
        if (line.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += " | ";
        }
        if (text.size() + line.size() > maxChars) {
            break;
        }
        text += line;
    }
    return text;
}

void AddAtisIssue(
    std::uint32_t* mask,
    brain::BrainRawVatsimAtisMechanicalIssue issue) {
    if (mask != nullptr) {
        *mask |= brain::BrainRawVatsimAtisIssueBit(issue);
    }
}

bool SanitizeAtisField(
    std::string value,
    std::size_t maximumBytes,
    bool collapseWhitespace,
    std::string* output) {
    if (output == nullptr || value.find('\0') != std::string::npos) return false;
    std::string sanitized;
    sanitized.reserve(std::min(value.size(), maximumBytes));
    bool pendingSpace = false;
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (character < 0x20U || character == 0x7fU) {
            if (raw == '\t' || raw == '\r' || raw == '\n') {
                pendingSpace = collapseWhitespace && !sanitized.empty();
                continue;
            }
            return false;
        }
        if (collapseWhitespace && std::isspace(character) != 0) {
            pendingSpace = !sanitized.empty();
            continue;
        }
        if (pendingSpace) {
            if (sanitized.size() >= maximumBytes) return false;
            sanitized.push_back(' ');
            pendingSpace = false;
        }
        if (sanitized.size() >= maximumBytes) return false;
        sanitized.push_back(raw);
    }
    const auto begin = sanitized.find_first_not_of(' ');
    if (begin == std::string::npos) {
        output->clear();
        return true;
    }
    const auto end = sanitized.find_last_not_of(' ');
    *output = sanitized.substr(begin, end - begin + 1);
    return true;
}

bool DecodeAtisStringField(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    std::size_t maximumBytes,
    bool required,
    brain::BrainRawVatsimAtisMechanicalIssue wrongTypeIssue,
    brain::BrainRawVatsimAtisMechanicalIssue unsafeIssue,
    std::string* output,
    std::uint32_t* issueMask) {
    using namespace winrt::Windows::Data::Json;
    if (!object.HasKey(key)) {
        if (required) AddAtisIssue(issueMask, wrongTypeIssue);
        if (output != nullptr) output->clear();
        return !required;
    }
    const auto value = object.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::String) {
        AddAtisIssue(issueMask, wrongTypeIssue);
        if (output != nullptr) output->clear();
        return false;
    }
    try {
        if (!SanitizeAtisField(
                winrt::to_string(value.GetString()), maximumBytes, true,
                output)) {
            AddAtisIssue(issueMask, unsafeIssue);
            if (output != nullptr) output->clear();
            return false;
        }
        return true;
    } catch (...) {
        AddAtisIssue(issueMask, unsafeIssue);
        if (output != nullptr) output->clear();
        return false;
    }
}

void DecodeRootAtisComponent(
    const winrt::Windows::Data::Json::JsonObject& root,
    VatsimDataFeedSnapshot* snapshot) {
    using namespace winrt::Windows::Data::Json;
    if (snapshot == nullptr) return;
    snapshot->atisComponentComplete = false;
    if (!root.HasKey(L"atis")) {
        AddAtisIssue(
            &snapshot->atisMechanicalIssueMask,
            brain::BrainRawVatsimAtisMechanicalIssue::RootMissing);
        return;
    }
    snapshot->atisRootPresent = true;
    const auto rootValue = root.GetNamedValue(L"atis");
    if (rootValue.ValueType() != JsonValueType::Array) {
        AddAtisIssue(
            &snapshot->atisMechanicalIssueMask,
            brain::BrainRawVatsimAtisMechanicalIssue::RootWrongType);
        return;
    }
    snapshot->atisRootArray = true;
    const auto records = rootValue.GetArray();
    if (records.Size() > kMaxRootAtisRecords) {
        AddAtisIssue(
            &snapshot->atisMechanicalIssueMask,
            brain::BrainRawVatsimAtisMechanicalIssue::RootCountExceeded);
        return;
    }
    snapshot->atisRecords.reserve(
        std::min<std::size_t>(records.Size(), kMaxRootAtisRecords));
    bool everyRecordComplete = true;
    for (uint32_t index = 0; index < records.Size(); ++index) {
        const auto value = records.GetAt(index);
        if (value.ValueType() != JsonValueType::Object) {
            ++snapshot->atisRejectedRecordCount;
            everyRecordComplete = false;
            AddAtisIssue(
                &snapshot->atisMechanicalIssueMask,
                brain::BrainRawVatsimAtisMechanicalIssue::RecordWrongType);
            continue;
        }
        brain::BrainRawVatsimAtisRecord record;
        const auto object = value.GetObject();
        (void)DecodeAtisStringField(
            object, L"callsign", kMaxRootAtisCallsignBytes, true,
            brain::BrainRawVatsimAtisMechanicalIssue::CallsignWrongType,
            brain::BrainRawVatsimAtisMechanicalIssue::
                CallsignUnsafeOrOverLimit,
            &record.callsign, &record.mechanicalIssueMask);
        (void)DecodeAtisStringField(
            object, L"frequency", kMaxRootAtisFrequencyBytes, false,
            brain::BrainRawVatsimAtisMechanicalIssue::FrequencyWrongType,
            brain::BrainRawVatsimAtisMechanicalIssue::
                FrequencyUnsafeOrOverLimit,
            &record.frequency, &record.mechanicalIssueMask);
        (void)DecodeAtisStringField(
            object, L"atis_code", kMaxRootAtisInformationCodeBytes, false,
            brain::BrainRawVatsimAtisMechanicalIssue::InformationCodeWrongType,
            brain::BrainRawVatsimAtisMechanicalIssue::
                InformationCodeUnsafeOrOverLimit,
            &record.informationCode, &record.mechanicalIssueMask);
        (void)DecodeAtisStringField(
            object, L"last_updated", kMaxRootAtisTimestampBytes, false,
            brain::BrainRawVatsimAtisMechanicalIssue::TimestampWrongType,
            brain::BrainRawVatsimAtisMechanicalIssue::
                TimestampUnsafeOrOverLimit,
            &record.lastUpdated, &record.mechanicalIssueMask);
        (void)DecodeAtisStringField(
            object, L"logon_time", kMaxRootAtisTimestampBytes, false,
            brain::BrainRawVatsimAtisMechanicalIssue::TimestampWrongType,
            brain::BrainRawVatsimAtisMechanicalIssue::
                TimestampUnsafeOrOverLimit,
            &record.logonTime, &record.mechanicalIssueMask);

        if (!object.HasKey(L"text_atis")) {
            AddAtisIssue(
                &record.mechanicalIssueMask,
                brain::BrainRawVatsimAtisMechanicalIssue::TextWrongType);
        } else {
            const auto textValue = object.GetNamedValue(L"text_atis");
            if (textValue.ValueType() != JsonValueType::Array) {
                AddAtisIssue(
                    &record.mechanicalIssueMask,
                    brain::BrainRawVatsimAtisMechanicalIssue::TextWrongType);
            } else {
                const auto lines = textValue.GetArray();
                if (lines.Size() > kMaxRootAtisTextLines) {
                    AddAtisIssue(
                        &record.mechanicalIssueMask,
                        brain::BrainRawVatsimAtisMechanicalIssue::
                            TextLineCountExceeded);
                } else {
                    record.textLines.reserve(
                        std::min<std::size_t>(
                            lines.Size(), kMaxRootAtisTextLines));
                    std::size_t bodyBytes = 0;
                    for (uint32_t lineIndex = 0;
                         lineIndex < lines.Size(); ++lineIndex) {
                        const auto lineValue = lines.GetAt(lineIndex);
                        if (lineValue.ValueType() != JsonValueType::String) {
                            AddAtisIssue(
                                &record.mechanicalIssueMask,
                                brain::BrainRawVatsimAtisMechanicalIssue::
                                    TextWrongType);
                            continue;
                        }
                        std::string line;
                        bool lineSafe = false;
                        try {
                            lineSafe = SanitizeAtisField(
                                winrt::to_string(lineValue.GetString()),
                                kMaxRootAtisTextLineBytes, true, &line);
                        } catch (...) {
                            lineSafe = false;
                        }
                        if (!lineSafe) {
                            AddAtisIssue(
                                &record.mechanicalIssueMask,
                                brain::BrainRawVatsimAtisMechanicalIssue::
                                    TextLineUnsafeOrOverLimit);
                            continue;
                        }
                        if (line.empty()) continue;
                        const auto delimiterBytes =
                            record.textLines.empty() ? 0U : 1U;
                        if (bodyBytes + delimiterBytes + line.size() >
                            kMaxRootAtisJoinedBodyBytes) {
                            AddAtisIssue(
                                &record.mechanicalIssueMask,
                                brain::BrainRawVatsimAtisMechanicalIssue::
                                    TextBodyExceeded);
                            break;
                        }
                        bodyBytes += delimiterBytes + line.size();
                        record.textLines.push_back(std::move(line));
                    }
                }
            }
        }
        record.retainedFieldBytes = record.callsign.size() +
            record.frequency.size() + record.informationCode.size() +
            record.lastUpdated.size() + record.logonTime.size();
        for (const auto& line : record.textLines) {
            record.retainedFieldBytes += line.size();
        }
        record.mechanicallyComplete = record.mechanicalIssueMask == 0;
        if (!record.mechanicallyComplete) {
            ++snapshot->atisRejectedRecordCount;
            everyRecordComplete = false;
            snapshot->atisMechanicalIssueMask |= record.mechanicalIssueMask;
        }
        const auto conservativeBytes = sizeof(record) +
            record.retainedFieldBytes +
            record.textLines.size() * sizeof(std::string);
        if (snapshot->atisRetainedFieldBytes + record.retainedFieldBytes >
            kMaxRootAtisRetainedFieldBytes) {
            AddAtisIssue(
                &snapshot->atisMechanicalIssueMask,
                brain::BrainRawVatsimAtisMechanicalIssue::
                    AggregateFieldBytesExceeded);
            snapshot->atisRecords.clear();
            snapshot->atisRetainedFieldBytes = 0;
            snapshot->atisConservativeBytes = 0;
            return;
        }
        if (snapshot->atisConservativeBytes + conservativeBytes >
            kMaxRootAtisConservativeBytes) {
            AddAtisIssue(
                &snapshot->atisMechanicalIssueMask,
                brain::BrainRawVatsimAtisMechanicalIssue::
                    ConservativeMemoryExceeded);
            snapshot->atisRecords.clear();
            snapshot->atisRetainedFieldBytes = 0;
            snapshot->atisConservativeBytes = 0;
            return;
        }
        snapshot->atisRetainedFieldBytes += record.retainedFieldBytes;
        snapshot->atisConservativeBytes += conservativeBytes;
        snapshot->atisRecords.push_back(std::move(record));
    }
    snapshot->atisComponentComplete = everyRecordComplete;
}

double ParseFiledAltitudeText(std::string altitudeText) {
    if (altitudeText.size() > kMaxAltitudeChars) {
        return 0.0;
    }

    altitudeText.erase(
        std::remove_if(
            altitudeText.begin(),
            altitudeText.end(),
            [](unsigned char character) { return std::isspace(character) != 0; }),
        altitudeText.end());

    if (altitudeText.empty()) {
        return 0.0;
    }

    std::transform(
        altitudeText.begin(),
        altitudeText.end(),
        altitudeText.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });

    const bool isFlightLevel =
        altitudeText.rfind("FL", 0) == 0 || altitudeText.rfind("F", 0) == 0;

    std::string digitsOnly;
    digitsOnly.reserve(altitudeText.size());
    for (const auto character : altitudeText) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digitsOnly.push_back(character);
        }
    }

    if (digitsOnly.empty()) {
        return 0.0;
    }

    const auto parsedValue = std::strtod(digitsOnly.c_str(), nullptr);
    if (!std::isfinite(parsedValue) || parsedValue <= 0.0) {
        return 0.0;
    }

    if (isFlightLevel || parsedValue <= 600.0) {
        return parsedValue * 100.0;
    }

    return parsedValue;
}

double GetOptionalJsonAltitudeFt(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key) {
    using namespace winrt::Windows::Data::Json;

    if (!object.HasKey(key)) {
        return 0.0;
    }

    const auto value = object.GetNamedValue(key);
    if (value.ValueType() == JsonValueType::Number) {
        const auto numberValue = value.GetNumber();
        if (!std::isfinite(numberValue) || numberValue <= 0.0) {
            return 0.0;
        }

        return numberValue <= 600.0 ? numberValue * 100.0 : numberValue;
    }

    if (value.ValueType() == JsonValueType::String) {
        const auto text = TrimString(winrt::to_string(value.GetString()));
        if (text.size() > kMaxAltitudeChars) {
            return 0.0;
        }
        return ParseFiledAltitudeText(text);
    }

    return 0.0;
}

bool ParseControllerCallsignFlags(
    const std::string& callsign,
    bool* outActionable,
    bool* outAtis) {
    const auto separatorIndex = callsign.rfind('_');
    if (separatorIndex == std::string::npos || separatorIndex == 0 ||
        separatorIndex >= (callsign.size() - 1)) {
        return false;
    }

    const auto suffix = ToUpperCopy(callsign.substr(separatorIndex + 1));
    if (suffix == "OBS" || suffix == "SUP") {
        return false;
    }

    if (outActionable != nullptr) {
        *outActionable = suffix != "ATIS";
    }
    if (outAtis != nullptr) {
        *outAtis = suffix == "ATIS";
    }
    return true;
}

VatsimDataFeedSnapshot ParseFeed(const std::string& payload) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    static const bool kWinRtInitialized = []() {
        try {
            init_apartment();
        } catch (...) {
        }
        return true;
    }();
    (void)kWinRtInitialized;

    VatsimDataFeedSnapshot snapshot;
    if (payload.empty()) {
        return snapshot;
    }

    try {
        const auto root = JsonObject::Parse(to_hstring(payload));

        const auto controllers = root.GetNamedArray(L"controllers", JsonArray{});
        snapshot.connectedControllers = static_cast<int>(controllers.Size());
        for (uint32_t index = 0; index < controllers.Size(); ++index) {
            if (snapshot.controllers.size() >= kMaxControllers) {
                break;
            }

            const auto controllerObject = controllers.GetObjectAt(index);

            brain::ControllerSnapshot controller;
            controller.callsign = NormalizeControllerCallsign(
                GetOptionalJsonString(
                    controllerObject,
                    L"callsign",
                    kMaxCallsignChars));
            controller.frequency = NormalizeControllerFrequency(
                GetOptionalJsonString(
                    controllerObject,
                    L"frequency",
                    kMaxFrequencyChars));
            controller.textAtis = GetOptionalJsonStringArrayText(
                controllerObject,
                L"text_atis",
                kMaxControllerTextAtisChars);
            const auto facilityNumber = controllerObject.GetNamedNumber(L"facility", 0);
            const auto visualRangeNumber =
                controllerObject.GetNamedNumber(L"visual_range", 0);
            if (!std::isfinite(facilityNumber) ||
                !std::isfinite(visualRangeNumber)) {
                continue;
            }

            controller.facility = static_cast<int>(
                std::clamp(facilityNumber, 0.0, 99.0));
            controller.visualRangeNm = static_cast<int>(
                std::clamp(visualRangeNumber, 0.0, 1000.0));

            if (controller.callsign.empty()) {
                continue;
            }

            if (!ParseControllerCallsignFlags(
                    controller.callsign,
                    &controller.actionable,
                    &controller.atis)) {
                continue;
            }

            snapshot.controllers.push_back(std::move(controller));
        }
        snapshot.controllerContentDigest =
            brain::HashBrainControllerEvidenceContent(snapshot.controllers);
        auto authorityControllers =
            brain::BuildBrainAuthorityControllerEvidence(
                snapshot.controllers);
        snapshot.authorityControllerContentDigest =
            brain::HashBrainAuthorityControllerEvidenceContent(
                authorityControllers);
        snapshot.immutableAuthorityControllers = std::make_shared<
            const std::vector<brain::AuthorityControllerSnapshot>>(
                std::move(authorityControllers));
        snapshot.immutableControllers = std::make_shared<
            const std::vector<brain::ControllerSnapshot>>(
                snapshot.controllers);

        const auto pilots = root.GetNamedArray(L"pilots", JsonArray{});
        for (uint32_t index = 0; index < pilots.Size(); ++index) {
            if (snapshot.pilotPlans.size() >= kMaxPilotPlans) {
                break;
            }

            const auto pilotObject = pilots.GetObjectAt(index);
            const auto callsign = NormalizeControllerCallsign(
                GetOptionalJsonString(
                    pilotObject,
                    L"callsign",
                    kMaxCallsignChars));
            const auto normalizedCallsign = NormalizeCallsign(callsign);
            if (callsign.empty() || normalizedCallsign.empty()) {
                continue;
            }

            JsonObject flightPlanObject;
            if (pilotObject.HasKey(L"flight_plan")) {
                const auto flightPlanValue = pilotObject.GetNamedValue(L"flight_plan");
                if (flightPlanValue.ValueType() == JsonValueType::Object) {
                    flightPlanObject = flightPlanValue.GetObject();
                }
            }

            auto departureIcao =
                GetOptionalJsonString(
                    flightPlanObject,
                    L"departure",
                    kMaxAirportChars);
            auto destinationIcao =
                GetOptionalJsonString(
                    flightPlanObject,
                    L"arrival",
                    kMaxAirportChars);

            departureIcao = NormalizeIcao(departureIcao);
            destinationIcao = NormalizeIcao(destinationIcao);

            PilotPlanEntry plan;
            const auto cidNumber = pilotObject.GetNamedNumber(L"cid", 0);
            plan.cid = std::isfinite(cidNumber)
                           ? static_cast<int>(
                                 std::clamp(
                                     cidNumber,
                                     0.0,
                                     static_cast<double>(
                                         std::numeric_limits<int>::max())))
                           : 0;
            plan.callsign = callsign;
            plan.normalizedCallsign = normalizedCallsign;
            plan.departureIcao = departureIcao;
            plan.destinationIcao = destinationIcao;
            plan.filedCruiseAltitudeFt =
                GetOptionalJsonAltitudeFt(flightPlanObject, L"altitude");
            plan.hasFiledCruiseAltitude = plan.filedCruiseAltitudeFt > 0.0;
            plan.routeText =
                NormalizeRouteText(
                    GetOptionalJsonString(
                        flightPlanObject,
                        L"route",
                        kMaxRouteTextChars));
            snapshot.pilotPlans.push_back(std::move(plan));
        }

        DecodeRootAtisComponent(root, &snapshot);

        snapshot.hasCache = true;
        snapshot.stale = false;
        return snapshot;
    } catch (...) {
        return {};
    }
}

}  // namespace

VatsimDataFeedSnapshot DecodeVatsimDataFeedDocument(
    const std::string& payload) {
    return ParseFeed(payload);
}

VatsimDataFeedClient::~VatsimDataFeedClient() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }
}

void VatsimDataFeedClient::Reset() {
    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    cachedSnapshot_ = {};
    emptySnapshot_ = {};
    pendingSnapshot_ = {};
    hasPendingSnapshot_ = false;
    lastFetchSucceeded_ = false;
    lastFetchTickSeconds_ = 0;
    lastSuccessfulFetchTickSeconds_ = 0;
    lastGeneration_ = 0;
    fetchInProgress_.store(false);
}

const VatsimDataFeedSnapshot& VatsimDataFeedClient::Poll() {
    (void)RefreshFeedIfNeeded();
    const auto fetchInProgress = fetchInProgress_.load();
    if (!cachedSnapshot_.hasCache) {
        emptySnapshot_ = {};
        emptySnapshot_.fetchInProgress = fetchInProgress;
        emptySnapshot_.stale = true;
        return emptySnapshot_;
    }

    cachedSnapshot_.fetchInProgress = fetchInProgress;
    cachedSnapshot_.stale = !IsCachedSnapshotFresh(CurrentTickSeconds());
    return cachedSnapshot_;
}

VatsimDataFeedSnapshot VatsimDataFeedClient::FetchSnapshot() const {
    const auto payload = DownloadJsonDocument();
    return DecodeVatsimDataFeedDocument(payload);
}

bool VatsimDataFeedClient::StartAsyncFetch(long long nowSeconds) {
    if (fetchInProgress_.load()) {
        return false;
    }

    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        pendingSnapshot_ = {};
        hasPendingSnapshot_ = false;
    }

    fetchInProgress_.store(true);
    try {
        fetchThread_ = std::thread([this]() {
            VatsimDataFeedSnapshot fetchedSnapshot;
            try {
                fetchedSnapshot = FetchSnapshot();
            } catch (...) {
                fetchedSnapshot = {};
            }

            try {
                std::lock_guard<std::mutex> lock(fetchMutex_);
                pendingSnapshot_ = std::move(fetchedSnapshot);
                hasPendingSnapshot_ = true;
            } catch (...) {
            }
            fetchInProgress_.store(false);
        });
    } catch (...) {
        fetchInProgress_.store(false);
        return false;
    }

    lastFetchTickSeconds_ = nowSeconds;
    return true;
}

void VatsimDataFeedClient::HarvestPendingFetch() {
    if (fetchInProgress_.load()) {
        return;
    }

    if (fetchThread_.joinable()) {
        fetchThread_.join();
    }

    std::lock_guard<std::mutex> lock(fetchMutex_);
    if (!hasPendingSnapshot_) {
        return;
    }

    hasPendingSnapshot_ = false;
    if (!pendingSnapshot_.hasCache) {
        lastFetchSucceeded_ = false;
        pendingSnapshot_ = {};
        return;
    }

    lastFetchSucceeded_ = true;
    cachedSnapshot_ = std::move(pendingSnapshot_);
    cachedSnapshot_.stale = false;
    cachedSnapshot_.fetchInProgress = false;
    cachedSnapshot_.generation = ++lastGeneration_;
    lastSuccessfulFetchTickSeconds_ = CurrentTickSeconds();
    pendingSnapshot_ = {};
}

bool VatsimDataFeedClient::IsCachedSnapshotFresh(long long nowSeconds) const {
    if (!cachedSnapshot_.hasCache ||
        !lastFetchSucceeded_ ||
        lastSuccessfulFetchTickSeconds_ <= 0) {
        return false;
    }

    const auto cacheAgeSeconds = nowSeconds - lastSuccessfulFetchTickSeconds_;
    if (fetchInProgress_.load()) {
        return cacheAgeSeconds <=
               (kRefreshCadenceSeconds + kInProgressCacheGraceSeconds);
    }

    return cacheAgeSeconds <= kRefreshCadenceSeconds;
}

bool VatsimDataFeedClient::RefreshFeedIfNeeded() {
    const auto nowSeconds = CurrentTickSeconds();
    HarvestPendingFetch();

    const auto cadenceSeconds =
        lastFetchSucceeded_ ? kRefreshCadenceSeconds : kFailureBackoffSeconds;
    if (cachedSnapshot_.hasCache &&
        (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        return lastFetchSucceeded_;
    }
    if (!cachedSnapshot_.hasCache &&
        lastFetchTickSeconds_ != 0 &&
        (nowSeconds - lastFetchTickSeconds_) < cadenceSeconds) {
        return false;
    }

    if (!fetchInProgress_.load() && !StartAsyncFetch(nowSeconds)) {
        lastFetchSucceeded_ = false;
        lastFetchTickSeconds_ = nowSeconds;
    }

    return false;
}

}  // namespace xvatsim::modules::vatsim_data_feed
