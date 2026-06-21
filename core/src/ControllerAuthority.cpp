#include "XVatsim/core/ControllerAuthority.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace xvatsim::core::authority {

namespace {

constexpr int kVatsimFlightServiceFacility = 1;
constexpr int kVatsimDeliveryFacility = 2;
constexpr int kVatsimGroundFacility = 3;
constexpr int kVatsimTowerFacility = 4;
constexpr int kVatsimApproachFacility = 5;
constexpr int kVatsimCenterFacility = 6;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

double ToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double ClampUnit(double value) {
    return std::clamp(value, -1.0, 1.0);
}

double NormalizeLongitudeDeg(double longitudeDeg) {
    while (longitudeDeg > 180.0) {
        longitudeDeg -= 360.0;
    }
    while (longitudeDeg < -180.0) {
        longitudeDeg += 360.0;
    }
    return longitudeDeg;
}

double ShortestLongitudeDeltaDeg(double fromLongitudeDeg, double toLongitudeDeg) {
    auto deltaDeg = toLongitudeDeg - fromLongitudeDeg;
    while (deltaDeg > 180.0) {
        deltaDeg -= 360.0;
    }
    while (deltaDeg < -180.0) {
        deltaDeg += 360.0;
    }
    return deltaDeg;
}

double UnwrapLongitudeRelativeDeg(double referenceLongitudeDeg, double longitudeDeg) {
    return referenceLongitudeDeg +
           ShortestLongitudeDeltaDeg(referenceLongitudeDeg, longitudeDeg);
}

double AngularDistanceRad(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    const auto latitudeRadA = ToRadians(latitudeDegA);
    const auto latitudeRadB = ToRadians(latitudeDegB);
    const auto deltaLatitude = ToRadians(latitudeDegB - latitudeDegA);
    const auto deltaLongitude = ToRadians(longitudeDegB - longitudeDegA);

    const auto sinLatitude = std::sin(deltaLatitude / 2.0);
    const auto sinLongitude = std::sin(deltaLongitude / 2.0);
    const auto a = sinLatitude * sinLatitude +
                   std::cos(latitudeRadA) * std::cos(latitudeRadB) *
                       sinLongitude * sinLongitude;
    const auto clampedA = std::clamp(a, 0.0, 1.0);
    return 2.0 * std::atan2(std::sqrt(clampedA), std::sqrt(1.0 - clampedA));
}

double GreatCircleDistanceNm(
    double latitudeDegA,
    double longitudeDegA,
    double latitudeDegB,
    double longitudeDegB) {
    return kEarthRadiusNm *
           AngularDistanceRad(latitudeDegA, longitudeDegA, latitudeDegB, longitudeDegB);
}

double DotProduct(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 AddVector(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 ScaleVector(const Vector3& vector, double scale) {
    return {vector.x * scale, vector.y * scale, vector.z * scale};
}

double VectorLength(const Vector3& vector) {
    return std::sqrt(DotProduct(vector, vector));
}

std::optional<Vector3> NormalizeVector(const Vector3& vector) {
    const auto length = VectorLength(vector);
    if (length <= 1e-12) {
        return std::nullopt;
    }
    return ScaleVector(vector, 1.0 / length);
}

Vector3 ToUnitVector(const GeoPoint& point) {
    const auto latitudeRad = ToRadians(point.latitudeDeg);
    const auto longitudeRad = ToRadians(NormalizeLongitudeDeg(point.longitudeDeg));
    const auto cosLatitude = std::cos(latitudeRad);
    return {
        cosLatitude * std::cos(longitudeRad),
        cosLatitude * std::sin(longitudeRad),
        std::sin(latitudeRad),
    };
}

GeoPoint ToGeoPoint(const Vector3& vector) {
    const auto normalized = NormalizeVector(vector);
    if (!normalized.has_value()) {
        return {};
    }

    const auto horizontalLength =
        std::sqrt(normalized->x * normalized->x + normalized->y * normalized->y);
    return {
        std::atan2(normalized->z, horizontalLength) * 180.0 / kPi,
        NormalizeLongitudeDeg(std::atan2(normalized->y, normalized->x) * 180.0 / kPi),
    };
}

double AngularDistanceRad(const Vector3& a, const Vector3& b) {
    return std::acos(ClampUnit(DotProduct(a, b)));
}

GeoPoint InterpolatePoint(const GeoPoint& start, const GeoPoint& end, double fraction) {
    const auto startVector = ToUnitVector(start);
    const auto endVector = ToUnitVector(end);
    const auto angularDistanceRad = AngularDistanceRad(startVector, endVector);
    const auto sinAngularDistance = std::sin(angularDistanceRad);
    if (angularDistanceRad <= 1e-10 || std::fabs(sinAngularDistance) <= 1e-12) {
        return fraction < 0.5 ? start : end;
    }

    const auto startScale =
        std::sin((1.0 - fraction) * angularDistanceRad) / sinAngularDistance;
    const auto endScale =
        std::sin(fraction * angularDistanceRad) / sinAngularDistance;
    return ToGeoPoint(AddVector(
        ScaleVector(startVector, startScale),
        ScaleVector(endVector, endScale)));
}

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
}

std::size_t SkipJsonWhitespace(const std::string& value, std::size_t index) {
    while (index < value.size() &&
           std::isspace(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    return index;
}

std::optional<std::string> DecodeJsonStringAt(
    const std::string& value,
    std::size_t quoteIndex,
    std::size_t* outNextIndex) {
    if (quoteIndex >= value.size() || value[quoteIndex] != '"') {
        return std::nullopt;
    }

    std::string decoded;
    for (std::size_t index = quoteIndex + 1; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == '"') {
            if (outNextIndex != nullptr) {
                *outNextIndex = index + 1;
            }
            return decoded;
        }

        if (character != '\\') {
            decoded.push_back(character);
            continue;
        }

        if (index + 1 >= value.size()) {
            return std::nullopt;
        }
        const auto escaped = value[++index];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                decoded.push_back(escaped);
                break;
            case 'b':
                decoded.push_back('\b');
                break;
            case 'f':
                decoded.push_back('\f');
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            default:
                return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::string> ExtractJsonStringField(
    const std::string& objectPayload,
    const std::string& fieldName) {
    std::size_t searchIndex = 0;
    for (;;) {
        const auto keyIndex = objectPayload.find('"' + fieldName + '"', searchIndex);
        if (keyIndex == std::string::npos) {
            return std::nullopt;
        }

        auto valueIndex = SkipJsonWhitespace(
            objectPayload,
            keyIndex + fieldName.size() + 2);
        if (valueIndex >= objectPayload.size() || objectPayload[valueIndex] != ':') {
            searchIndex = keyIndex + 1;
            continue;
        }

        valueIndex = SkipJsonWhitespace(objectPayload, valueIndex + 1);
        if (valueIndex >= objectPayload.size() || objectPayload[valueIndex] != '"') {
            return std::nullopt;
        }

        return DecodeJsonStringAt(objectPayload, valueIndex, nullptr);
    }
}

std::vector<std::string> ExtractJsonStringArrayField(
    const std::string& objectPayload,
    const std::string& fieldName) {
    std::vector<std::string> values;
    std::size_t searchIndex = 0;
    for (;;) {
        const auto keyIndex = objectPayload.find('"' + fieldName + '"', searchIndex);
        if (keyIndex == std::string::npos) {
            return values;
        }

        auto arrayIndex = SkipJsonWhitespace(
            objectPayload,
            keyIndex + fieldName.size() + 2);
        if (arrayIndex >= objectPayload.size() || objectPayload[arrayIndex] != ':') {
            searchIndex = keyIndex + 1;
            continue;
        }

        arrayIndex = SkipJsonWhitespace(objectPayload, arrayIndex + 1);
        if (arrayIndex >= objectPayload.size() || objectPayload[arrayIndex] != '[') {
            return values;
        }

        for (std::size_t index = arrayIndex + 1; index < objectPayload.size();) {
            index = SkipJsonWhitespace(objectPayload, index);
            if (index >= objectPayload.size() || objectPayload[index] == ']') {
                return values;
            }
            if (objectPayload[index] != '"') {
                return values;
            }

            std::size_t nextIndex = index;
            const auto decoded = DecodeJsonStringAt(objectPayload, index, &nextIndex);
            if (!decoded.has_value()) {
                return values;
            }
            values.push_back(*decoded);
            index = SkipJsonWhitespace(objectPayload, nextIndex);
            if (index < objectPayload.size() && objectPayload[index] == ',') {
                ++index;
            }
        }

        return values;
    }
}

std::optional<std::string> ExtractJsonArrayPayload(
    const std::string& payload,
    const std::string& fieldName) {
    const auto keyIndex = payload.find('"' + fieldName + '"');
    if (keyIndex == std::string::npos) {
        return std::nullopt;
    }

    auto arrayStart = SkipJsonWhitespace(payload, keyIndex + fieldName.size() + 2);
    if (arrayStart >= payload.size() || payload[arrayStart] != ':') {
        return std::nullopt;
    }
    arrayStart = SkipJsonWhitespace(payload, arrayStart + 1);
    if (arrayStart >= payload.size() || payload[arrayStart] != '[') {
        return std::nullopt;
    }

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (std::size_t index = arrayStart; index < payload.size(); ++index) {
        const auto character = payload[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }

        if (character == '"') {
            inString = true;
            continue;
        }
        if (character == '[') {
            ++depth;
        } else if (character == ']') {
            --depth;
            if (depth == 0) {
                return payload.substr(arrayStart + 1, index - arrayStart - 1);
            }
        }
    }

    return std::nullopt;
}

std::vector<std::string> ExtractJsonObjectPayloads(
    const std::string& payload,
    const std::string& fieldName);

std::optional<std::string> ExtractJsonObjectPayload(
    const std::string& payload,
    const std::string& fieldName) {
    const auto payloads = ExtractJsonObjectPayloads(payload, fieldName);
    if (payloads.empty()) {
        return std::nullopt;
    }
    return payloads.front();
}

std::vector<std::string> ExtractJsonObjectPayloads(
    const std::string& payload,
    const std::string& fieldName) {
    std::vector<std::string> payloads;
    std::size_t searchIndex = 0;
    for (;;) {
        const auto keyIndex = payload.find('"' + fieldName + '"', searchIndex);
        if (keyIndex == std::string::npos) {
            return payloads;
        }

        auto objectStart = SkipJsonWhitespace(payload, keyIndex + fieldName.size() + 2);
        if (objectStart >= payload.size() || payload[objectStart] != ':') {
            searchIndex = keyIndex + 1;
            continue;
        }
        objectStart = SkipJsonWhitespace(payload, objectStart + 1);
        if (objectStart >= payload.size() || payload[objectStart] != '{') {
            searchIndex = keyIndex + 1;
            continue;
        }

        bool inString = false;
        bool escaped = false;
        int depth = 0;
        bool foundObject = false;
        for (std::size_t index = objectStart; index < payload.size(); ++index) {
            const auto character = payload[index];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    inString = false;
                }
                continue;
            }

            if (character == '"') {
                inString = true;
                continue;
            }
            if (character == '{') {
                ++depth;
            } else if (character == '}') {
                --depth;
                if (depth == 0) {
                    payloads.push_back(
                        payload.substr(objectStart + 1, index - objectStart - 1));
                    searchIndex = index + 1;
                    foundObject = true;
                    break;
                }
            }
        }

        if (!foundObject) {
            return payloads;
        }
    }
}

std::vector<std::string> ExtractJsonObjectsFromArrayPayload(
    const std::string& arrayPayload) {
    std::vector<std::string> objects;
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    std::size_t objectStart = std::string::npos;

    for (std::size_t index = 0; index < arrayPayload.size(); ++index) {
        const auto character = arrayPayload[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }

        if (character == '"') {
            inString = true;
            continue;
        }
        if (character == '{') {
            if (depth == 0) {
                objectStart = index;
            }
            ++depth;
        } else if (character == '}') {
            --depth;
            if (depth == 0 && objectStart != std::string::npos) {
                objects.push_back(arrayPayload.substr(objectStart, index - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }

    return objects;
}

std::vector<std::pair<std::string, std::string>> ExtractJsonNamedObjectsFromObjectPayload(
    const std::string& objectPayload) {
    std::vector<std::pair<std::string, std::string>> objects;

    for (std::size_t index = 0; index < objectPayload.size();) {
        index = SkipJsonWhitespace(objectPayload, index);
        if (index >= objectPayload.size()) {
            break;
        }
        if (objectPayload[index] != '"') {
            ++index;
            continue;
        }

        std::size_t afterKeyIndex = index;
        const auto key = DecodeJsonStringAt(objectPayload, index, &afterKeyIndex);
        if (!key.has_value()) {
            break;
        }

        auto valueIndex = SkipJsonWhitespace(objectPayload, afterKeyIndex);
        if (valueIndex >= objectPayload.size() || objectPayload[valueIndex] != ':') {
            index = afterKeyIndex;
            continue;
        }
        valueIndex = SkipJsonWhitespace(objectPayload, valueIndex + 1);
        if (valueIndex >= objectPayload.size() || objectPayload[valueIndex] != '{') {
            index = afterKeyIndex;
            continue;
        }

        bool inString = false;
        bool escaped = false;
        int depth = 0;
        std::size_t objectEnd = std::string::npos;
        for (std::size_t scanIndex = valueIndex; scanIndex < objectPayload.size(); ++scanIndex) {
            const auto character = objectPayload[scanIndex];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    inString = false;
                }
                continue;
            }

            if (character == '"') {
                inString = true;
                continue;
            }
            if (character == '{') {
                ++depth;
            } else if (character == '}') {
                --depth;
                if (depth == 0) {
                    objectEnd = scanIndex;
                    break;
                }
            }
        }

        if (objectEnd == std::string::npos) {
            break;
        }

        objects.push_back({
            *key,
            objectPayload.substr(valueIndex, objectEnd - valueIndex + 1),
        });
        index = objectEnd + 1;
    }

    return objects;
}

std::vector<std::pair<std::string, std::vector<std::string>>>
ExtractJsonNamedStringArraysFromObjectPayload(const std::string& objectPayload) {
    std::vector<std::pair<std::string, std::vector<std::string>>> arrays;

    for (std::size_t index = 0; index < objectPayload.size();) {
        index = SkipJsonWhitespace(objectPayload, index);
        if (index >= objectPayload.size()) {
            break;
        }
        if (objectPayload[index] != '"') {
            ++index;
            continue;
        }

        std::size_t afterKeyIndex = index;
        const auto key = DecodeJsonStringAt(objectPayload, index, &afterKeyIndex);
        if (!key.has_value()) {
            break;
        }

        auto arrayStart = SkipJsonWhitespace(objectPayload, afterKeyIndex);
        if (arrayStart >= objectPayload.size() || objectPayload[arrayStart] != ':') {
            index = afterKeyIndex;
            continue;
        }
        arrayStart = SkipJsonWhitespace(objectPayload, arrayStart + 1);
        if (arrayStart >= objectPayload.size() || objectPayload[arrayStart] != '[') {
            index = afterKeyIndex;
            continue;
        }

        std::vector<std::string> values;
        index = arrayStart + 1;
        for (;;) {
            index = SkipJsonWhitespace(objectPayload, index);
            if (index >= objectPayload.size()) {
                return arrays;
            }
            if (objectPayload[index] == ']') {
                ++index;
                break;
            }
            if (objectPayload[index] != '"') {
                const auto closeIndex = objectPayload.find(']', index);
                if (closeIndex == std::string::npos) {
                    return arrays;
                }
                index = closeIndex + 1;
                break;
            }

            std::size_t afterValueIndex = index;
            const auto value = DecodeJsonStringAt(objectPayload, index, &afterValueIndex);
            if (!value.has_value()) {
                return arrays;
            }
            values.push_back(*value);
            index = SkipJsonWhitespace(objectPayload, afterValueIndex);
            if (index < objectPayload.size() && objectPayload[index] == ',') {
                ++index;
            }
        }

        arrays.push_back({*key, std::move(values)});
    }

    return arrays;
}

std::vector<std::string> SplitPipeFields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t startIndex = 0;
    while (startIndex <= line.size()) {
        const auto separatorIndex = line.find('|', startIndex);
        if (separatorIndex == std::string::npos) {
            fields.push_back(line.substr(startIndex));
            break;
        }
        fields.push_back(line.substr(startIndex, separatorIndex - startIndex));
        startIndex = separatorIndex + 1;
    }
    return fields;
}

void SortUnique(std::vector<std::string>* values) {
    if (values == nullptr) {
        return;
    }
    std::sort(values->begin(), values->end());
    values->erase(std::unique(values->begin(), values->end()), values->end());
}

std::vector<std::string> BuildCenterActivationPatterns(const std::string& prefix) {
    const auto normalizedPrefix = NormalizeAuthorityToken(prefix);
    if (normalizedPrefix.empty()) {
        return {};
    }

    std::vector<std::string> patterns{
        normalizedPrefix + "_CTR",
        normalizedPrefix + "_*_CTR",
        normalizedPrefix + "_FSS",
        normalizedPrefix + "_*_FSS",
    };
    SortUnique(&patterns);
    return patterns;
}

std::string NormalizeAuthorityFrequency(std::string frequency) {
    frequency.erase(
        std::remove_if(
            frequency.begin(),
            frequency.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        frequency.end());

    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }

        if (character == '.' && !sawDecimal) {
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

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool PatternFacilityMatches(const std::string& pattern, int vatsimFacility) {
    if (EndsWith(pattern, "_DEL") ||
        EndsWith(pattern, "_CLR") ||
        EndsWith(pattern, "_CLNC") ||
        EndsWith(pattern, "_CD")) {
        return vatsimFacility == kVatsimDeliveryFacility;
    }
    if (EndsWith(pattern, "_GND")) {
        return vatsimFacility == kVatsimGroundFacility;
    }
    if (EndsWith(pattern, "_TWR")) {
        return vatsimFacility == kVatsimTowerFacility;
    }
    if (EndsWith(pattern, "_APP") || EndsWith(pattern, "_DEP")) {
        return vatsimFacility == kVatsimApproachFacility;
    }
    if (EndsWith(pattern, "_CTR")) {
        return vatsimFacility == kVatsimCenterFacility;
    }
    if (EndsWith(pattern, "_FSS")) {
        return vatsimFacility == kVatsimFlightServiceFacility;
    }
    return false;
}

bool SplitTerminalCallsignPattern(
    const std::string& rawPattern,
    std::string* outPrefix,
    std::string* outSuffix) {
    const auto pattern = NormalizeControllerCallsign(rawPattern);
    if (pattern.find('*') != std::string::npos) {
        return false;
    }

    const auto separatorIndex = pattern.rfind('_');
    if (separatorIndex == std::string::npos ||
        separatorIndex == 0 ||
        separatorIndex >= pattern.size() - 1) {
        return false;
    }

    const auto suffix = pattern.substr(separatorIndex + 1);
    if (suffix != "APP" && suffix != "DEP") {
        return false;
    }

    if (outPrefix != nullptr) {
        *outPrefix = pattern.substr(0, separatorIndex);
    }
    if (outSuffix != nullptr) {
        *outSuffix = suffix;
    }
    return true;
}

void AddSourceBackedTerminalSectorPattern(
    AuthoritySource source,
    const std::string& normalizedPattern,
    std::vector<std::string>* patterns) {
    if (patterns == nullptr || source != AuthoritySource::SimAwareTracon) {
        return;
    }

    std::string prefix;
    std::string suffix;
    if (!SplitTerminalCallsignPattern(normalizedPattern, &prefix, &suffix)) {
        return;
    }

    patterns->push_back(prefix + "_*_" + suffix);
}

bool BareCenterAuthorityFacilityMatches(int vatsimFacility) {
    return vatsimFacility == kVatsimCenterFacility ||
           vatsimFacility == kVatsimFlightServiceFacility;
}

std::string SourcePrefix(AuthoritySource source) {
    switch (source) {
        case AuthoritySource::VatSpyFir:
            return "VATSPY_FIR";
        case AuthoritySource::VatSpyUir:
            return "VATSPY_UIR";
        case AuthoritySource::VatSpyBoundary:
            return "VATSPY_BOUNDARY";
        case AuthoritySource::SimAwareTracon:
            return "SIMAWARE_TRACON";
        case AuthoritySource::VatGlasses:
            return "VATGLASSES";
        case AuthoritySource::VatsimRadarExtension:
            return "VATSIM_RADAR_EXTENSION";
        case AuthoritySource::SpecialSectorData:
            return "SPECIAL_SECTOR_DATA";
        case AuthoritySource::AirportLocal:
            return "AIRPORT_LOCAL";
    }
    return "UNKNOWN";
}

std::string DefaultProofSourceForAuthoritySource(AuthoritySource source) {
    switch (source) {
        case AuthoritySource::VatSpyFir:
            return "VATSPY_FIR";
        case AuthoritySource::VatSpyUir:
            return "VATSPY_UIR";
        case AuthoritySource::VatSpyBoundary:
            return "VATSPY_BOUNDARY";
        case AuthoritySource::SimAwareTracon:
            return "SIMAWARE_TRACON";
        case AuthoritySource::VatGlasses:
            return "VATGLASSES_STATIC_OWNER";
        case AuthoritySource::VatsimRadarExtension:
            return "SPECIAL_SECTOR_DATA";
        case AuthoritySource::SpecialSectorData:
            return "SPECIAL_SECTOR_DATA";
        case AuthoritySource::AirportLocal:
            return "AIRPORT_LOCAL_FACILITY";
    }
    return "UNKNOWN";
}

AuthorityKind SourceDefaultKind(AuthoritySource source) {
    switch (source) {
        case AuthoritySource::SimAwareTracon:
        case AuthoritySource::AirportLocal:
            return AuthorityKind::Terminal;
        case AuthoritySource::VatsimRadarExtension:
            return AuthorityKind::Extension;
        case AuthoritySource::VatSpyFir:
        case AuthoritySource::VatSpyUir:
        case AuthoritySource::VatSpyBoundary:
        case AuthoritySource::VatGlasses:
        case AuthoritySource::SpecialSectorData:
            return AuthorityKind::Center;
    }
    return AuthorityKind::Center;
}

void AppendProofDetailField(
    std::ostringstream* stream,
    const std::string& key,
    const std::string& value) {
    if (stream == nullptr || key.empty() || value.empty()) {
        return;
    }
    if (stream->tellp() > 0) {
        *stream << ";";
    }
    *stream << key << "=" << value;
}

std::string BuildAuthorityProofDetail(
    const ControllerAuthority& authority,
    const std::string& matchedPattern,
    const std::string& normalizedFrequency,
    bool frequencyOwned) {
    std::ostringstream stream;
    AppendProofDetailField(&stream, "authoritySource", SourcePrefix(authority.source));
    AppendProofDetailField(&stream, "authorityId", authority.id);
    AppendProofDetailField(&stream, "polygonKey", authority.polygonKey);
    AppendProofDetailField(&stream, "pattern", matchedPattern);
    AppendProofDetailField(&stream, "source", authority.proofDetail);
    if (frequencyOwned) {
        AppendProofDetailField(&stream, "frequencyOwned", "1");
        AppendProofDetailField(&stream, "frequency", normalizedFrequency);
    }
    return stream.str();
}

void AddUniqueString(std::vector<std::string>* values, const std::string& value) {
    if (values == nullptr || value.empty()) {
        return;
    }
    if (std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

std::string JoinEvidenceItems(const std::vector<std::string>& values) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ">";
        }
        stream << values[index];
    }
    return stream.str();
}

std::string BuildDecisionProofDetail(const AuthorityEvidence& evidence) {
    std::ostringstream stream;
    stream << evidence.proofDetail;
    const auto proofItems = JoinEvidenceItems(evidence.proofItems);
    if (!proofItems.empty()) {
        if (stream.tellp() > 0) {
            stream << ";";
        }
        stream << "proofItems=" << proofItems;
    }
    return stream.str();
}

AuthorityEvidence BuildAuthorityEvidence(
    const ControllerAuthority& authority,
    const std::string& normalizedCallsign,
    const std::string& normalizedFrequency,
    const std::string& matchedPattern,
    int vatsimFacility,
    bool callsignMatched,
    bool facilityMatched,
    bool frequencyRequired,
    bool frequencyMatched) {
    AuthorityEvidence evidence;
    evidence.callsign = normalizedCallsign;
    evidence.frequency = normalizedFrequency;
    evidence.vatsimFacility = vatsimFacility;
    evidence.authorityId = authority.id;
    evidence.authoritySource = authority.source;
    evidence.authorityKind = authority.kind;
    evidence.polygonKey = authority.polygonKey;
    evidence.matchedPattern = matchedPattern;
    evidence.callsignMatched = callsignMatched;
    evidence.facilityMatched = facilityMatched;
    evidence.frequencyRequired = frequencyRequired;
    evidence.frequencyMatched = frequencyMatched;
    evidence.frequencyOwned = frequencyRequired && frequencyMatched;
    evidence.proofSource = authority.proofSource.empty()
                               ? DefaultProofSourceForAuthoritySource(authority.source)
                               : authority.proofSource;
    evidence.proofDetail = BuildAuthorityProofDetail(
        authority,
        matchedPattern,
        normalizedFrequency,
        frequencyRequired);

    if (authority.source == AuthoritySource::AirportLocal) {
        AddUniqueString(&evidence.proofItems, "airport-endpoint");
    }
    if (callsignMatched) {
        AddUniqueString(&evidence.proofItems, "callsign-pattern");
    }
    if (facilityMatched) {
        AddUniqueString(&evidence.proofItems, "facility-type");
    }
    if (frequencyRequired) {
        if (frequencyMatched) {
            AddUniqueString(&evidence.proofItems, "published-frequency");
            AddUniqueString(&evidence.proofItems, "FREQUENCY_OWNED_MATCH");
        } else {
            AddUniqueString(&evidence.rejectionReasons, "frequency-mismatch");
        }
    }
    if (!facilityMatched && callsignMatched) {
        AddUniqueString(&evidence.rejectionReasons, "facility-mismatch");
    }
    if (!callsignMatched && frequencyRequired && frequencyMatched && facilityMatched) {
        AddUniqueString(&evidence.rejectionReasons, "missing-source-ownership");
    }
    return evidence;
}

AuthorityDecision BuildAuthorityDecision(AuthorityEvidence evidence) {
    AuthorityDecision decision;
    decision.accepted =
        evidence.callsignMatched &&
        evidence.facilityMatched &&
        (!evidence.frequencyRequired || evidence.frequencyMatched);
    decision.evidence = std::move(evidence);
    if (decision.accepted) {
        decision.activeAuthority = {
            decision.evidence.callsign,
            decision.evidence.authorityId,
            decision.evidence.polygonKey,
            decision.evidence.matchedPattern,
            decision.evidence.authorityKind,
            decision.evidence.proofSource,
            BuildDecisionProofDetail(decision.evidence),
        };
    }
    return decision;
}

bool ShouldKeepRejectedDecision(const AuthorityEvidence& evidence) {
    return !evidence.rejectionReasons.empty() &&
           (evidence.callsignMatched || evidence.frequencyMatched);
}

bool AuthorityDecisionLess(
    const AuthorityDecision& left,
    const AuthorityDecision& right) {
    if (left.evidence.callsign != right.evidence.callsign) {
        return left.evidence.callsign < right.evidence.callsign;
    }
    if (left.accepted != right.accepted) {
        return left.accepted && !right.accepted;
    }
    if (left.evidence.authorityId != right.evidence.authorityId) {
        return left.evidence.authorityId < right.evidence.authorityId;
    }
    return left.evidence.matchedPattern < right.evidence.matchedPattern;
}

bool HasPrefix(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool ActiveAuthorityCanUsePolygon(
    const ActiveControllerAuthority& authorityMatch,
    const AuthorityPolygon& polygon) {
    if (HasPrefix(authorityMatch.authorityId, "AIRPORT_LOCAL:")) {
        return polygon.source == AuthoritySource::AirportLocal;
    }
    return true;
}

std::string PolygonIdFromRecord(const AuthorityPolygonSourceRecord& record) {
    const auto sourcePrefix = SourcePrefix(record.source);
    const auto baseId = NormalizeAuthorityToken(record.id);
    if (baseId.empty()) {
        return {};
    }

    if (record.source == AuthoritySource::SimAwareTracon) {
        // SimAware TRACON records use a missing suffix for approach coverage.
        const auto suffix = NormalizeAuthorityToken(record.suffix).empty()
                                ? std::string("APP")
                                : NormalizeAuthorityToken(record.suffix);
        return sourcePrefix + ":" + baseId + "_" + suffix;
    }

    return sourcePrefix + ":" + baseId;
}

std::string AuthorityIdFromPositionRecord(const AuthorityPositionSourceRecord& record) {
    const auto sourcePrefix = SourcePrefix(record.source);
    const auto baseId = NormalizeAuthorityToken(record.id);
    if (baseId.empty()) {
        return {};
    }

    return sourcePrefix + ":" + baseId;
}

AuthorityKind ParseAuthorityKindOrDefault(const std::string& value) {
    const auto normalized = NormalizeAuthorityToken(value);
    if (normalized == "TERMINAL" || normalized == "TRACON" ||
        normalized == "APP" || normalized == "APPROACH" ||
        normalized == "DEP" || normalized == "DEPARTURE" ||
        normalized == "TWR" || normalized == "TOWER" ||
        normalized == "GND" || normalized == "GROUND" ||
        normalized == "DEL" || normalized == "DELIVERY" ||
        normalized == "CLR" || normalized == "CLNC" || normalized == "CD") {
        return AuthorityKind::Terminal;
    }
    if (normalized == "EXTENSION") {
        return AuthorityKind::Extension;
    }
    return AuthorityKind::Center;
}

std::string FirstJsonStringField(
    const std::string& objectPayload,
    const std::vector<std::string>& fieldNames) {
    for (const auto& fieldName : fieldNames) {
        const auto value = ExtractJsonStringField(objectPayload, fieldName);
        if (value.has_value() && !Trim(*value).empty()) {
            return *value;
        }
    }
    return {};
}

std::vector<std::string> FirstJsonStringArrayField(
    const std::string& objectPayload,
    const std::vector<std::string>& fieldNames) {
    for (const auto& fieldName : fieldNames) {
        auto values = ExtractJsonStringArrayField(objectPayload, fieldName);
        if (!values.empty()) {
            return values;
        }
    }
    return {};
}

bool PayloadAllowsAuthoritySource(AuthoritySource source, const std::string& payload) {
    const auto sourceField = NormalizeAuthorityToken(
        FirstJsonStringField(payload, {"source", "source_type", "sourceType"}));
    if (sourceField.empty()) {
        return source != AuthoritySource::SpecialSectorData &&
               source != AuthoritySource::SimAwareTracon;
    }

    const auto sourcePrefix = NormalizeAuthorityToken(SourcePrefix(source));
    if (sourceField == sourcePrefix) {
        return true;
    }
    if (source == AuthoritySource::VatGlasses && sourceField == "VATGLASSES") {
        return true;
    }
    if (source == AuthoritySource::SimAwareTracon &&
        (sourceField == "SIMAWARE" ||
         sourceField == "TRACON" ||
         sourceField == "TERMINAL" ||
         sourceField == "TERMINAL_AUTHORITY" ||
         sourceField == "TERMINAL_AUTHORITY_DATA")) {
        return true;
    }
    if (source == AuthoritySource::VatsimRadarExtension &&
        (sourceField == "VATSIM_RADAR" || sourceField == "EXTENSION")) {
        return true;
    }
    if (source == AuthoritySource::SpecialSectorData &&
        (sourceField == "SPECIAL" || sourceField == "SPECIAL_SECTOR" ||
         sourceField == "SPECIAL_SECTORS" || sourceField == "SECTOR_DATA")) {
        return true;
    }
    return false;
}

std::vector<std::string> BuildVatGlassesCallsignPatterns(
    const std::vector<std::string>& rawPrefixes,
    const std::string& rawType) {
    const auto type = NormalizeAuthorityToken(rawType);
    if (type.empty()) {
        return {};
    }

    std::vector<std::string> patterns;
    for (const auto& rawPrefix : rawPrefixes) {
        const auto prefix = NormalizeAuthorityToken(rawPrefix);
        if (prefix.empty()) {
            continue;
        }

        patterns.push_back(prefix + "_" + type);
        patterns.push_back(prefix + "_*_" + type);
    }
    SortUnique(&patterns);
    return patterns;
}

std::unordered_map<std::string, std::vector<std::string>>
ExtractVatGlassesStaticOwnerGroups(const std::string& payload) {
    std::unordered_map<std::string, std::vector<std::string>> groupsByOwner;
    const auto airspaceArrayPayload = ExtractJsonArrayPayload(payload, "airspace");
    if (!airspaceArrayPayload.has_value()) {
        return groupsByOwner;
    }

    for (const auto& objectPayload : ExtractJsonObjectsFromArrayPayload(*airspaceArrayPayload)) {
        const auto group = NormalizeAuthorityToken(
            FirstJsonStringField(objectPayload, {"group"}));
        if (group.empty()) {
            continue;
        }

        for (const auto& owner : FirstJsonStringArrayField(objectPayload, {"owner"})) {
            const auto normalizedOwner = NormalizeAuthorityToken(owner);
            if (normalizedOwner.empty()) {
                continue;
            }
            groupsByOwner[normalizedOwner].push_back(group);
        }
    }

    for (auto& [_, groups] : groupsByOwner) {
        SortUnique(&groups);
    }
    return groupsByOwner;
}

std::optional<std::string> ExtractVatGlassesDynamicAirspacePayload(
    const std::string& payload) {
    for (const auto& candidatePayload : ExtractJsonObjectPayloads(payload, "airspace")) {
        for (const auto& [_, airspacePayload] :
             ExtractJsonNamedObjectsFromObjectPayload(candidatePayload)) {
            if (ExtractJsonArrayPayload(airspacePayload, "sectors").has_value()) {
                return candidatePayload;
            }
        }
    }
    return std::nullopt;
}

std::unordered_map<std::string, std::vector<std::string>>
ExtractVatGlassesDynamicOwnerAirspaces(const std::string& payload) {
    std::unordered_map<std::string, std::vector<std::string>> airspacesByOwner;

    for (const auto& ownershipPayload : ExtractJsonObjectPayloads(payload, "ownership")) {
        const auto ownershipAirspacePayload =
            ExtractJsonObjectPayload(ownershipPayload, "airspace");
        if (!ownershipAirspacePayload.has_value()) {
            continue;
        }

        for (const auto& [rawAirspaceId, rawOwners] :
             ExtractJsonNamedStringArraysFromObjectPayload(*ownershipAirspacePayload)) {
            const auto airspaceId = NormalizeAuthorityToken(rawAirspaceId);
            if (airspaceId.empty()) {
                continue;
            }

            for (const auto& rawOwner : rawOwners) {
                const auto owner = NormalizeAuthorityToken(rawOwner);
                if (owner.empty()) {
                    continue;
                }
                airspacesByOwner[owner].push_back(airspaceId);
            }
        }
    }

    for (auto& [_, airspaces] : airspacesByOwner) {
        SortUnique(&airspaces);
    }
    return airspacesByOwner;
}

std::vector<AuthorityPositionSourceRecord> ParseVatGlassesPositionObjectRecords(
    AuthoritySource source,
    const std::string& payload) {
    std::vector<AuthorityPositionSourceRecord> records;
    const auto positionsPayload = ExtractJsonObjectPayload(payload, "positions");
    if (!positionsPayload.has_value()) {
        return records;
    }

    const auto groupsByOwner = ExtractVatGlassesStaticOwnerGroups(payload);
    const auto dynamicAirspacesByOwner =
        ExtractVatGlassesDynamicOwnerAirspaces(payload);
    for (const auto& [rawPositionId, objectPayload] :
         ExtractJsonNamedObjectsFromObjectPayload(*positionsPayload)) {
        AuthorityPositionSourceRecord baseRecord;
        baseRecord.source = source;
        baseRecord.sourceRecord = objectPayload;
        baseRecord.id = rawPositionId;
        baseRecord.name = FirstJsonStringField(objectPayload, {"callsign", "name", "title"});
        baseRecord.frequency = FirstJsonStringField(objectPayload, {"frequency"});
        baseRecord.proofSource = DefaultProofSourceForAuthoritySource(source);
        baseRecord.proofDetail =
            "position=" + NormalizeAuthorityToken(rawPositionId);
        baseRecord.kind = ParseAuthorityKindOrDefault(
            FirstJsonStringField(objectPayload, {"type"}));
        baseRecord.controllerCallsignPatterns = FirstJsonStringArrayField(
            objectPayload,
            {"callsign_patterns", "callsignPatterns", "patterns", "callsigns"});

        const auto singleCallsign = FirstJsonStringField(objectPayload, {"callsign_pattern"});
        if (!singleCallsign.empty()) {
            baseRecord.controllerCallsignPatterns.push_back(singleCallsign);
        }

        const auto prefixes = FirstJsonStringArrayField(objectPayload, {"pre", "prefixes"});
        const auto type = FirstJsonStringField(objectPayload, {"type"});
        std::vector<std::string> normalizedPrefixes;
        if (baseRecord.kind == AuthorityKind::Center) {
            for (const auto& prefix : prefixes) {
                const auto normalizedPrefix = NormalizeAuthorityToken(prefix);
                if (!normalizedPrefix.empty()) {
                    normalizedPrefixes.push_back(normalizedPrefix);
                }
            }
            SortUnique(&normalizedPrefixes);
        }
        const auto generatedPatterns = BuildVatGlassesCallsignPatterns(prefixes, type);
        baseRecord.controllerCallsignPatterns.insert(
            baseRecord.controllerCallsignPatterns.end(),
            generatedPatterns.begin(),
            generatedPatterns.end());
        SortUnique(&baseRecord.controllerCallsignPatterns);

        const auto ownerKey = NormalizeAuthorityToken(rawPositionId);
        const auto dynamicAirspaces = dynamicAirspacesByOwner.find(ownerKey);
        if (dynamicAirspaces != dynamicAirspacesByOwner.end() &&
            !dynamicAirspaces->second.empty()) {
            for (const auto& airspaceId : dynamicAirspaces->second) {
                auto record = baseRecord;
                record.polygonKey = airspaceId;
                record.proofSource =
                    source == AuthoritySource::VatGlasses
                        ? std::string("VATGLASSES_DYNAMIC_OWNER")
                        : DefaultProofSourceForAuthoritySource(source);
                record.proofDetail =
                    "position=" + ownerKey + ";dynamicAirspace=" + airspaceId;
                records.push_back(std::move(record));
            }
            continue;
        }

        const auto ownerGroups = groupsByOwner.find(ownerKey);
        std::unordered_set<std::string> emittedPolygonKeys;
        auto pushStaticRecord = [&](
            const std::string& rawPolygonKey,
            const std::string& proofTag) {
            const auto polygonKey = NormalizeAuthorityToken(rawPolygonKey);
            if (polygonKey.empty() ||
                !emittedPolygonKeys.insert(polygonKey).second) {
                return;
            }

            auto record = baseRecord;
            record.polygonKey = polygonKey;
            record.proofDetail = "position=" + ownerKey + ";" + proofTag + "=" + polygonKey;
            records.push_back(std::move(record));
        };

        const auto explicitPolygonKey = FirstJsonStringField(
            objectPayload,
            {"polygon_key", "polygonKey", "polygon", "sector", "airspace_id", "airspaceId"});
        pushStaticRecord(explicitPolygonKey, "staticPolygon");
        pushStaticRecord(ownerKey, "staticOwnerGroup");
        if (ownerGroups != groupsByOwner.end()) {
            for (const auto& group : ownerGroups->second) {
                pushStaticRecord(group, "staticOwnerGroup");
            }
        }
        if (baseRecord.kind == AuthorityKind::Center) {
            for (const auto& prefix : normalizedPrefixes) {
                pushStaticRecord(prefix, "sourcePre");
            }
        }

        if (emittedPolygonKeys.empty()) {
            records.push_back(std::move(baseRecord));
        }
    }

    return records;
}

std::optional<double> ParseVatGlassesDmsCoordinate(std::string value) {
    value = Trim(std::move(value));
    if (value.empty()) {
        return std::nullopt;
    }

    double sign = 1.0;
    if (value.front() == '-') {
        sign = -1.0;
        value.erase(value.begin());
    } else if (value.front() == '+') {
        value.erase(value.begin());
    }

    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }),
        value.end());

    if (value.size() < 5 ||
        std::any_of(
            value.begin(),
            value.end(),
            [](unsigned char c) { return std::isdigit(c) == 0; })) {
        return std::nullopt;
    }

    const auto degreeDigits = value.size() - 4;
    try {
        const auto degrees = std::stod(value.substr(0, degreeDigits));
        const auto minutes = std::stod(value.substr(degreeDigits, 2));
        const auto seconds = std::stod(value.substr(degreeDigits + 2, 2));
        if (minutes >= 60.0 || seconds >= 60.0) {
            return std::nullopt;
        }
        return sign * (degrees + minutes / 60.0 + seconds / 3600.0);
    } catch (...) {
        return std::nullopt;
    }
}

AuthorityPolygonRing ExtractVatGlassesPointsRing(const std::string& sectorPayload) {
    AuthorityPolygonRing ring;
    const auto pointsPayload = ExtractJsonArrayPayload(sectorPayload, "points");
    if (!pointsPayload.has_value()) {
        return ring;
    }

    for (std::size_t index = 0; index < pointsPayload->size();) {
        index = SkipJsonWhitespace(*pointsPayload, index);
        if (index >= pointsPayload->size()) {
            break;
        }
        if ((*pointsPayload)[index] != '[') {
            ++index;
            continue;
        }

        index = SkipJsonWhitespace(*pointsPayload, index + 1);
        if (index >= pointsPayload->size() || (*pointsPayload)[index] != '"') {
            continue;
        }
        std::size_t afterLatitudeIndex = index;
        const auto rawLatitude =
            DecodeJsonStringAt(*pointsPayload, index, &afterLatitudeIndex);
        if (!rawLatitude.has_value()) {
            continue;
        }

        index = SkipJsonWhitespace(*pointsPayload, afterLatitudeIndex);
        if (index >= pointsPayload->size() || (*pointsPayload)[index] != ',') {
            continue;
        }
        index = SkipJsonWhitespace(*pointsPayload, index + 1);
        if (index >= pointsPayload->size() || (*pointsPayload)[index] != '"') {
            continue;
        }
        std::size_t afterLongitudeIndex = index;
        const auto rawLongitude =
            DecodeJsonStringAt(*pointsPayload, index, &afterLongitudeIndex);
        if (!rawLongitude.has_value()) {
            continue;
        }

        const auto latitude = ParseVatGlassesDmsCoordinate(*rawLatitude);
        const auto longitude = ParseVatGlassesDmsCoordinate(*rawLongitude);
        if (latitude.has_value() && longitude.has_value()) {
            ring.points.push_back({*latitude, *longitude});
        }

        const auto closeIndex = pointsPayload->find(']', afterLongitudeIndex);
        if (closeIndex == std::string::npos) {
            break;
        }
        index = closeIndex + 1;
    }

    return ring;
}

std::vector<AuthorityPolygonSourceRecord> ParseVatGlassesStaticAirspacePolygons(
    AuthoritySource source,
    const std::string& payload) {
    std::vector<AuthorityPolygonSourceRecord> records;
    const auto airspaceArrayPayload = ExtractJsonArrayPayload(payload, "airspace");
    if (!airspaceArrayPayload.has_value()) {
        return records;
    }

    std::size_t airspaceIndex = 0;
    for (const auto& objectPayload : ExtractJsonObjectsFromArrayPayload(*airspaceArrayPayload)) {
        const auto airspaceName = FirstJsonStringField(objectPayload, {"id", "name"});
        const auto airspaceKey = NormalizeAuthorityToken(airspaceName);
        const auto groupKey = NormalizeAuthorityToken(
            FirstJsonStringField(objectPayload, {"group"}));
        auto owners = FirstJsonStringArrayField(objectPayload, {"owner"});
        if (owners.empty() && !groupKey.empty()) {
            owners.push_back(groupKey);
        }

        const auto sectorsPayload = ExtractJsonArrayPayload(objectPayload, "sectors");
        if (!sectorsPayload.has_value()) {
            ++airspaceIndex;
            continue;
        }

        std::size_t sectorIndex = 0;
        for (const auto& sectorPayload : ExtractJsonObjectsFromArrayPayload(*sectorsPayload)) {
            const auto ring = ExtractVatGlassesPointsRing(sectorPayload);
            if (ring.points.size() < 3) {
                ++sectorIndex;
                continue;
            }

            for (const auto& owner : owners) {
                const auto ownerKey = NormalizeAuthorityToken(owner);
                if (ownerKey.empty()) {
                    continue;
                }

                AuthorityPolygonSourceRecord record;
                record.source = source;
                record.id = ownerKey + "_" +
                            (!airspaceKey.empty()
                                 ? airspaceKey
                                 : ("AIRSPACE_" + std::to_string(airspaceIndex))) +
                            "_" + std::to_string(sectorIndex);
                record.name = airspaceName;
                record.lookupTokens.push_back(ownerKey);
                if (!groupKey.empty()) {
                    record.lookupTokens.push_back(groupKey);
                }
                if (!airspaceKey.empty()) {
                    record.lookupTokens.push_back(airspaceKey);
                }
                SortUnique(&record.lookupTokens);
                record.rings.push_back(ring);
                record.sourceRecord = airspaceName;
                records.push_back(std::move(record));
            }

            ++sectorIndex;
        }

        ++airspaceIndex;
    }

    return records;
}

std::vector<AuthorityPolygonSourceRecord> ParseVatGlassesDynamicAirspacePolygons(
    AuthoritySource source,
    const std::string& payload) {
    std::vector<AuthorityPolygonSourceRecord> records;
    const auto airspaceObjectPayload =
        ExtractVatGlassesDynamicAirspacePayload(payload);
    if (!airspaceObjectPayload.has_value()) {
        return records;
    }

    for (const auto& [rawAirspaceId, objectPayload] :
         ExtractJsonNamedObjectsFromObjectPayload(*airspaceObjectPayload)) {
        const auto airspaceId = NormalizeAuthorityToken(rawAirspaceId);
        if (airspaceId.empty()) {
            continue;
        }

        const auto airspaceName = FirstJsonStringField(objectPayload, {"id", "name"});
        const auto airspaceNameKey = NormalizeAuthorityToken(airspaceName);
        const auto groupKey = NormalizeAuthorityToken(
            FirstJsonStringField(objectPayload, {"group"}));

        const auto sectorsPayload = ExtractJsonArrayPayload(objectPayload, "sectors");
        if (!sectorsPayload.has_value()) {
            continue;
        }

        std::size_t sectorIndex = 0;
        for (const auto& sectorPayload : ExtractJsonObjectsFromArrayPayload(*sectorsPayload)) {
            const auto ring = ExtractVatGlassesPointsRing(sectorPayload);
            if (ring.points.size() < 3) {
                ++sectorIndex;
                continue;
            }

            AuthorityPolygonSourceRecord record;
            record.source = source;
            record.id = airspaceId + "_" + std::to_string(sectorIndex);
            record.name = airspaceName;
            record.lookupTokens.push_back(airspaceId);
            if (!airspaceNameKey.empty()) {
                record.lookupTokens.push_back(airspaceNameKey);
            }
            if (!groupKey.empty()) {
                record.lookupTokens.push_back(groupKey);
            }
            SortUnique(&record.lookupTokens);
            record.rings.push_back(ring);
            record.sourceRecord = airspaceName.empty() ? rawAirspaceId : airspaceName;
            records.push_back(std::move(record));
            ++sectorIndex;
        }
    }

    return records;
}

void AddLookupKey(std::vector<std::string>* lookupKeys, const std::string& rawKey) {
    if (lookupKeys == nullptr) {
        return;
    }
    const auto key = NormalizeAuthorityToken(rawKey);
    if (!key.empty()) {
        lookupKeys->push_back(key);
    }
}

void AddSimAwareTraconLookupKeys(
    const AuthorityPolygonSourceRecord& record,
    std::vector<std::string>* lookupKeys) {
    if (lookupKeys == nullptr) {
        return;
    }

    const auto id = NormalizeAuthorityToken(record.id);
    const auto suffix = NormalizeAuthorityToken(record.suffix).empty()
                            ? std::string("APP")
                            : NormalizeAuthorityToken(record.suffix);
    AddLookupKey(lookupKeys, id);
    AddLookupKey(lookupKeys, id + "_" + suffix);
    for (const auto& prefix : record.prefixes) {
        const auto normalizedPrefix = NormalizeAuthorityToken(prefix);
        if (normalizedPrefix.empty()) {
            continue;
        }
        AddLookupKey(lookupKeys, normalizedPrefix);
        AddLookupKey(lookupKeys, normalizedPrefix + "_" + suffix);
    }
}

bool RingIsValid(const AuthorityPolygonRing& ring) {
    return ring.points.size() >= 3;
}

std::vector<AuthorityPolygonRing> ValidRings(
    const std::vector<AuthorityPolygonRing>& rings) {
    std::vector<AuthorityPolygonRing> validRings;
    for (const auto& ring : rings) {
        if (RingIsValid(ring)) {
            validRings.push_back(ring);
        }
    }
    return validRings;
}

bool PolygonMatchesAuthorityKey(
    const AuthorityPolygon& polygon,
    const std::string& rawAuthorityKey) {
    const auto authorityKey = NormalizeAuthorityToken(rawAuthorityKey);
    if (authorityKey.empty()) {
        return false;
    }
    if (polygon.polygonKey == authorityKey) {
        return true;
    }
    return std::find(
               polygon.lookupKeys.begin(),
               polygon.lookupKeys.end(),
               authorityKey) != polygon.lookupKeys.end();
}

bool CrossesAntiMeridian(double longitudeDegA, double longitudeDegB) {
    return std::fabs(
               NormalizeLongitudeDeg(longitudeDegB) -
               NormalizeLongitudeDeg(longitudeDegA)) > 180.0;
}

bool RingCrossesAntiMeridian(const AuthorityPolygonRing& ring) {
    double minLongitudeDeg = std::numeric_limits<double>::max();
    double maxLongitudeDeg = std::numeric_limits<double>::lowest();
    for (std::size_t index = 0; index < ring.points.size(); ++index) {
        const auto longitudeDeg = NormalizeLongitudeDeg(ring.points[index].longitudeDeg);
        minLongitudeDeg = std::min(minLongitudeDeg, longitudeDeg);
        maxLongitudeDeg = std::max(maxLongitudeDeg, longitudeDeg);
        if (index > 0 &&
            CrossesAntiMeridian(
                ring.points[index - 1].longitudeDeg,
                ring.points[index].longitudeDeg)) {
            return true;
        }
    }

    return (maxLongitudeDeg - minLongitudeDeg) > 180.0;
}

bool PointInRing(const GeoPoint& point, const AuthorityPolygonRing& ring) {
    bool inside = false;
    const auto count = ring.points.size();
    if (count < 3) {
        return false;
    }

    const auto crossesAntiMeridian = RingCrossesAntiMeridian(ring);
    const auto pointLongitudeDeg = NormalizeLongitudeDeg(point.longitudeDeg);

    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const auto& pi = ring.points[i];
        const auto& pj = ring.points[j];
        const auto piLongitudeDeg =
            crossesAntiMeridian
                ? UnwrapLongitudeRelativeDeg(pointLongitudeDeg, pi.longitudeDeg)
                : pi.longitudeDeg;
        const auto pjLongitudeDeg =
            crossesAntiMeridian
                ? UnwrapLongitudeRelativeDeg(pointLongitudeDeg, pj.longitudeDeg)
                : pj.longitudeDeg;
        const auto intersects =
            ((pi.latitudeDeg > point.latitudeDeg) != (pj.latitudeDeg > point.latitudeDeg)) &&
            (pointLongitudeDeg <
             (pjLongitudeDeg - piLongitudeDeg) * (point.latitudeDeg - pi.latitudeDeg) /
                     ((pj.latitudeDeg - pi.latitudeDeg) == 0.0
                          ? 1e-12
                          : (pj.latitudeDeg - pi.latitudeDeg)) +
                 piLongitudeDeg);
        if (intersects) {
            inside = !inside;
        }
    }

    return inside;
}

bool PointInPolygon(const GeoPoint& point, const AuthorityPolygon& polygon) {
    for (const auto& ring : polygon.rings) {
        if (PointInRing(point, ring)) {
            return true;
        }
    }
    return false;
}

Point2D ProjectPointForSegment(
    const GeoPoint& point,
    double referenceLatitudeDeg,
    double referenceLongitudeDeg) {
    const auto unwrappedLongitudeDeg =
        UnwrapLongitudeRelativeDeg(referenceLongitudeDeg, point.longitudeDeg);
    const auto cosReferenceLatitude =
        std::max(std::cos(ToRadians(referenceLatitudeDeg)), 1e-6);
    return {
        (unwrappedLongitudeDeg - referenceLongitudeDeg) * cosReferenceLatitude,
        point.latitudeDeg - referenceLatitudeDeg,
    };
}

double Cross2D(const Point2D& left, const Point2D& right) {
    return left.x * right.y - left.y * right.x;
}

Point2D Subtract2D(const Point2D& left, const Point2D& right) {
    return {left.x - right.x, left.y - right.y};
}

std::optional<double> SegmentIntersectionFraction(
    const Point2D& segmentStart,
    const Point2D& segmentEnd,
    const Point2D& edgeStart,
    const Point2D& edgeEnd) {
    constexpr double kIntersectionTolerance = 1e-9;

    const auto segmentDelta = Subtract2D(segmentEnd, segmentStart);
    const auto edgeDelta = Subtract2D(edgeEnd, edgeStart);
    const auto denominator = Cross2D(segmentDelta, edgeDelta);
    if (std::fabs(denominator) <= kIntersectionTolerance) {
        return std::nullopt;
    }

    const auto delta = Subtract2D(edgeStart, segmentStart);
    const auto segmentFraction = Cross2D(delta, edgeDelta) / denominator;
    const auto edgeFraction = Cross2D(delta, segmentDelta) / denominator;
    if (segmentFraction < -kIntersectionTolerance ||
        segmentFraction > 1.0 + kIntersectionTolerance ||
        edgeFraction < -kIntersectionTolerance ||
        edgeFraction > 1.0 + kIntersectionTolerance) {
        return std::nullopt;
    }

    return std::clamp(segmentFraction, 0.0, 1.0);
}

std::vector<double> CollectSegmentBoundaryFractions(
    const GeoPoint& start,
    const GeoPoint& end,
    const AuthorityPolygon& polygon) {
    std::vector<double> fractions;
    const auto referenceLatitudeDeg = (start.latitudeDeg + end.latitudeDeg) / 2.0;
    const auto referenceLongitudeDeg =
        NormalizeLongitudeDeg(
            start.longitudeDeg +
            ShortestLongitudeDeltaDeg(start.longitudeDeg, end.longitudeDeg) / 2.0);
    const auto projectedStart =
        ProjectPointForSegment(start, referenceLatitudeDeg, referenceLongitudeDeg);
    const auto projectedEnd =
        ProjectPointForSegment(end, referenceLatitudeDeg, referenceLongitudeDeg);

    for (const auto& ring : polygon.rings) {
        const auto count = ring.points.size();
        if (count < 2) {
            continue;
        }

        for (std::size_t index = 0; index < count; ++index) {
            const auto& edgeStartPoint = ring.points[index];
            const auto& edgeEndPoint = ring.points[(index + 1) % count];
            const auto projectedEdgeStart =
                ProjectPointForSegment(
                    edgeStartPoint,
                    referenceLatitudeDeg,
                    referenceLongitudeDeg);
            const auto projectedEdgeEnd =
                ProjectPointForSegment(
                    edgeEndPoint,
                    referenceLatitudeDeg,
                    referenceLongitudeDeg);
            const auto fraction = SegmentIntersectionFraction(
                projectedStart,
                projectedEnd,
                projectedEdgeStart,
                projectedEdgeEnd);
            if (fraction.has_value()) {
                fractions.push_back(*fraction);
            }
        }
    }

    std::sort(fractions.begin(), fractions.end());
    fractions.erase(
        std::unique(
            fractions.begin(),
            fractions.end(),
            [](double left, double right) { return std::fabs(left - right) <= 1e-6; }),
        fractions.end());
    return fractions;
}

std::optional<double> FindEntryFractionByBinarySearch(
    const GeoPoint& start,
    const GeoPoint& end,
    const AuthorityPolygon& polygon) {
    const auto startInside = PointInPolygon(start, polygon);
    const auto endInside = PointInPolygon(end, polygon);
    if (startInside || !endInside) {
        return std::nullopt;
    }

    double outsideFraction = 0.0;
    double insideFraction = 1.0;
    for (int iteration = 0; iteration < 48; ++iteration) {
        const auto middleFraction = (outsideFraction + insideFraction) / 2.0;
        const auto middlePoint = InterpolatePoint(start, end, middleFraction);
        if (PointInPolygon(middlePoint, polygon)) {
            insideFraction = middleFraction;
        } else {
            outsideFraction = middleFraction;
        }
    }

    return insideFraction;
}

std::optional<double> FindPolygonEntryFraction(
    const GeoPoint& start,
    const GeoPoint& end,
    const AuthorityPolygon& polygon) {
    if (PointInPolygon(start, polygon)) {
        return 0.0;
    }

    const auto boundaryFractions = CollectSegmentBoundaryFractions(start, end, polygon);
    for (const auto fraction : boundaryFractions) {
        const auto beforeFraction = std::max(0.0, fraction - 1e-6);
        const auto afterFraction = std::min(1.0, fraction + 1e-6);
        const auto beforePoint = InterpolatePoint(start, end, beforeFraction);
        const auto afterPoint = InterpolatePoint(start, end, afterFraction);
        const auto beforeInside = PointInPolygon(beforePoint, polygon);
        const auto afterInside = PointInPolygon(afterPoint, polygon);
        if (!beforeInside && afterInside) {
            return fraction;
        }
    }

    return FindEntryFractionByBinarySearch(start, end, polygon);
}

std::optional<double> FindRouteEntryDistanceNm(
    const std::vector<GeoPoint>& routePoints,
    const AuthorityPolygon& polygon) {
    if (routePoints.empty()) {
        return std::nullopt;
    }
    if (PointInPolygon(routePoints.front(), polygon)) {
        return 0.0;
    }

    double accumulatedDistanceNm = 0.0;
    for (std::size_t index = 1; index < routePoints.size(); ++index) {
        const auto& start = routePoints[index - 1];
        const auto& end = routePoints[index];
        const auto segmentDistanceNm = GreatCircleDistanceNm(
            start.latitudeDeg,
            start.longitudeDeg,
            end.latitudeDeg,
            end.longitudeDeg);
        const auto entryFraction = FindPolygonEntryFraction(start, end, polygon);
        if (entryFraction.has_value()) {
            return accumulatedDistanceNm + segmentDistanceNm * *entryFraction;
        }
        accumulatedDistanceNm += segmentDistanceNm;
    }

    return std::nullopt;
}

const AuthorityPolygon* FindPolygonById(
    const AuthorityPolygonCatalog& catalog,
    const std::string& polygonId) {
    for (const auto& polygon : catalog.polygons) {
        if (polygon.id == polygonId) {
            return &polygon;
        }
    }
    return nullptr;
}

}  // namespace

std::string AuthoritySourceLabel(AuthoritySource source) {
    return SourcePrefix(source);
}

std::string NormalizeAuthorityToken(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '_' || character == '-') {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

std::string NormalizeControllerCallsign(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }
    return normalized;
}

bool CallsignMatchesPattern(
    const std::string& rawPattern,
    const std::string& rawCallsign) {
    const auto pattern = NormalizeControllerCallsign(rawPattern);
    const auto callsign = NormalizeControllerCallsign(rawCallsign);
    if (pattern.empty() || callsign.empty()) {
        return false;
    }

    const auto wildcardIndex = pattern.find('*');
    if (wildcardIndex == std::string::npos) {
        return pattern == callsign;
    }
    if (pattern.find('*', wildcardIndex + 1) != std::string::npos) {
        return false;
    }

    const auto prefix = pattern.substr(0, wildcardIndex);
    const auto suffix = pattern.substr(wildcardIndex + 1);
    if (callsign.size() < prefix.size() + suffix.size()) {
        return false;
    }

    return callsign.compare(0, prefix.size(), prefix) == 0 &&
           callsign.compare(callsign.size() - suffix.size(), suffix.size(), suffix) == 0;
}

ControllerAuthorityCatalog CompileVatSpyAuthorityCatalog(
    const std::string& vatspyDat) {
    enum class Section {
        None,
        Firs,
        Uirs,
    };

    ControllerAuthorityCatalog catalog;
    Section currentSection = Section::None;

    std::istringstream stream(vatspyDat);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto trimmedLine = Trim(line);
        if (trimmedLine.empty() || trimmedLine.front() == ';') {
            continue;
        }

        if (trimmedLine == "[FIRs]") {
            currentSection = Section::Firs;
            continue;
        }
        if (trimmedLine == "[UIRs]") {
            currentSection = Section::Uirs;
            continue;
        }
        if (trimmedLine.front() == '[') {
            currentSection = Section::None;
            continue;
        }
        if (currentSection != Section::Firs && currentSection != Section::Uirs) {
            continue;
        }

        const auto fields = SplitPipeFields(trimmedLine);
        if (fields.size() < 4) {
            continue;
        }

        const auto sectorIdentifier = NormalizeAuthorityToken(fields[0]);
        const auto callsignPrefix = NormalizeAuthorityToken(fields[2]);
        auto boundaryIdentifier = NormalizeAuthorityToken(fields[3]);
        if (boundaryIdentifier.empty()) {
            boundaryIdentifier = sectorIdentifier;
        }
        if (sectorIdentifier.empty() && boundaryIdentifier.empty()) {
            continue;
        }

        const auto source =
            currentSection == Section::Uirs ? AuthoritySource::VatSpyUir : AuthoritySource::VatSpyFir;
        ControllerAuthority authority;
        authority.source = source;
        authority.kind = AuthorityKind::Center;
        authority.name = Trim(fields[1]);
        authority.polygonKey = boundaryIdentifier;
        authority.id = SourcePrefix(source) + ":" +
                       (!sectorIdentifier.empty() ? sectorIdentifier : boundaryIdentifier);
        authority.sourceRecord = trimmedLine;
        authority.proofSource = DefaultProofSourceForAuthoritySource(source);
        authority.proofDetail =
            "vatspyRecord=" + sectorIdentifier +
            ";boundary=" + boundaryIdentifier +
            ";prefix=" + callsignPrefix;
        AddLookupKey(&authority.lookupKeys, sectorIdentifier);
        AddLookupKey(&authority.lookupKeys, boundaryIdentifier);
        SortUnique(&authority.lookupKeys);

        if (!callsignPrefix.empty()) {
            authority.controllerPrefixes.push_back(callsignPrefix);
            SortUnique(&authority.controllerPrefixes);
            authority.controllerCallsignPatterns =
                BuildCenterActivationPatterns(callsignPrefix);
        } else {
            catalog.dataGaps.push_back({
                authority.id,
                authority.polygonKey,
                "missing-callsign-prefix",
                authority.sourceRecord,
            });
        }

        catalog.authorities.push_back(std::move(authority));
    }

