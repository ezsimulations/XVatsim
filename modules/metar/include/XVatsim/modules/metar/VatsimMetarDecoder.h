#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace xvatsim::modules::metar {

enum class VatsimMetarDecodeStatus {
    NotAttempted,
    Decoded,
    MalformedJson,
    RootTypeMismatch,
    ResourceFailure,
};

enum class VatsimMetarJsonFieldType {
    Missing,
    String,
    Null,
    Boolean,
    Number,
    Array,
    Object,
};

struct VatsimMetarDecodedReportFields {
    VatsimMetarJsonFieldType stationFieldType =
        VatsimMetarJsonFieldType::Missing;
    VatsimMetarJsonFieldType metarFieldType =
        VatsimMetarJsonFieldType::Missing;
    std::string returnedStation;
    std::string rawMetar;
    bool stationFieldMissing = true;
    bool metarFieldMissing = true;
    bool stationFieldMalformed = false;
    bool metarFieldMalformed = false;
    bool stationByteLimitExceeded = false;
    bool rawMetarByteLimitExceeded = false;
    bool rawMetarContainsNul = false;
    bool rawMetarHasLeadingWhitespace = false;
    bool rawMetarHasTrailingWhitespace = false;
};

struct VatsimMetarDecodedPayloadFact {
    VatsimMetarDecodeStatus decodeStatus =
        VatsimMetarDecodeStatus::NotAttempted;
    std::size_t reportCardinality = 0;
    bool reportCardinalityLimitExceeded = false;
    std::optional<VatsimMetarDecodedReportFields> soleReport;
    std::size_t payloadBytes = 0;
    std::uint64_t decodingElapsedMicroseconds = 0;
    std::string diagnostic;
};

// Stateless wire-format decoding only. This function has no requested-station
// input and therefore cannot make a station-match or product-acceptance
// decision.
VatsimMetarDecodedPayloadFact DecodeVatsimMetarPayload(
    std::string_view boundedPayload);

}  // namespace xvatsim::modules::metar
