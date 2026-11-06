#include "XVatsim/modules/vnas_data/VnasDataClient.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace xvatsim::modules::vnas_data {
namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/2.2.0";
constexpr wchar_t kControllerFeedHost[] = L"live.env.vnas.vatsim.net";
constexpr wchar_t kControllerFeedPath[] = L"/data-feed/controllers.json";
constexpr wchar_t kFacilityDataHost[] = L"data-api.vnas.vatsim.net";
constexpr long long kControllerRefreshSeconds = 15;
constexpr long long kFailureBackoffSeconds = 60;
constexpr long long kControllerHoldoverSeconds = 75;
constexpr int kHttpResolveTimeoutMs = 2500;
constexpr int kHttpConnectTimeoutMs = 2500;
constexpr int kHttpSendTimeoutMs = 2500;
constexpr int kHttpReceiveTimeoutMs = 7000;
constexpr std::size_t kMaxControllerPayloadBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaxFacilityPayloadBytes = 24U * 1024U * 1024U;
constexpr std::size_t kMaxControllers = 2000;
constexpr std::size_t kMaxPositionsPerController = 256;
constexpr std::size_t kMaxFacilities = 512;
constexpr std::size_t kMaxFacilityPositions = 512;
constexpr std::size_t kMaxInternalAirports = 4096;
constexpr std::size_t kMaxFacilityAreas = 256;
constexpr std::size_t kMaxTcps = 1024;
constexpr std::size_t kMaxDepartureCoordinations = 512;
constexpr std::size_t kMaxDepartureFrequencies = 128;
constexpr std::size_t kMaxTextChars = 128;
constexpr int kMaxTcpParentDepth = 16;

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

std::string Trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string Upper(std::string value) {
    value = Trim(std::move(value));
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

std::string NormalizeCallsign(std::string value) {
    value = Upper(std::move(value));
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char character) {
                return std::isalnum(character) == 0 && character != '_' &&
                       character != '-';
            }),
        value.end());
    return value.size() <= 32 ? value : std::string{};
}

std::string NormalizeId(std::string value) {
    value = Upper(std::move(value));
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char character) {
                return std::isalnum(character) == 0;
            }),
        value.end());
    // vNAS uses short ARTCC/facility identifiers and 26-character ULIDs for
    // positions and STARS areas. Keep the common normalizer large enough for
    // both; individual JSON fields are still bounded by GetString().
    return value.size() <= 64 ? value : std::string{};
}

std::string NormalizeFrequency(std::string frequency) {
    frequency = Trim(std::move(frequency));
    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
        } else if (character == '.' && !sawDecimal) {
            sawDecimal = true;
        }
    }
    if (digits.empty()) {
        return {};
    }
    if (sawDecimal) {
        while (decimals < 3) {
            digits.push_back('0');
            ++decimals;
        }
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }
    return digits;
}

std::string FrequencyFromHz(double frequencyHz) {
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0) {
        return {};
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3)
           << (frequencyHz / 1000000.0);
    return stream.str();
}

std::string DownloadJsonDocument(
    const wchar_t* host,
    const std::wstring& path,
    std::size_t maximumBytes) {
    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.handle == nullptr ||
        !WinHttpSetTimeouts(
            session.handle,
            kHttpResolveTimeoutMs,
            kHttpConnectTimeoutMs,
            kHttpSendTimeoutMs,
            kHttpReceiveTimeoutMs)) {
        return {};
    }

    WinHttpHandle connection(WinHttpConnect(
        session.handle,
        host,
        INTERNET_DEFAULT_HTTPS_PORT,
        0));
    if (connection.handle == nullptr) {
        return {};
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.handle,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (request.handle == nullptr ||
        !WinHttpSendRequest(
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
            WINHTTP_NO_HEADER_INDEX) ||
        statusCode != 200) {
        return {};
    }

    std::string payload;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            return {};
        }
        if (available == 0) {
            break;
        }
        if (payload.size() + available > maximumBytes) {
            return {};
        }
        std::vector<char> buffer(available);
        DWORD downloaded = 0;
        if (!WinHttpReadData(
                request.handle,
                buffer.data(),
                available,
                &downloaded) ||
            payload.size() + downloaded > maximumBytes) {
            return {};
        }
        payload.append(buffer.data(), downloaded);
    }
    return payload;
}

bool TryGetObject(
    const winrt::Windows::Data::Json::JsonObject& parent,
    const wchar_t* key,
    winrt::Windows::Data::Json::JsonObject* outObject) {
    using namespace winrt::Windows::Data::Json;
    if (outObject == nullptr || !parent.HasKey(key)) {
        return false;
    }
    const auto value = parent.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::Object) {
        return false;
    }
    *outObject = value.GetObject();
    return true;
}

std::string GetString(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    std::size_t maximumChars = kMaxTextChars) {
    using namespace winrt::Windows::Data::Json;
    if (!object.HasKey(key)) {
        return {};
    }
    const auto value = object.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::String) {
        return {};
    }
    auto text = Trim(winrt::to_string(value.GetString()));
    return text.size() <= maximumChars ? text : std::string{};
}

bool GetBool(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    bool fallback = false) {
    using namespace winrt::Windows::Data::Json;
    if (!object.HasKey(key)) {
        return fallback;
    }
    const auto value = object.GetNamedValue(key);
    return value.ValueType() == JsonValueType::Boolean
               ? value.GetBoolean()
               : fallback;
}

double GetNumber(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key) {
    using namespace winrt::Windows::Data::Json;
    if (!object.HasKey(key)) {
        return 0.0;
    }
    const auto value = object.GetNamedValue(key);
    return value.ValueType() == JsonValueType::Number
               ? value.GetNumber()
               : 0.0;
}