    std::sort(
        catalog.authorities.begin(),
        catalog.authorities.end(),
        [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    std::sort(
        catalog.dataGaps.begin(),
        catalog.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            return left.reason < right.reason;
        });
    return catalog;
}

ControllerAuthorityCatalog CompileAuthorityPositionCatalog(
    const std::vector<AuthorityPositionSourceRecord>& sourceRecords) {
    ControllerAuthorityCatalog catalog;

    for (const auto& record : sourceRecords) {
        const auto authorityId = AuthorityIdFromPositionRecord(record);
        const auto polygonKey = NormalizeAuthorityToken(record.polygonKey);
        if (authorityId.empty()) {
            catalog.dataGaps.push_back({
                SourcePrefix(record.source) + ":<missing-id>",
                polygonKey,
                "missing-position-id",
                record.sourceRecord,
            });
            continue;
        }

        ControllerAuthority authority;
        authority.id = authorityId;
        authority.source = record.source;
        authority.kind = record.kind;
        authority.name = Trim(record.name);
        authority.polygonKey = polygonKey;
        authority.sourceRecord = record.sourceRecord;
        authority.proofSource = record.proofSource.empty()
                                    ? DefaultProofSourceForAuthoritySource(record.source)
                                    : record.proofSource;
        authority.proofDetail = record.proofDetail;
        AddLookupKey(&authority.lookupKeys, record.id);
        AddLookupKey(&authority.lookupKeys, record.polygonKey);

        for (const auto& pattern : record.controllerCallsignPatterns) {
            const auto normalizedPattern = NormalizeControllerCallsign(pattern);
            if (!normalizedPattern.empty()) {
                authority.controllerCallsignPatterns.push_back(normalizedPattern);
                AddSourceBackedTerminalSectorPattern(
                    record.source,
                    normalizedPattern,
                    &authority.controllerCallsignPatterns);
            }
        }
        const auto normalizedFrequency = NormalizeAuthorityFrequency(record.frequency);
        if (!normalizedFrequency.empty()) {
            authority.controllerFrequencies.push_back(normalizedFrequency);
        }
        SortUnique(&authority.lookupKeys);
        SortUnique(&authority.controllerPrefixes);
        SortUnique(&authority.controllerCallsignPatterns);
        SortUnique(&authority.controllerFrequencies);

        auto existingAuthority = std::find_if(
            catalog.authorities.begin(),
            catalog.authorities.end(),
            [&](const ControllerAuthority& candidate) {
                return candidate.id == authority.id;
            });
        if (existingAuthority != catalog.authorities.end()) {
            for (const auto& lookupKey : authority.lookupKeys) {
                AddLookupKey(&existingAuthority->lookupKeys, lookupKey);
            }
            if (existingAuthority->polygonKey.empty() &&
                !authority.polygonKey.empty()) {
                existingAuthority->polygonKey = authority.polygonKey;
            }
            existingAuthority->controllerPrefixes.insert(
                existingAuthority->controllerPrefixes.end(),
                authority.controllerPrefixes.begin(),
                authority.controllerPrefixes.end());
            existingAuthority->controllerCallsignPatterns.insert(
                existingAuthority->controllerCallsignPatterns.end(),
                authority.controllerCallsignPatterns.begin(),
                authority.controllerCallsignPatterns.end());
            existingAuthority->controllerFrequencies.insert(
                existingAuthority->controllerFrequencies.end(),
                authority.controllerFrequencies.begin(),
                authority.controllerFrequencies.end());
            if (!authority.proofDetail.empty()) {
                if (!existingAuthority->proofDetail.empty()) {
                    existingAuthority->proofDetail.push_back('|');
                }
                existingAuthority->proofDetail += authority.proofDetail;
            }
            SortUnique(&existingAuthority->lookupKeys);
            SortUnique(&existingAuthority->controllerPrefixes);
            SortUnique(&existingAuthority->controllerCallsignPatterns);
            SortUnique(&existingAuthority->controllerFrequencies);
            continue;
        }

        if (authority.polygonKey.empty()) {
            catalog.dataGaps.push_back({
                authority.id,
                authority.polygonKey,
                "missing-polygon-key",
                authority.sourceRecord,
            });
        }
        if (authority.controllerCallsignPatterns.empty()) {
            catalog.dataGaps.push_back({
                authority.id,
                authority.polygonKey,
                "missing-callsign-pattern",
                authority.sourceRecord,
            });
        }

        catalog.authorities.push_back(std::move(authority));
    }

    std::sort(
        catalog.authorities.begin(),
        catalog.authorities.end(),
        [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    std::sort(
        catalog.dataGaps.begin(),
        catalog.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            if (left.polygonKey != right.polygonKey) {
                return left.polygonKey < right.polygonKey;
            }
            return left.reason < right.reason;
        });
    return catalog;
}

std::vector<AuthorityPositionSourceRecord> ParseAuthorityPositionSourceRecordsJson(
    AuthoritySource source,
    const std::string& payload) {
    std::vector<AuthorityPositionSourceRecord> records;
    if (payload.empty()) {
        return records;
    }

    if (auto sourcePackages = ExtractJsonArrayPayload(payload, "source_packages");
        sourcePackages.has_value()) {
        for (const auto& packagePayload :
             ExtractJsonObjectsFromArrayPayload(*sourcePackages)) {
            auto packageRecords =
                ParseAuthorityPositionSourceRecordsJson(source, packagePayload);
            records.insert(
                records.end(),
                std::make_move_iterator(packageRecords.begin()),
                std::make_move_iterator(packageRecords.end()));
        }
        return records;
    }

    if (!PayloadAllowsAuthoritySource(source, payload)) {
        return records;
    }

    auto positionObjectRecords = ParseVatGlassesPositionObjectRecords(source, payload);
    records.insert(
        records.end(),
        std::make_move_iterator(positionObjectRecords.begin()),
        std::make_move_iterator(positionObjectRecords.end()));

    auto arrayPayload = ExtractJsonArrayPayload(payload, "positions");
    if (!arrayPayload.has_value()) {
        arrayPayload = ExtractJsonArrayPayload(payload, "ownership");
    }
    if (!arrayPayload.has_value()) {
        return records;
    }

    for (const auto& objectPayload : ExtractJsonObjectsFromArrayPayload(*arrayPayload)) {
        AuthorityPositionSourceRecord record;
        record.source = source;
        record.sourceRecord = objectPayload;
        record.id = FirstJsonStringField(
            objectPayload,
            {"id", "position_id", "positionId", "position"});
        record.name = FirstJsonStringField(objectPayload, {"name", "title"});
        record.polygonKey = FirstJsonStringField(
            objectPayload,
            {"polygon_key", "polygonKey", "polygon", "sector", "airspace_id", "airspaceId"});
        record.frequency = FirstJsonStringField(objectPayload, {"frequency"});
        record.kind = ParseAuthorityKindOrDefault(
            FirstJsonStringField(objectPayload, {"kind", "type"}));
        record.proofSource = DefaultProofSourceForAuthoritySource(source);
        record.proofDetail =
            "position=" + NormalizeAuthorityToken(record.id) +
            ";polygon=" + NormalizeAuthorityToken(record.polygonKey);
        record.controllerCallsignPatterns = FirstJsonStringArrayField(
            objectPayload,
            {"callsign_patterns", "callsignPatterns", "patterns", "callsigns"});

        const auto singleCallsign = FirstJsonStringField(objectPayload, {"callsign"});
        if (!singleCallsign.empty()) {
            record.controllerCallsignPatterns.push_back(singleCallsign);
        }
        SortUnique(&record.controllerCallsignPatterns);
        records.push_back(std::move(record));
    }

    return records;
}

std::vector<AuthorityPolygonSourceRecord> ParseAuthorityPolygonSourceRecordsJson(
    AuthoritySource source,
    const std::string& payload) {
    std::vector<AuthorityPolygonSourceRecord> records;
    if (payload.empty()) {
        return records;
    }

    if (auto sourcePackages = ExtractJsonArrayPayload(payload, "source_packages");
        sourcePackages.has_value()) {
        for (const auto& packagePayload :
             ExtractJsonObjectsFromArrayPayload(*sourcePackages)) {
            auto packageRecords =
                ParseAuthorityPolygonSourceRecordsJson(source, packagePayload);
            records.insert(
                records.end(),
                std::make_move_iterator(packageRecords.begin()),
                std::make_move_iterator(packageRecords.end()));
        }
        return records;
    }

    if (!PayloadAllowsAuthoritySource(source, payload)) {
        return records;
    }

    auto staticRecords = ParseVatGlassesStaticAirspacePolygons(source, payload);
    records.insert(
        records.end(),
        std::make_move_iterator(staticRecords.begin()),
        std::make_move_iterator(staticRecords.end()));
    auto dynamicRecords = ParseVatGlassesDynamicAirspacePolygons(source, payload);
    records.insert(
        records.end(),
        std::make_move_iterator(dynamicRecords.begin()),
        std::make_move_iterator(dynamicRecords.end()));
    return records;
}

ControllerAuthorityCatalog MergeControllerAuthorityCatalogs(
    const ControllerAuthorityCatalog& left,
    const ControllerAuthorityCatalog& right) {
    ControllerAuthorityCatalog merged;
    merged.authorities = left.authorities;
    merged.authorities.insert(
        merged.authorities.end(),
        right.authorities.begin(),
        right.authorities.end());
    merged.dataGaps = left.dataGaps;
    merged.dataGaps.insert(
        merged.dataGaps.end(),
        right.dataGaps.begin(),
        right.dataGaps.end());

    std::sort(
        merged.authorities.begin(),
        merged.authorities.end(),
        [](const auto& leftAuthority, const auto& rightAuthority) {
            return leftAuthority.id < rightAuthority.id;
        });
    std::sort(
        merged.dataGaps.begin(),
        merged.dataGaps.end(),
        [](const auto& leftGap, const auto& rightGap) {
            if (leftGap.authorityId != rightGap.authorityId) {
                return leftGap.authorityId < rightGap.authorityId;
            }
            if (leftGap.polygonKey != rightGap.polygonKey) {
                return leftGap.polygonKey < rightGap.polygonKey;
            }
            return leftGap.reason < rightGap.reason;
        });
    return merged;
}

AuthorityPolygonCatalog CompileAuthorityPolygons(
    const std::vector<AuthorityPolygonSourceRecord>& sourceRecords) {
    AuthorityPolygonCatalog catalog;

    for (const auto& record : sourceRecords) {
        const auto polygonId = PolygonIdFromRecord(record);
        const auto polygonKey = NormalizeAuthorityToken(record.id);
        if (polygonId.empty() || polygonKey.empty()) {
            catalog.dataGaps.push_back({
                polygonId.empty() ? SourcePrefix(record.source) + ":<missing-id>" : polygonId,
                polygonKey,
                "missing-polygon-key",
                record.sourceRecord,
            });
            continue;
        }

        AuthorityPolygon polygon;
        polygon.id = polygonId;
        polygon.source = record.source;
        polygon.kind = SourceDefaultKind(record.source);
        polygon.name = Trim(record.name);
        polygon.polygonKey = polygonKey;
        polygon.sourceRecord = record.sourceRecord;

        if (record.source == AuthoritySource::SimAwareTracon) {
            AddSimAwareTraconLookupKeys(record, &polygon.lookupKeys);
        } else {
            AddLookupKey(&polygon.lookupKeys, record.id);
            for (const auto& token : record.lookupTokens) {
                AddLookupKey(&polygon.lookupKeys, token);
            }
        }
        SortUnique(&polygon.lookupKeys);

        polygon.rings = ValidRings(record.rings);
        if (polygon.rings.empty()) {
            catalog.dataGaps.push_back({
                polygon.id,
                polygon.polygonKey,
                "missing-valid-polygon-ring",
                record.sourceRecord,
            });
            continue;
        }

        catalog.polygons.push_back(std::move(polygon));
    }

    std::sort(
        catalog.polygons.begin(),
        catalog.polygons.end(),
        [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    std::sort(
        catalog.dataGaps.begin(),
        catalog.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            return left.reason < right.reason;
        });
    return catalog;
}

std::vector<ActiveControllerAuthority> ResolveControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    int vatsimFacility) {
    return ResolveControllerAuthority(catalog, callsign, std::string{}, vatsimFacility);
}

std::vector<AuthorityDecision> EvaluateControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    const std::string& frequency,
    int vatsimFacility) {
    std::vector<AuthorityDecision> decisions;
    std::unordered_set<std::string> insertedDecisionKeys;
    std::unordered_set<std::string> frequencySpecificAuthorityIds;
    const auto normalizedFrequency = NormalizeAuthorityFrequency(frequency);
    const auto normalizedCallsign = NormalizeControllerCallsign(callsign);
    bool sawFrequencySpecificPatternCandidate = false;

    for (const auto& authority : catalog.authorities) {
        const auto requiresFrequency = !authority.controllerFrequencies.empty();
        auto appendDecision = [&](AuthorityDecision decision) {
            const auto key = normalizedCallsign + "|" +
                             authority.id + "|" +
                             decision.evidence.matchedPattern + "|" +
                             (decision.accepted ? "accepted" : "rejected");
            if (!insertedDecisionKeys.insert(key).second) {
                return;
            }

            if (decision.accepted && requiresFrequency) {
                frequencySpecificAuthorityIds.insert(authority.id);
            }
            decisions.push_back(std::move(decision));
        };

        for (const auto& pattern : authority.controllerCallsignPatterns) {
            const auto callsignMatched = CallsignMatchesPattern(pattern, callsign);
            const auto facilityMatched = PatternFacilityMatches(pattern, vatsimFacility);
            const auto frequencyMatched =
                !requiresFrequency ||
                (!normalizedFrequency.empty() &&
                 std::find(
                     authority.controllerFrequencies.begin(),
                     authority.controllerFrequencies.end(),
                     normalizedFrequency) != authority.controllerFrequencies.end());

            if (requiresFrequency && callsignMatched && facilityMatched) {
                sawFrequencySpecificPatternCandidate = true;
            }

            auto evidence = BuildAuthorityEvidence(
                authority,
                normalizedCallsign,
                normalizedFrequency,
                pattern,
                vatsimFacility,
                callsignMatched,
                facilityMatched,
                requiresFrequency,
                frequencyMatched);
            auto decision = BuildAuthorityDecision(std::move(evidence));
            if (!decision.accepted &&
                !ShouldKeepRejectedDecision(decision.evidence)) {
                continue;
            }
            appendDecision(std::move(decision));
        }

        if (authority.kind != AuthorityKind::Center ||
            authority.controllerPrefixes.empty()) {
            continue;
        }
        for (const auto& rawPrefix : authority.controllerPrefixes) {
            const auto prefix = NormalizeControllerCallsign(rawPrefix);
            if (prefix.empty()) {
                continue;
            }

            const auto callsignMatched = prefix == normalizedCallsign;
            const auto facilityMatched =
                BareCenterAuthorityFacilityMatches(vatsimFacility);
            const auto frequencyMatched =
                !requiresFrequency ||
                (!normalizedFrequency.empty() &&
                 std::find(
                     authority.controllerFrequencies.begin(),
                     authority.controllerFrequencies.end(),
                     normalizedFrequency) != authority.controllerFrequencies.end());

            if (requiresFrequency && callsignMatched && facilityMatched) {
                sawFrequencySpecificPatternCandidate = true;
            }

            auto evidence = BuildAuthorityEvidence(
                authority,
                normalizedCallsign,
                normalizedFrequency,
                prefix,
                vatsimFacility,
                callsignMatched,
                facilityMatched,
                requiresFrequency,
                frequencyMatched);
            auto decision = BuildAuthorityDecision(std::move(evidence));
            if (!decision.accepted &&
                !ShouldKeepRejectedDecision(decision.evidence)) {
                continue;
            }
            appendDecision(std::move(decision));
        }
    }

    if (sawFrequencySpecificPatternCandidate &&
        frequencySpecificAuthorityIds.empty()) {
        decisions.erase(
            std::remove_if(
                decisions.begin(),
                decisions.end(),
                [](const auto& decision) { return decision.accepted; }),
            decisions.end());
    }

    if (!frequencySpecificAuthorityIds.empty()) {
        decisions.erase(
            std::remove_if(
                decisions.begin(),
                decisions.end(),
                [&](const auto& decision) {
                    return decision.accepted &&
                           frequencySpecificAuthorityIds.find(
                               decision.evidence.authorityId) ==
                               frequencySpecificAuthorityIds.end();
                }),
            decisions.end());
    }

    std::sort(decisions.begin(), decisions.end(), AuthorityDecisionLess);
    return decisions;
}

std::vector<ActiveControllerAuthority> ResolveControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    const std::string& frequency,
    int vatsimFacility) {
    std::vector<ActiveControllerAuthority> matches;
    const auto decisions =
        EvaluateControllerAuthority(catalog, callsign, frequency, vatsimFacility);
    matches.reserve(decisions.size());
    for (const auto& decision : decisions) {
        if (decision.accepted) {
            matches.push_back(decision.activeAuthority);
        }
    }

    std::sort(
        matches.begin(),
        matches.end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            return left.matchedPattern < right.matchedPattern;
        });
    return matches;
}

