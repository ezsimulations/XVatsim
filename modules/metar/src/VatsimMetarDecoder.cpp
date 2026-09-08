#include "XVatsim/modules/metar/VatsimMetarDecoder.h"

#include <chrono>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace xvatsim::modules::metar {
namespace {

constexpr std::size_t kMaximumPayloadBytes = 65'536;
constexpr std::size_t kMaximumStationBytes = 16;
constexpr std::size_t kMaximumRawMetarBytes = 4'096;

VatsimMetarJsonFieldType FieldType(
    const winrt::Windows::Data::Json::IJsonValue& value) {
    using JsonValueType = winrt::Windows::Data::Json::JsonValueType;
    if (value == nullptr) return VatsimMetarJsonFieldType::Missing;
    switch (value.ValueType()) {
        case JsonValueType::String: return VatsimMetarJsonFieldType::String;
        case JsonValueType::Null: return VatsimMetarJsonFieldType::Null;
        case JsonValueType::Boolean: return VatsimMetarJsonFieldType::Boolean;
        case JsonValueType::Number: return VatsimMetarJsonFieldType::Number;
        case JsonValueType::Array: return VatsimMetarJsonFieldType::Array;
        case JsonValueType::Object: return VatsimMetarJsonFieldType::Object;
    }
    return VatsimMetarJsonFieldType::Missing;
}

}  // namespace

VatsimMetarDecodedPayloadFact DecodeVatsimMetarPayload(
    std::string_view boundedPayload) {
    const auto started = std::chrono::steady_clock::now();
    VatsimMetarDecodedPayloadFact fact;
    fact.payloadBytes = boundedPayload.size();
    const auto finish = [&](VatsimMetarDecodeStatus status,
                            const char* diagnostic) {
        fact.decodeStatus = status;
        fact.diagnostic = diagnostic == nullptr ? "" : diagnostic;
        fact.decodingElapsedMicroseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
        return fact;
    };
    if (boundedPayload.size() > kMaximumPayloadBytes) {
        fact.reportCardinalityLimitExceeded = true;
        return finish(VatsimMetarDecodeStatus::ResourceFailure,
                      "payload-bound-fact");
    }
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_changed_state&) {
    }
    try {
        using namespace winrt::Windows::Data::Json;
        JsonArray array{nullptr};
        if (!JsonArray::TryParse(
                winrt::to_hstring(std::string(boundedPayload)), array)) {
            return finish(VatsimMetarDecodeStatus::MalformedJson,
                          "json-syntax-malformed");
        }
        fact.reportCardinality = array.Size();
        fact.reportCardinalityLimitExceeded = array.Size() > 1;
        if (array.Size() != 1) {
            return finish(VatsimMetarDecodeStatus::Decoded,
                          "json-cardinality-decoded");
        }
        const auto item = array.GetAt(0);
        if (item.ValueType() != JsonValueType::Object) {
            return finish(VatsimMetarDecodeStatus::RootTypeMismatch,
                          "report-object-type-mismatch");
        }
        const auto object = item.GetObject();
        VatsimMetarDecodedReportFields fields;
        const auto station = object.GetNamedValue(L"id", nullptr);
        const auto metar = object.GetNamedValue(L"metar", nullptr);
        fields.stationFieldType = FieldType(station);
        fields.metarFieldType = FieldType(metar);
        fields.stationFieldMissing = station == nullptr;
        fields.metarFieldMissing = metar == nullptr;
        fields.stationFieldMalformed = station != nullptr &&
            station.ValueType() != JsonValueType::String;
        fields.metarFieldMalformed = metar != nullptr &&
            metar.ValueType() != JsonValueType::String;
        if (!fields.stationFieldMalformed && !fields.stationFieldMissing) {
            fields.returnedStation = winrt::to_string(station.GetString());
            fields.stationByteLimitExceeded =
                fields.returnedStation.size() > kMaximumStationBytes;
        }
        if (!fields.metarFieldMalformed && !fields.metarFieldMissing) {
            fields.rawMetar = winrt::to_string(metar.GetString());
            fields.rawMetarByteLimitExceeded =
                fields.rawMetar.size() > kMaximumRawMetarBytes;
            fields.rawMetarContainsNul =
                fields.rawMetar.find('\0') != std::string::npos;
            if (!fields.rawMetar.empty()) {
                const auto isOuter = [](char value) {
                    return value == ' ' || value == '\t' ||
                        value == '\r' || value == '\n';
                };
                fields.rawMetarHasLeadingWhitespace =
                    isOuter(fields.rawMetar.front());
                fields.rawMetarHasTrailingWhitespace =
                    isOuter(fields.rawMetar.back());
            }
        }
        fact.soleReport = std::move(fields);
        return finish(VatsimMetarDecodeStatus::Decoded,
                      "json-fields-decoded");
    } catch (...) {
        return finish(VatsimMetarDecodeStatus::ResourceFailure,
                      "json-decode-resource-failure");
    }
}

}  // namespace xvatsim::modules::metar