std::vector<std::string> GetStringArray(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    std::size_t maximumValues) {
    using namespace winrt::Windows::Data::Json;
    std::vector<std::string> values;
    if (!object.HasKey(key)) {
        return values;
    }
    const auto jsonValue = object.GetNamedValue(key);
    if (jsonValue.ValueType() != JsonValueType::Array) {
        return values;
    }
    const auto array = jsonValue.GetArray();
    for (uint32_t index = 0;
         index < array.Size() && values.size() < maximumValues;
         ++index) {
        const auto value = array.GetAt(index);
        if (value.ValueType() != JsonValueType::String) {
            continue;
        }
        const auto normalized = NormalizeId(winrt::to_string(value.GetString()));
        if (!normalized.empty()) {
            values.push_back(normalized);
        }
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void AddUniqueNormalizedId(
    std::vector<std::string>* values,
    const std::string& value,
    std::size_t maximumValues) {
    if (values == nullptr || values->size() >= maximumValues) {
        return;
    }
    const auto normalized = NormalizeId(value);
    if (!normalized.empty() &&
        std::find(values->begin(), values->end(), normalized) == values->end()) {
        values->push_back(normalized);
    }
}

void AddUniqueNormalizedFrequency(
    std::vector<std::string>* values,
    const std::string& value) {
    if (values == nullptr || values->size() >= kMaxDepartureFrequencies) {
        return;
    }
    const auto normalized = NormalizeFrequency(value);
    if (!normalized.empty() &&
        std::find(values->begin(), values->end(), normalized) == values->end()) {
        values->push_back(normalized);
    }
}

void CollectTdlsTransitionFrequencies(
    const winrt::Windows::Data::Json::JsonArray& sids,
    std::vector<std::string>* frequencies) {
    using namespace winrt::Windows::Data::Json;
    if (frequencies == nullptr) {
        return;
    }
    for (uint32_t sidIndex = 0;
         sidIndex < sids.Size() &&
         frequencies->size() < kMaxDepartureFrequencies;
         ++sidIndex) {
        const auto sidValue = sids.GetAt(sidIndex);
        if (sidValue.ValueType() != JsonValueType::Object) {
            continue;
        }
        const auto sid = sidValue.GetObject();
        const auto transitions =
            sid.GetNamedArray(L"transitions", JsonArray{});
        for (uint32_t transitionIndex = 0;
             transitionIndex < transitions.Size() &&
             frequencies->size() < kMaxDepartureFrequencies;
             ++transitionIndex) {
            const auto transitionValue = transitions.GetAt(transitionIndex);
            if (transitionValue.ValueType() != JsonValueType::Object) {
                continue;
            }
            AddUniqueNormalizedFrequency(
                frequencies,
                GetString(
                    transitionValue.GetObject(),
                    L"defaultDepFreq",
                    24));
        }
    }
}

void ParseTdlsDepartureFrequencies(
    const winrt::Windows::Data::Json::JsonObject& facilityObject,
    std::vector<std::string>* frequencies) {
    using namespace winrt::Windows::Data::Json;
    JsonObject tdls;
    if (frequencies == nullptr ||
        !TryGetObject(facilityObject, L"tdlsConfiguration", &tdls)) {
        return;
    }

    CollectTdlsTransitionFrequencies(
        tdls.GetNamedArray(L"sids", JsonArray{}), frequencies);
    const auto operationConfigurations =
        tdls.GetNamedArray(L"opConfigs", JsonArray{});
    for (uint32_t index = 0;
         index < operationConfigurations.Size() &&
         frequencies->size() < kMaxDepartureFrequencies;
         ++index) {
        const auto value = operationConfigurations.GetAt(index);
        if (value.ValueType() != JsonValueType::Object) {
            continue;
        }
        CollectTdlsTransitionFrequencies(
            value.GetObject().GetNamedArray(L"sids", JsonArray{}),
            frequencies);
    }
    std::sort(frequencies->begin(), frequencies->end());
}

std::string TcpToken(
    const winrt::Windows::Data::Json::JsonObject& object) {
    const auto subset = static_cast<int>(GetNumber(object, L"subset"));
    const auto sectorId = NormalizeId(GetString(object, L"sectorId", 8));
    if (subset <= 0 || subset > 99 || sectorId.empty()) {
        return {};
    }
    return std::to_string(subset) + sectorId;
}

void ParseFacilityNode(
    const winrt::Windows::Data::Json::JsonObject& object,
    const std::string& parentFacilityId,
    VnasArtccFacilityDocument* document,
    int depth) {
    using namespace winrt::Windows::Data::Json;
    if (document == nullptr || depth > 8 ||
        document->facilities.size() >= kMaxFacilities) {
        return;
    }

    VnasFacility facility;
    facility.facilityId = NormalizeId(GetString(object, L"id", 16));
    if (facility.facilityId.empty()) {
        return;
    }
    facility.parentFacilityId = NormalizeId(parentFacilityId);
    facility.facilityType = GetString(object, L"type", 32);
    facility.facilityName = GetString(object, L"name", kMaxTextChars);

    JsonObject stars;
    std::unordered_map<std::string, std::string> areaNames;
    if (TryGetObject(object, L"starsConfiguration", &stars)) {
        facility.internalAirports =
            GetStringArray(stars, L"internalAirports", kMaxInternalAirports);
        if (stars.HasKey(L"areas") &&
            stars.GetNamedValue(L"areas").ValueType() == JsonValueType::Array) {
            const auto areas = stars.GetNamedArray(L"areas");
            for (uint32_t index = 0;
                 index < areas.Size() &&
                 facility.areas.size() < kMaxFacilityAreas;
                 ++index) {
                const auto area = areas.GetObjectAt(index);
                const auto id = NormalizeId(GetString(area, L"id", 32));
                const auto name = GetString(area, L"name", kMaxTextChars);
                if (!id.empty()) {
                    areaNames[id] = name;
                    VnasFacilityArea parsedArea;
                    parsedArea.areaId = id;
                    parsedArea.areaName = name;
                    parsedArea.airports = GetStringArray(
                        area, L"underlyingAirports", kMaxInternalAirports);
                    const auto ssaAirports = GetStringArray(
                        area, L"ssaAirports", kMaxInternalAirports);
                    for (const auto& airport : ssaAirports) {
                        AddUniqueNormalizedId(
                            &parsedArea.airports,
                            airport,
                            kMaxInternalAirports);
                    }
                    const auto towerConfigurations = area.GetNamedArray(
                        L"towerListConfigurations", JsonArray{});
                    for (uint32_t towerIndex = 0;
                         towerIndex < towerConfigurations.Size();
                         ++towerIndex) {
                        const auto towerValue =
                            towerConfigurations.GetAt(towerIndex);
                        if (towerValue.ValueType() != JsonValueType::Object) {
                            continue;
                        }
                        AddUniqueNormalizedId(
                            &parsedArea.airports,
                            GetString(
                                towerValue.GetObject(),
                                L"airportId",
                                8),
                            kMaxInternalAirports);
                    }
                    std::sort(
                        parsedArea.airports.begin(),
                        parsedArea.airports.end());
                    facility.areas.push_back(std::move(parsedArea));
                }
            }
        }

        const auto tcps = stars.GetNamedArray(L"tcps", JsonArray{});
        for (uint32_t index = 0;
             index < tcps.Size() && facility.tcps.size() < kMaxTcps;
             ++index) {
            const auto tcpValue = tcps.GetAt(index);
            if (tcpValue.ValueType() != JsonValueType::Object) {
                continue;
            }
            const auto tcpObject = tcpValue.GetObject();
            VnasTcp tcp;
            tcp.tcpId = NormalizeId(GetString(tcpObject, L"id", 32));
            tcp.tcpToken = TcpToken(tcpObject);
            tcp.parentTcpId = NormalizeId(
                GetString(tcpObject, L"parentTcpId", 32));
            if (!tcp.tcpId.empty() && !tcp.tcpToken.empty()) {
                facility.tcps.push_back(std::move(tcp));
            }
        }

        const auto lists = stars.GetNamedArray(L"lists", JsonArray{});
        for (uint32_t index = 0;
             index < lists.Size() &&
             facility.departureCoordinations.size() <
                 kMaxDepartureCoordinations;
             ++index) {
            const auto listValue = lists.GetAt(index);
            if (listValue.ValueType() != JsonValueType::Object) {
                continue;
            }
            JsonObject channel;
            if (!TryGetObject(
                    listValue.GetObject(),
                    L"coordinationChannel",
                    &channel) ||
                Upper(GetString(channel, L"flightType", 32)) !=
                    "DEPARTURE") {
                continue;
            }
            VnasDepartureCoordination coordination;
            coordination.airportIcao = NormalizeId(
                GetString(channel, L"airportId", 8));
            const auto receivers =
                channel.GetNamedArray(L"receivers", JsonArray{});
            for (uint32_t receiverIndex = 0;
                 receiverIndex < receivers.Size();
                 ++receiverIndex) {
                const auto receiverValue = receivers.GetAt(receiverIndex);
                if (receiverValue.ValueType() != JsonValueType::Object) {
                    continue;
                }
                const auto receivingTcpId = NormalizeId(GetString(
                    receiverValue.GetObject(), L"receivingTcpId", 32));
                const auto found = std::find_if(
                    facility.tcps.begin(),
                    facility.tcps.end(),
                    [&](const auto& tcp) {
                        return tcp.tcpId == receivingTcpId;
                    });
                if (found != facility.tcps.end()) {
                    AddUniqueNormalizedId(
                        &coordination.receivingTcpTokens,
                        found->tcpToken,
                        kMaxTcps);
                }
            }
            if (!coordination.airportIcao.empty() &&
                !coordination.receivingTcpTokens.empty()) {
                std::sort(
                    coordination.receivingTcpTokens.begin(),
                    coordination.receivingTcpTokens.end());
                facility.departureCoordinations.push_back(
                    std::move(coordination));
            }
        }
    }

    ParseTdlsDepartureFrequencies(object, &facility.departureFrequencies);

    if (object.HasKey(L"positions") &&
        object.GetNamedValue(L"positions").ValueType() == JsonValueType::Array) {
        const auto positions = object.GetNamedArray(L"positions");
        for (uint32_t index = 0;
             index < positions.Size() &&
             facility.positions.size() < kMaxFacilityPositions;
             ++index) {
            const auto positionObject = positions.GetObjectAt(index);
            VnasFacilityPosition position;
            position.positionId =
                NormalizeId(GetString(positionObject, L"id", 32));
            position.positionName =
                GetString(positionObject, L"name", kMaxTextChars);
            position.callsign = NormalizeCallsign(
                GetString(positionObject, L"callsign", 32));
            position.frequency = FrequencyFromHz(
                GetNumber(positionObject, L"frequency"));
            JsonObject positionStars;
            if (TryGetObject(
                    positionObject,
                    L"starsConfiguration",
                    &positionStars)) {
                position.areaId = NormalizeId(
                    GetString(positionStars, L"areaId", 32));
                position.tcpId = NormalizeId(
                    GetString(positionStars, L"tcpId", 32));
                const auto tcp = std::find_if(
                    facility.tcps.begin(),
                    facility.tcps.end(),
                    [&](const auto& value) {
                        return value.tcpId == position.tcpId;
                    });
                if (tcp != facility.tcps.end()) {
                    position.tcpToken = tcp->tcpToken;
                }
                const auto area = areaNames.find(position.areaId);
                if (area != areaNames.end()) {
                    position.areaName = area->second;
                }
            }
            if (!position.positionId.empty()) {
                facility.positions.push_back(std::move(position));
            }
        }
    }

    std::vector<JsonObject> children;
    if (object.HasKey(L"childFacilities") &&
        object.GetNamedValue(L"childFacilities").ValueType() ==
            JsonValueType::Array) {
        const auto childArray = object.GetNamedArray(L"childFacilities");
        children.reserve(childArray.Size());
        for (uint32_t index = 0; index < childArray.Size(); ++index) {
            const auto child = childArray.GetObjectAt(index);
            const auto childId = NormalizeId(GetString(child, L"id", 16));
            if (!childId.empty()) {
                facility.childFacilityIds.push_back(childId);
                children.push_back(child);
            }
        }
    }

    const auto currentFacilityId = facility.facilityId;
    document->facilities.push_back(std::move(facility));
    for (const auto& child : children) {
        ParseFacilityNode(child, currentFacilityId, document, depth + 1);
    }
}

const VnasFacility* FindFacility(
    const VnasArtccFacilityDocument& document,
    const std::string& facilityId) {
    const auto normalized = NormalizeId(facilityId);
    const auto found = std::find_if(
        document.facilities.begin(),
        document.facilities.end(),
        [&](const auto& facility) {
            return NormalizeId(facility.facilityId) == normalized;
        });
    return found == document.facilities.end() ? nullptr : &*found;
}

const VnasArtccFacilityDocument* FindArtccDocument(
    const std::vector<std::shared_ptr<const VnasArtccFacilityDocument>>&
        documents,
    const std::string& artccId) {
    const auto normalized = NormalizeId(artccId);
    const auto found = std::find_if(
        documents.begin(),
        documents.end(),
        [&](const auto& document) {
            return document != nullptr && document->hasCache &&
                   NormalizeId(document->artccId) == normalized;
        });
    return found == documents.end() ? nullptr : found->get();
}

const VnasFacilityPosition* FindFacilityPosition(
    const VnasFacility& facility,
    const std::string& positionId) {
    const auto normalized = NormalizeId(positionId);
    const auto found = std::find_if(
        facility.positions.begin(),
        facility.positions.end(),
        [&](const auto& position) {
            return NormalizeId(position.positionId) == normalized;
        });
    return found == facility.positions.end() ? nullptr : &*found;
}

const VnasTcp* FindTcpById(
    const VnasFacility& facility,
    const std::string& tcpId) {
    const auto normalized = NormalizeId(tcpId);
    const auto found = std::find_if(
        facility.tcps.begin(),
        facility.tcps.end(),
        [&](const auto& tcp) {
            return tcp.tcpId == normalized;
        });
    return found == facility.tcps.end() ? nullptr : &*found;
}

const VnasTcp* FindTcpByToken(
    const VnasFacility& facility,
    const std::string& tcpToken) {
    const auto normalized = NormalizeId(tcpToken);
    const auto found = std::find_if(
        facility.tcps.begin(),
        facility.tcps.end(),
        [&](const auto& tcp) { return tcp.tcpToken == normalized; });
    return found == facility.tcps.end() ? nullptr : &*found;
}

bool ValidateTcpGraph(const VnasFacility& facility) {
    if (facility.tcps.empty()) {
        return false;
    }
    for (const auto& tcp : facility.tcps) {
        auto current = tcp.tcpId;
        std::unordered_set<std::string> visited;
        bool reachedRoot = false;
        for (int depth = 0; depth < kMaxTcpParentDepth; ++depth) {
            if (!visited.insert(current).second) {
                return false;
            }
            const auto* node = FindTcpById(facility, current);
            if (node == nullptr) {
                return false;
            }
            if (node->parentTcpId.empty()) {
                reachedRoot = true;
                break;
            }
            current = node->parentTcpId;
        }
        if (!reachedRoot) {
            return false;
        }
    }
    return true;
}

bool TcpIsAncestorOf(
    const VnasFacility& facility,
    const std::string& possibleAncestorId,
    const std::string& descendantId) {
    const auto ancestor = NormalizeId(possibleAncestorId);
    auto current = NormalizeId(descendantId);
    if (ancestor.empty() || current.empty() || ancestor == current) {
        return false;
    }
    std::unordered_set<std::string> visited;
    bool foundAncestor = false;
    for (int depth = 0; depth < kMaxTcpParentDepth; ++depth) {
        if (!visited.insert(current).second) {
            return false;
        }
        const auto* tcp = FindTcpById(facility, current);
        if (tcp == nullptr || tcp->parentTcpId.empty()) {
            return foundAncestor;
        }
        if (tcp->parentTcpId == ancestor) {
            foundAncestor = true;
        }
        current = tcp->parentTcpId;
    }
    return false;
}

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(value.size()));
    for (const auto character : value) {
        HashCombine(
            seed,
            static_cast<std::uint64_t>(
                static_cast<unsigned char>(character)));
    }
}