AuthorityActivationResult ActivateAuthorityPolygons(
    const ControllerAuthorityCatalog& controllerCatalog,
    const AuthorityPolygonCatalog& polygonCatalog,
    const std::string& callsign,
    int vatsimFacility) {
    return ActivateAuthorityPolygons(
        controllerCatalog,
        polygonCatalog,
        callsign,
        std::string{},
        vatsimFacility);
}

AuthorityActivationResult ActivateAuthorityPolygons(
    const ControllerAuthorityCatalog& controllerCatalog,
    const AuthorityPolygonCatalog& polygonCatalog,
    const std::string& callsign,
    const std::string& frequency,
    int vatsimFacility) {
    AuthorityActivationResult result;
    std::unordered_set<std::string> insertedActiveKeys;
    std::unordered_set<std::string> insertedGapKeys;

    result.decisions =
        EvaluateControllerAuthority(controllerCatalog, callsign, frequency, vatsimFacility);
    for (const auto& decision : result.decisions) {
        if (!decision.accepted) {
            continue;
        }
        const auto& authorityMatch = decision.activeAuthority;
        bool matchedPolygon = false;
        for (const auto& polygon : polygonCatalog.polygons) {
            if (!ActiveAuthorityCanUsePolygon(authorityMatch, polygon)) {
                continue;
            }
            if (!PolygonMatchesAuthorityKey(polygon, authorityMatch.polygonKey)) {
                continue;
            }

            matchedPolygon = true;
            const auto activeKey = authorityMatch.callsign + "|" +
                                   authorityMatch.authorityId + "|" +
                                   polygon.id + "|" +
                                   authorityMatch.matchedPattern;
            if (!insertedActiveKeys.insert(activeKey).second) {
                continue;
            }

            result.activePolygons.push_back({
                authorityMatch.callsign,
                authorityMatch.authorityId,
                polygon.id,
                polygon.polygonKey,
                authorityMatch.matchedPattern,
                polygon.source,
                authorityMatch.kind,
                authorityMatch.proofSource,
                authorityMatch.proofDetail,
            });
        }

        if (!matchedPolygon) {
            const auto gapKey = authorityMatch.authorityId + "|" +
                                authorityMatch.polygonKey + "|missing-authority-polygon";
            if (insertedGapKeys.insert(gapKey).second) {
                result.dataGaps.push_back({
                    authorityMatch.authorityId,
                    authorityMatch.polygonKey,
                    "missing-authority-polygon",
                    authorityMatch.callsign,
                });
            }
        }
    }

    std::sort(
        result.activePolygons.begin(),
        result.activePolygons.end(),
        [](const auto& left, const auto& right) {
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            if (left.polygonId != right.polygonId) {
                return left.polygonId < right.polygonId;
            }
            return left.matchedPattern < right.matchedPattern;
        });
    std::sort(
        result.dataGaps.begin(),
        result.dataGaps.end(),
        [](const auto& left, const auto& right) {
            if (left.authorityId != right.authorityId) {
                return left.authorityId < right.authorityId;
            }
            if (left.polygonKey != right.polygonKey) {
                return left.polygonKey < right.polygonKey;
            }
            return left.reason < right.reason;
        });
    return result;
}

