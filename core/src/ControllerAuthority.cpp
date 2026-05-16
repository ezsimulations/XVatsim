#include "XVatsim/core/ControllerAuthority.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_set>

namespace xvatsim::core::authority {

namespace {

constexpr int kVatsimFlightServiceFacility = 1;
constexpr int kVatsimCenterFacility = 6;

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

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool PatternFacilityMatches(const std::string& pattern, int vatsimFacility) {
    if (EndsWith(pattern, "_CTR")) {
        return vatsimFacility == kVatsimCenterFacility;
    }
    if (EndsWith(pattern, "_FSS")) {
        return vatsimFacility == kVatsimFlightServiceFacility;
    }
    return false;
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
    }
    return "UNKNOWN";
}

AuthorityKind SourceDefaultKind(AuthoritySource source) {
    switch (source) {
        case AuthoritySource::SimAwareTracon:
            return AuthorityKind::Terminal;
        case AuthoritySource::VatsimRadarExtension:
            return AuthorityKind::Extension;
        case AuthoritySource::VatSpyFir:
        case AuthoritySource::VatSpyUir:
        case AuthoritySource::VatSpyBoundary:
        case AuthoritySource::VatGlasses:
            return AuthorityKind::Center;
    }
    return AuthorityKind::Center;
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
    std::vector<ActiveControllerAuthority> matches;
    std::unordered_set<std::string> insertedKeys;

    for (const auto& authority : catalog.authorities) {
        for (const auto& pattern : authority.controllerCallsignPatterns) {
            if (!PatternFacilityMatches(pattern, vatsimFacility) ||
                !CallsignMatchesPattern(pattern, callsign)) {
                continue;
            }

            const auto key = NormalizeControllerCallsign(callsign) + "|" +
                             authority.id + "|" + pattern;
            if (!insertedKeys.insert(key).second) {
                continue;
            }

            matches.push_back({
                NormalizeControllerCallsign(callsign),
                authority.id,
                authority.polygonKey,
                pattern,
            });
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
    AuthorityActivationResult result;
    std::unordered_set<std::string> insertedActiveKeys;
    std::unordered_set<std::string> insertedGapKeys;

    const auto authorityMatches =
        ResolveControllerAuthority(controllerCatalog, callsign, vatsimFacility);
    for (const auto& authorityMatch : authorityMatches) {
        bool matchedPolygon = false;
        for (const auto& polygon : polygonCatalog.polygons) {
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
                polygon.kind,
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

}  // namespace xvatsim::core::authority