std::uint64_t HashLiveOwnership(
    const brain::VnasLiveOwnershipSnapshot& snapshot) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(&hash, snapshot.enabled ? 1U : 0U);
    HashCombine(&hash, snapshot.available ? 1U : 0U);
    HashCombine(&hash, snapshot.stale ? 1U : 0U);
    HashCombine(&hash, snapshot.facilityDataComplete ? 1U : 0U);
    HashCombine(&hash, snapshot.ownershipDataAvailable ? 1U : 0U);
    HashCombine(&hash, snapshot.unresolvedLivePositions);
    for (const auto& fact : snapshot.facts) {
        HashCombine(&hash, NormalizeCallsign(fact.controllerCallsign));
        HashCombine(&hash, NormalizeFrequency(fact.controllerFrequency));
        HashCombine(&hash, NormalizeId(fact.artccId));
        HashCombine(&hash, NormalizeId(fact.facilityId));
        HashCombine(&hash, NormalizeId(fact.livePositionId));
        HashCombine(&hash, NormalizeCallsign(fact.livePositionCallsign));
        HashCombine(&hash, NormalizeId(fact.sourcePositionId));
        HashCombine(&hash, NormalizeCallsign(fact.sourcePositionCallsign));
        HashCombine(&hash, NormalizeFrequency(fact.sourcePositionFrequency));
        HashCombine(&hash, NormalizeId(fact.tcpToken));
        for (const auto& airport : fact.ownedAirportCodes) {
            HashCombine(&hash, NormalizeId(airport));
        }
        for (const auto& airport : fact.departureAirportCodes) {
            HashCombine(&hash, NormalizeId(airport));
        }
        HashCombine(
            &hash,
            static_cast<std::uint64_t>(fact.ownershipKind));
        HashCombine(&hash, fact.proof);
    }
    return hash;
}