std::vector<RelevantAuthorityPolygon> ResolveRelevantAuthorityPolygons(
    const std::vector<ActiveAuthorityPolygon>& activePolygons,
    const AuthorityPolygonCatalog& polygonCatalog,
    bool hasAircraftPosition,
    const GeoPoint& aircraftPosition,
    const std::vector<GeoPoint>& routePoints) {
    std::vector<RelevantAuthorityPolygon> relevantPolygons;

    for (const auto& activePolygon : activePolygons) {
        const auto* polygon = FindPolygonById(polygonCatalog, activePolygon.polygonId);
        if (polygon == nullptr) {
            continue;
        }

        RelevantAuthorityPolygon relevant;
        relevant.activePolygon = activePolygon;
        relevant.aircraftInside =
            hasAircraftPosition && PointInPolygon(aircraftPosition, *polygon);

        const auto routeEntryDistanceNm =
            FindRouteEntryDistanceNm(routePoints, *polygon);
        if (routeEntryDistanceNm.has_value()) {
            relevant.routeIntersects = true;
            relevant.routeEntryDistanceNm = *routeEntryDistanceNm;
        }

        if (relevant.aircraftInside || relevant.routeIntersects) {
            relevantPolygons.push_back(std::move(relevant));
        }
    }

    std::sort(
        relevantPolygons.begin(),
        relevantPolygons.end(),
        [](const auto& left, const auto& right) {
            if (left.routeIntersects != right.routeIntersects) {
                return left.routeIntersects && !right.routeIntersects;
            }
            if (left.routeIntersects && right.routeIntersects &&
                left.routeEntryDistanceNm != right.routeEntryDistanceNm) {
                return left.routeEntryDistanceNm < right.routeEntryDistanceNm;
            }
            if (left.aircraftInside != right.aircraftInside) {
                return left.aircraftInside && !right.aircraftInside;
            }
            if (left.activePolygon.callsign != right.activePolygon.callsign) {
                return left.activePolygon.callsign < right.activePolygon.callsign;
            }
            return left.activePolygon.polygonId < right.activePolygon.polygonId;
        });
    return relevantPolygons;
}

}  // namespace xvatsim::core::authority