std::uint64_t HashControllerFeedDocument(
    const VnasControllerFeedDocument& document) {
    std::vector<std::string> facts;
    for (const auto& controller : document.controllers) {
        if (!controller.active || controller.observer) {
            continue;
        }
        std::ostringstream controllerFact;
        controllerFact << NormalizeId(controller.artccId) << '|'
                       << NormalizeCallsign(controller.vatsimCallsign) << '|'
                       << NormalizeFrequency(controller.vatsimFrequency);
        facts.push_back(controllerFact.str());
        for (const auto& position : controller.positions) {
            if (!position.active) {
                continue;
            }
            std::ostringstream positionFact;
            positionFact << NormalizeCallsign(controller.vatsimCallsign) << '|'
                         << NormalizeId(position.facilityId) << '|'
                         << NormalizeId(position.positionId) << '|'
                         << NormalizeFrequency(position.frequency) << '|'
                         << NormalizeId(position.areaId);
            for (const auto& tcp : position.assumedTcps) {
                positionFact << '|' << NormalizeId(tcp);
            }
            facts.push_back(positionFact.str());
        }
    }
    std::sort(facts.begin(), facts.end());
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& fact : facts) {
        HashCombine(&hash, fact);
    }
    return hash;
}

std::uint64_t HashFacilityDocument(
    const VnasArtccFacilityDocument& document) {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombine(&hash, NormalizeId(document.artccId));
    for (const auto& facility : document.facilities) {
        HashCombine(&hash, NormalizeId(facility.facilityId));
        for (const auto& area : facility.areas) {
            HashCombine(&hash, area.areaId);
            for (const auto& airport : area.airports) {
                HashCombine(&hash, airport);
            }
        }
        for (const auto& tcp : facility.tcps) {
            HashCombine(&hash, tcp.tcpId);
            HashCombine(&hash, tcp.tcpToken);
            HashCombine(&hash, tcp.parentTcpId);
        }
        HashCombine(&hash, facility.tcpGraphValid ? 1U : 0U);
        for (const auto& position : facility.positions) {
            HashCombine(&hash, position.positionId);
            HashCombine(&hash, position.callsign);
            HashCombine(&hash, position.areaId);
            HashCombine(&hash, position.tcpToken);
        }
        for (const auto& frequency : facility.departureFrequencies) {
            HashCombine(&hash, frequency);
        }
    }
    return hash;
}

std::string BuildProof(
    const VnasLiveController& controller,
    const VnasLivePosition& position,
    const VnasFacilityPosition* staticPosition) {
    std::ostringstream stream;
    stream << "artcc=" << NormalizeId(controller.artccId)
           << "/facility=" << NormalizeId(position.facilityId)
           << "/position=" << NormalizeId(position.positionId);
    const auto areaName = staticPosition != nullptr
                              ? staticPosition->areaName
                              : std::string{};
    if (!areaName.empty()) {
        stream << "/area=" << areaName;
    }
    return stream.str();
}

std::unordered_set<std::string> CollectInterestedArtccs(
    const VnasControllerFeedDocument& feed) {
    std::unordered_set<std::string> artccs;
    if (!feed.hasCache || feed.stale) {
        return artccs;
    }
    for (const auto& controller : feed.controllers) {
        if (!controller.active || controller.observer) {
            continue;
        }
        const auto hasActiveTraconPosition = std::any_of(
            controller.positions.begin(),
            controller.positions.end(),
            [](const auto& position) {
                return position.active &&
                       Upper(position.positionType) == "TRACON";
            });
        if (!hasActiveTraconPosition) {
            continue;
        }
        const auto artcc = NormalizeId(controller.artccId);
        if (!artcc.empty()) {
            artccs.insert(artcc);
        }
    }
    return artccs;
}

}  // namespace

VnasControllerFeedDocument DecodeVnasControllerFeedDocument(
    const std::string& payload) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    VnasControllerFeedDocument document;
    if (payload.empty()) {
        return document;
    }
    try {
        // Apartment initialization is thread-local. Decoding is used by both
        // the harness thread and short-lived background fetch threads, so a
        // process-wide one-time flag would leave later workers uninitialized.
        try {
            init_apartment(apartment_type::multi_threaded);
        } catch (const hresult_changed_state&) {
        }
        const auto root = JsonObject::Parse(to_hstring(payload));
        document.updatedAt = GetString(root, L"updatedAt", 64);
        const auto controllers = root.GetNamedArray(L"controllers", JsonArray{});
        for (uint32_t controllerIndex = 0;
             controllerIndex < controllers.Size() &&
             document.controllers.size() < kMaxControllers;
             ++controllerIndex) {
            const auto object = controllers.GetObjectAt(controllerIndex);
            VnasLiveController controller;
            controller.artccId = NormalizeId(GetString(object, L"artccId", 16));
            controller.primaryFacilityId =
                NormalizeId(GetString(object, L"primaryFacilityId", 16));
            controller.primaryPositionId =
                NormalizeId(GetString(object, L"primaryPositionId", 32));
            controller.active = GetBool(object, L"isActive");
            controller.observer = GetBool(object, L"isObserver");

            JsonObject vatsimData;
            if (TryGetObject(object, L"vatsimData", &vatsimData)) {
                controller.vatsimCallsign = NormalizeCallsign(
                    GetString(vatsimData, L"callsign", 32));
                controller.vatsimFrequency = FrequencyFromHz(
                    GetNumber(vatsimData, L"primaryFrequency"));
            }

            const auto positions = object.GetNamedArray(L"positions", JsonArray{});
            for (uint32_t positionIndex = 0;
                 positionIndex < positions.Size() &&
                 controller.positions.size() < kMaxPositionsPerController;
                 ++positionIndex) {
                const auto positionObject = positions.GetObjectAt(positionIndex);
                VnasLivePosition position;
                position.facilityId = NormalizeId(
                    GetString(positionObject, L"facilityId", 16));
                position.facilityName =
                    GetString(positionObject, L"facilityName", kMaxTextChars);
                position.positionId = NormalizeId(
                    GetString(positionObject, L"positionId", 32));
                position.positionName =
                    GetString(positionObject, L"positionName", kMaxTextChars);
                position.positionType =
                    GetString(positionObject, L"positionType", 32);
                position.radioName =
                    GetString(positionObject, L"radioName", kMaxTextChars);
                position.defaultCallsign = NormalizeCallsign(
                    GetString(positionObject, L"defaultCallsign", 32));
                position.frequency = FrequencyFromHz(
                    GetNumber(positionObject, L"frequency"));
                position.primary = GetBool(positionObject, L"isPrimary");
                position.active = GetBool(positionObject, L"isActive");
                JsonObject starsData;
                if (TryGetObject(positionObject, L"starsData", &starsData)) {
                    position.areaId = NormalizeId(
                        GetString(starsData, L"areaId", 32));
                    position.assumedTcps =
                        GetStringArray(starsData, L"assumedTcps", 256);
                }
                if (!position.facilityId.empty() &&
                    !position.positionId.empty()) {
                    controller.positions.push_back(std::move(position));
                }
            }
            if (!controller.artccId.empty() &&
                !controller.vatsimCallsign.empty()) {
                document.controllers.push_back(std::move(controller));
            }
        }
        document.hasCache = true;
        document.stale = false;
        document.stableHash = HashControllerFeedDocument(document);
    } catch (...) {
        return {};
    }
    return document;
}

VnasArtccFacilityDocument DecodeVnasArtccFacilityDocument(
    const std::string& payload) {
    using namespace winrt;
    using namespace winrt::Windows::Data::Json;

    VnasArtccFacilityDocument document;
    if (payload.empty()) {
        return document;
    }
    try {
        try {
            init_apartment(apartment_type::multi_threaded);
        } catch (const hresult_changed_state&) {
        }
        const auto root = JsonObject::Parse(to_hstring(payload));
        document.artccId = NormalizeId(GetString(root, L"id", 16));
        document.lastUpdatedAt = GetString(root, L"lastUpdatedAt", 64);
        JsonObject facility;
        if (document.artccId.empty() ||
            !TryGetObject(root, L"facility", &facility)) {
            return {};
        }
        ParseFacilityNode(facility, {}, &document, 0);
        for (auto& parsedFacility : document.facilities) {
            parsedFacility.tcpGraphValid = ValidateTcpGraph(parsedFacility);
        }
        document.hasCache = !document.facilities.empty();
        if (document.hasCache) {
            document.stableHash = HashFacilityDocument(document);
        }
    } catch (...) {
        return {};
    }
    return document;
}

brain::VnasLiveOwnershipSnapshot BuildVnasLiveOwnershipSnapshot(
    const VnasControllerFeedDocument& controllerFeed,
    const std::vector<std::shared_ptr<const VnasArtccFacilityDocument>>&
        facilityDocuments,
    bool enabled) {
    brain::VnasLiveOwnershipSnapshot snapshot;
    snapshot.enabled = enabled;
    if (!enabled) {
        snapshot.statusLine = "vnas-live-ownership-disabled";
        snapshot.stableHash = HashLiveOwnership(snapshot);
        return snapshot;
    }

    snapshot.available = controllerFeed.hasCache && !controllerFeed.stale;
    snapshot.stale = !snapshot.available;
    if (!snapshot.available) {
        snapshot.statusLine = "vnas-live-ownership-feed-unavailable";
        snapshot.stableHash = HashLiveOwnership(snapshot);
        return snapshot;
    }

    const auto interestedArtccs = CollectInterestedArtccs(controllerFeed);
    snapshot.facilityDataComplete = std::all_of(
        interestedArtccs.begin(),
        interestedArtccs.end(),
        [&](const auto& artcc) {
            return FindArtccDocument(facilityDocuments, artcc) != nullptr;
        });

    std::unordered_set<std::string> insertedFacts;
    const auto appendFact = [&snapshot, &insertedFacts](
                                const VnasLiveController& controller,
                                const VnasLivePosition& livePosition,
                                const VnasArtccFacilityDocument& artcc,
                                const VnasFacility& facility,
                                const VnasFacilityPosition& sourcePosition,
                                brain::VnasLiveOwnershipKind kind) {
        if (sourcePosition.callsign.empty()) {
            return;
        }
        const auto key = NormalizeCallsign(controller.vatsimCallsign) + "|" +
                         NormalizeId(livePosition.positionId) + "|" +
                         NormalizeId(sourcePosition.positionId) + "|" +
                         std::to_string(static_cast<int>(kind));
        if (!insertedFacts.insert(key).second) {
            return;
        }

        brain::VnasLiveOwnershipFactSnapshot fact;
        fact.controllerCallsign = controller.vatsimCallsign;
        fact.controllerFrequency = controller.vatsimFrequency;
        fact.artccId = controller.artccId;
        fact.facilityId = livePosition.facilityId;
        fact.livePositionId = livePosition.positionId;
        fact.livePositionCallsign = livePosition.defaultCallsign;
        fact.sourcePositionId = sourcePosition.positionId;
        fact.sourcePositionCallsign = sourcePosition.callsign;
        fact.sourcePositionFrequency = sourcePosition.frequency;
        fact.tcpToken = sourcePosition.tcpToken;
        const auto area = std::find_if(
            facility.areas.begin(),
            facility.areas.end(),
            [&](const auto& value) {
                return NormalizeId(value.areaId) ==
                       NormalizeId(livePosition.areaId);
            });
        const auto& sourceAirports =
            area != facility.areas.end() && !area->airports.empty()
                ? area->airports
                : facility.internalAirports;
        for (const auto& airport : sourceAirports) {
            const auto normalized = NormalizeId(airport);
            if (!normalized.empty()) {
                fact.ownedAirportCodes.push_back(normalized);
            }
        }
        std::sort(
            fact.ownedAirportCodes.begin(),
            fact.ownedAirportCodes.end());
        fact.ownedAirportCodes.erase(
            std::unique(
                fact.ownedAirportCodes.begin(),
                fact.ownedAirportCodes.end()),
            fact.ownedAirportCodes.end());
        const auto liveFrequency = NormalizeFrequency(
            !controller.vatsimFrequency.empty()
                ? controller.vatsimFrequency
                : livePosition.frequency);
        for (const auto& candidateFacility : artcc.facilities) {
            const auto isCurrentOrChild =
                NormalizeId(candidateFacility.facilityId) ==
                    NormalizeId(facility.facilityId) ||
                NormalizeId(candidateFacility.parentFacilityId) ==
                    NormalizeId(facility.facilityId);
            if (!isCurrentOrChild || liveFrequency.empty()) {
                continue;
            }
            const auto frequencyMatched = std::any_of(
                candidateFacility.departureFrequencies.begin(),
                candidateFacility.departureFrequencies.end(),
                [&](const auto& value) {
                    return NormalizeFrequency(value) == liveFrequency;
                });
            if (frequencyMatched) {
                fact.departureAirportCodes.push_back(
                    NormalizeId(candidateFacility.facilityId));
            }
        }
        std::sort(
            fact.departureAirportCodes.begin(),
            fact.departureAirportCodes.end());
        fact.departureAirportCodes.erase(
            std::unique(
                fact.departureAirportCodes.begin(),
                fact.departureAirportCodes.end()),
            fact.departureAirportCodes.end());
        fact.ownershipKind = kind;
        std::ostringstream proof;
        proof << BuildProof(controller, livePosition, &sourcePosition)
              << "/source-position=" << sourcePosition.positionId;
        if (!sourcePosition.tcpToken.empty()) {
            proof << "/tcp=" << sourcePosition.tcpToken;
        }
        if (!fact.ownedAirportCodes.empty()) {
            proof << "/airports=";
            for (std::size_t index = 0;
                 index < fact.ownedAirportCodes.size();
                 ++index) {
                if (index > 0) {
                    proof << ',';
                }
                proof << fact.ownedAirportCodes[index];
            }
        }
        if (!fact.departureAirportCodes.empty()) {
            proof << "/departure-airports=";
            for (std::size_t index = 0;
                 index < fact.departureAirportCodes.size();
                 ++index) {
                if (index > 0) {
                    proof << ',';
                }
                proof << fact.departureAirportCodes[index];
            }
        }
        switch (kind) {
            case brain::VnasLiveOwnershipKind::ExactPosition:
                proof << "/ownership=exact-position";
                break;
            case brain::VnasLiveOwnershipKind::DynamicAssumption:
                proof << "/ownership=dynamic-assumption";
                break;
            case brain::VnasLiveOwnershipKind::ParentAssumption:
                proof << "/ownership=parent-assumption";
                break;
        }
        fact.proof = proof.str();
        snapshot.facts.push_back(std::move(fact));
    };

    for (const auto& controller : controllerFeed.controllers) {
        if (!controller.active || controller.observer) {
            continue;
        }
        const auto* artcc =
            FindArtccDocument(facilityDocuments, controller.artccId);
        if (artcc == nullptr) {
            snapshot.unresolvedLivePositions +=
                std::count_if(
                    controller.positions.begin(),
                    controller.positions.end(),
                    [](const auto& position) { return position.active; });
            continue;
        }
        for (const auto& livePosition : controller.positions) {
            if (!livePosition.active) {
                continue;
            }
            const auto* facility =
                FindFacility(*artcc, livePosition.facilityId);
            const auto* exactPosition = facility != nullptr
                ? FindFacilityPosition(*facility, livePosition.positionId)
                : nullptr;
            if (facility == nullptr || exactPosition == nullptr) {
                ++snapshot.unresolvedLivePositions;
                continue;
            }

            appendFact(
                controller,
                livePosition,
                *artcc,
                *facility,
                *exactPosition,
                brain::VnasLiveOwnershipKind::ExactPosition);
            for (const auto& rawAssumedToken : livePosition.assumedTcps) {
                const auto* assumedTcp =
                    FindTcpByToken(*facility, rawAssumedToken);
                if (assumedTcp == nullptr) {
                    continue;
                }
                for (const auto& ownedPosition : facility->positions) {
                    if (ownedPosition.tcpId != assumedTcp->tcpId) {
                        continue;
                    }
                    auto kind = brain::VnasLiveOwnershipKind::DynamicAssumption;
                    if (ownedPosition.positionId == exactPosition->positionId) {
                        kind = brain::VnasLiveOwnershipKind::ExactPosition;
                    } else if (facility->tcpGraphValid &&
                               TcpIsAncestorOf(
                                   *facility,
                                   exactPosition->tcpId,
                                   ownedPosition.tcpId)) {
                        kind = brain::VnasLiveOwnershipKind::ParentAssumption;
                    }
                    appendFact(
                        controller,
                        livePosition,
                        *artcc,
                        *facility,
                        ownedPosition,
                        kind);
                }
            }
        }
    }

    std::sort(
        snapshot.facts.begin(),
        snapshot.facts.end(),
        [](const auto& left, const auto& right) {
            if (left.controllerCallsign != right.controllerCallsign) {
                return left.controllerCallsign < right.controllerCallsign;
            }
            if (left.livePositionId != right.livePositionId) {
                return left.livePositionId < right.livePositionId;
            }
            if (left.sourcePositionId != right.sourcePositionId) {
                return left.sourcePositionId < right.sourcePositionId;
            }
            return static_cast<int>(left.ownershipKind) <
                   static_cast<int>(right.ownershipKind);
        });
    snapshot.ownershipDataAvailable = !snapshot.facts.empty();
    std::ostringstream status;
    status << "vnas-live-ownership-ready facts=" << snapshot.facts.size()
           << " artccs=" << interestedArtccs.size()
           << " facilityComplete="
           << (snapshot.facilityDataComplete ? 1 : 0)
           << " unresolved=" << snapshot.unresolvedLivePositions;
    snapshot.statusLine = status.str();
    snapshot.stableHash = HashLiveOwnership(snapshot);
    return snapshot;
}

struct VnasDataClient::Impl {
    enum class TaskType { None, ControllerFeed, Facility };

    struct PendingResult {
        TaskType type = TaskType::None;
        std::string artccId;
        VnasControllerFeedDocument controllerFeed;
        VnasArtccFacilityDocument facility;
        bool completed = false;
    };

    ~Impl() {
        JoinWorker();
    }

    void JoinWorker() {
        if (worker.joinable()) {
            worker.join();
        }
    }

    void Harvest() {
        if (workerBusy.load()) {
            return;
        }
        JoinWorker();
        std::lock_guard<std::mutex> lock(mutex);
        if (!pending.completed) {
            return;
        }
        const auto now = CurrentTickSeconds();
        if (pending.type == TaskType::ControllerFeed) {
            if (pending.controllerFeed.hasCache) {
                controllerLastSuccess = now;
                if (!controllerFeed.hasCache ||
                    controllerFeed.stableHash !=
                        pending.controllerFeed.stableHash) {
                    controllerFeed = std::move(pending.controllerFeed);
                    controllerFeed.stale = false;
                    ++sourceVersion;
                } else {
                    // Successful identical refresh: update health without
                    // publishing a new generation or rebuilding Brain facts.
                    controllerFeed.stale = false;
                    controllerFeed.updatedAt =
                        pending.controllerFeed.updatedAt;
                }
            }
        } else if (pending.type == TaskType::Facility &&
                   pending.facility.hasCache) {
            const auto artcc = NormalizeId(pending.facility.artccId);
            const auto existing = facilities.find(artcc);
            if (existing == facilities.end() || existing->second == nullptr ||
                existing->second->stableHash != pending.facility.stableHash) {
                facilities[artcc] = std::make_shared<
                    const VnasArtccFacilityDocument>(
                    std::move(pending.facility));
                ++sourceVersion;
            }
        }
        pending = {};
    }

    bool StartControllerFetch(long long now) {
        if (workerBusy.exchange(true)) {
            return false;
        }
        JoinWorker();
        controllerLastAttempt = now;
        try {
            worker = std::thread([this]() {
                VnasControllerFeedDocument decoded;
                try {
                    decoded = DecodeVnasControllerFeedDocument(
                        DownloadJsonDocument(
                            kControllerFeedHost,
                            kControllerFeedPath,
                            kMaxControllerPayloadBytes));
                    std::lock_guard<std::mutex> lock(mutex);
                    pending = {};
                    pending.type = TaskType::ControllerFeed;
                    pending.controllerFeed = std::move(decoded);
                    pending.completed = true;
                } catch (...) {
                }
                workerBusy.store(false);
            });
        } catch (...) {
            workerBusy.store(false);
            return false;
        }
        return true;
    }

    bool StartFacilityFetch(const std::string& artccId, long long now) {
        const auto normalized = NormalizeId(artccId);
        if (normalized.empty() || workerBusy.exchange(true)) {
            return false;
        }
        JoinWorker();
        facilityLastAttempt[normalized] = now;
        try {
            worker = std::thread([this, normalized]() {
                try {
                    const auto path =
                        std::wstring(L"/api/artccs/") +
                        std::wstring(normalized.begin(), normalized.end());
                    auto decoded = DecodeVnasArtccFacilityDocument(
                        DownloadJsonDocument(
                            kFacilityDataHost,
                            path,
                            kMaxFacilityPayloadBytes));
                    std::lock_guard<std::mutex> lock(mutex);
                    pending = {};
                    pending.type = TaskType::Facility;
                    pending.artccId = normalized;
                    pending.facility = std::move(decoded);
                    pending.completed = true;
                } catch (...) {
                }
                workerBusy.store(false);
            });
        } catch (...) {
            workerBusy.store(false);
            return false;
        }
        return true;
    }

    void AdvanceSourceAcquisition(bool enabled, long long now) {
        Harvest();
        if (enabled && !workerBusy.load()) {
            const auto feedDue =
                controllerLastAttempt == 0 ||
                (now - controllerLastAttempt) >=
                    (controllerFeed.hasCache ? kControllerRefreshSeconds
                                             : kFailureBackoffSeconds);
            if (feedDue) {
                (void)StartControllerFetch(now);
            } else if (controllerFeed.hasCache) {
                auto feedForInterest = controllerFeed;
                feedForInterest.stale =
                    controllerLastSuccess == 0 ||
                    (now - controllerLastSuccess) > kControllerHoldoverSeconds;
                const auto artccs = CollectInterestedArtccs(feedForInterest);
                std::vector<std::string> ordered(artccs.begin(), artccs.end());
                std::sort(ordered.begin(), ordered.end());
                for (const auto& artcc : ordered) {
                    if (facilities.find(artcc) != facilities.end()) {
                        continue;
                    }
                    const auto attempt = facilityLastAttempt.find(artcc);
                    if (attempt != facilityLastAttempt.end() &&
                        (now - attempt->second) < kFailureBackoffSeconds) {
                        continue;
                    }
                    (void)StartFacilityFetch(artcc, now);
                    break;
                }
            }
        }

        controllerFeed.stale = !controllerFeed.hasCache ||
                               controllerLastSuccess == 0 ||
                               (now - controllerLastSuccess) >
                                   kControllerHoldoverSeconds;
    }

    std::vector<std::shared_ptr<const VnasArtccFacilityDocument>>
    FacilityDocuments() const {
        std::vector<std::shared_ptr<const VnasArtccFacilityDocument>> documents;
        documents.reserve(facilities.size());
        for (const auto& entry : facilities) {
            documents.push_back(entry.second);
        }
        return documents;
    }

    std::shared_ptr<const brain::VnasLiveOwnershipSnapshot>
    PollLiveOwnership(bool enabled) {
        const auto now = CurrentTickSeconds();
        AdvanceSourceAcquisition(enabled, now);
        std::uint64_t requestKey = 1469598103934665603ULL;
        HashCombine(&requestKey, sourceVersion);
        HashCombine(&requestKey, enabled ? 1U : 0U);
        HashCombine(&requestKey, controllerFeed.stale ? 1U : 0U);
        if (publishedLiveOwnership != nullptr &&
            requestKey == lastLiveOwnershipRequestKey) {
            return publishedLiveOwnership;
        }

        const auto documents = FacilityDocuments();
        auto next = BuildVnasLiveOwnershipSnapshot(
            controllerFeed,
            documents,
            enabled);
        if (publishedLiveOwnership == nullptr ||
            publishedLiveOwnership->stableHash != next.stableHash) {
            next.generation = ++liveOwnershipGeneration;
        } else {
            next.generation = publishedLiveOwnership->generation;
        }
        publishedLiveOwnership = std::make_shared<
            const brain::VnasLiveOwnershipSnapshot>(std::move(next));
        lastLiveOwnershipRequestKey = requestKey;
        return publishedLiveOwnership;
    }

    void Reset() {
        JoinWorker();
        std::lock_guard<std::mutex> lock(mutex);
        workerBusy.store(false);
        pending = {};
        controllerFeed = {};
        facilities.clear();
        facilityLastAttempt.clear();
        controllerLastAttempt = 0;
        controllerLastSuccess = 0;
        sourceVersion = 0;
        liveOwnershipGeneration = 0;
        lastLiveOwnershipRequestKey = 0;
        publishedLiveOwnership.reset();
    }

    std::atomic<bool> workerBusy{false};
    std::mutex mutex;
    std::thread worker;
    PendingResult pending;
    VnasControllerFeedDocument controllerFeed;
    std::unordered_map<
        std::string,
        std::shared_ptr<const VnasArtccFacilityDocument>> facilities;
    std::unordered_map<std::string, long long> facilityLastAttempt;
    long long controllerLastAttempt = 0;
    long long controllerLastSuccess = 0;
    std::uint64_t sourceVersion = 0;
    std::uint64_t liveOwnershipGeneration = 0;
    std::uint64_t lastLiveOwnershipRequestKey = 0;
    std::shared_ptr<const brain::VnasLiveOwnershipSnapshot>
        publishedLiveOwnership;
};

VnasDataClient::VnasDataClient() : impl_(std::make_unique<Impl>()) {}

VnasDataClient::~VnasDataClient() = default;

std::shared_ptr<const brain::VnasLiveOwnershipSnapshot>
VnasDataClient::PollLiveOwnership(bool enabled) {
    return impl_->PollLiveOwnership(enabled);
}

void VnasDataClient::Reset() {
    impl_->Reset();
}

}  // namespace xvatsim::modules::vnas_data
