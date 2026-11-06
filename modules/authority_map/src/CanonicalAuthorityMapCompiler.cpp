#include "XVatsim/modules/authority_map/CanonicalAuthorityMapCompiler.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace xvatsim::modules::authority_map {
namespace {

void HashBytes(
    std::uint64_t* hash,
    const std::vector<unsigned char>& bytes) {
    if (hash == nullptr) {
        return;
    }
    for (const auto value : bytes) {
        *hash ^= static_cast<std::uint64_t>(value);
        *hash *= 1099511628211ULL;
    }
    *hash ^= static_cast<std::uint64_t>(bytes.size());
    *hash *= 1099511628211ULL;
}

std::uint64_t HashPayloads(
    const std::vector<const std::vector<unsigned char>*>& payloads) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto* payload : payloads) {
        if (payload != nullptr) {
            HashBytes(&hash, *payload);
        }
    }
    return hash == 0 ? 1 : hash;
}

std::string HexIdentity(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0')
           << std::setw(16) << value;
    return stream.str();
}

}  // namespace

std::shared_ptr<core::authority::CanonicalAuthorityMapDataset>
CompileCanonicalAuthorityMapDataset(
    const std::vector<unsigned char>& centerBoundaryPayload,
    const std::vector<unsigned char>& terminalBoundaryPayload,
    const std::vector<unsigned char>& positionCatalogPayload,
    const std::vector<unsigned char>& ownershipPayload,
    core::authority::ControllerAuthorityCatalog controllerCatalog,
    core::authority::AuthorityPolygonCatalog polygonCatalog) {
    if (centerBoundaryPayload.empty() || positionCatalogPayload.empty()) {
        return {};
    }

    auto dataset =
        std::make_shared<core::authority::CanonicalAuthorityMapDataset>();
    std::uint64_t identity = 1469598103934665603ULL;
    HashBytes(&identity, centerBoundaryPayload);
    HashBytes(&identity, terminalBoundaryPayload);
    HashBytes(&identity, positionCatalogPayload);
    HashBytes(&identity, ownershipPayload);
    dataset->identity = identity == 0 ? 1 : identity;
    dataset->controllerAuthorityCatalog = std::move(controllerCatalog);
    dataset->authorityPolygonCatalog = std::move(polygonCatalog);

    dataset->receipt.controllerPositions =
        dataset->controllerAuthorityCatalog.authorities.size();
    dataset->receipt.polygonDefinitions =
        dataset->authorityPolygonCatalog.polygons.size();
    dataset->receipt.dataGaps =
        dataset->controllerAuthorityCatalog.dataGaps.size() +
        dataset->authorityPolygonCatalog.dataGaps.size();

    std::unordered_set<std::string> polygonIds;
    std::unordered_set<std::string> polygonMatchKeys;
    for (const auto& polygon : dataset->authorityPolygonCatalog.polygons) {
        polygonIds.insert(polygon.id);
        dataset->receipt.polygonParts += polygon.parts.size();
        dataset->receipt.retainedBytes += sizeof(polygon) +
            polygon.id.capacity() + polygon.sourceFeatureId.capacity() +
            polygon.name.capacity() + polygon.polygonKey.capacity() +
            polygon.sourceRecord.capacity();
        for (const auto& part : polygon.parts) {
            dataset->receipt.polygonHoles += part.holes.size();
            dataset->receipt.retainedBytes += sizeof(part) +
                part.componentId.capacity() +
                part.exterior.points.capacity() *
                    sizeof(core::authority::GeoPoint);
            for (const auto& hole : part.holes) {
                dataset->receipt.retainedBytes +=
                    hole.points.capacity() * sizeof(core::authority::GeoPoint);
            }
        }
        const auto addMatchKey = [&](const std::string& value) {
            const auto normalized =
                core::authority::NormalizeAuthorityToken(value);
            if (!normalized.empty()) {
                polygonMatchKeys.insert(normalized);
            }
        };
        addMatchKey(polygon.id);
        addMatchKey(polygon.polygonKey);
        for (const auto& lookupKey : polygon.lookupKeys) {
            addMatchKey(lookupKey);
        }
    }
    dataset->receipt.uniquePolygonIds =
        polygonIds.size() == dataset->authorityPolygonCatalog.polygons.size();

    std::unordered_set<std::string> positionIds;
    for (const auto& authority :
         dataset->controllerAuthorityCatalog.authorities) {
        positionIds.insert(authority.id);
        dataset->receipt.retainedBytes += sizeof(authority) +
            authority.id.capacity() + authority.name.capacity() +
            authority.polygonKey.capacity() +
            authority.sourceRecord.capacity() +
            authority.proofSource.capacity() +
            authority.proofDetail.capacity();
        const auto polygonKey =
            core::authority::NormalizeAuthorityToken(authority.polygonKey);
        if (!polygonKey.empty() &&
            polygonMatchKeys.find(polygonKey) == polygonMatchKeys.end()) {
            ++dataset->receipt.unresolvedPositionPolygonReferences;
        }
    }
    dataset->receipt.uniquePositionIds =
        positionIds.size() ==
        dataset->controllerAuthorityCatalog.authorities.size();

    const std::array sources{
        core::authority::AuthoritySource::VatSpyFir,
        core::authority::AuthoritySource::VatSpyUir,
        core::authority::AuthoritySource::VatSpyBoundary,
        core::authority::AuthoritySource::SimAwareTracon,
        core::authority::AuthoritySource::VatGlasses,
        core::authority::AuthoritySource::SpecialSectorData,
        core::authority::AuthoritySource::AirportLocal,
    };
    for (const auto source : sources) {
        std::vector<const std::vector<unsigned char>*> sourcePayloads;
        std::string provider;
        std::string configuredIdentity;
        bool required = false;
        switch (source) {
            case core::authority::AuthoritySource::VatSpyFir:
            case core::authority::AuthoritySource::VatSpyUir:
                provider = "VATSpy";
                configuredIdentity = "position-catalog";
                sourcePayloads.push_back(&positionCatalogPayload);
                required = true;
                break;
            case core::authority::AuthoritySource::VatSpyBoundary:
                provider = "VATSIM Map Data";
                configuredIdentity = "center-boundary";
                sourcePayloads.push_back(&centerBoundaryPayload);
                required = true;
                break;
            case core::authority::AuthoritySource::SimAwareTracon:
                provider = "SimAware";
                configuredIdentity = "terminal-boundary+ownership";
                sourcePayloads.push_back(&terminalBoundaryPayload);
                sourcePayloads.push_back(&ownershipPayload);
                required = true;
                break;
            case core::authority::AuthoritySource::VatGlasses:
                provider = "VATGlasses";
                configuredIdentity = "dynamic-ownership";
                sourcePayloads.push_back(&ownershipPayload);
                break;
            case core::authority::AuthoritySource::SpecialSectorData:
                provider = "VATSIM Map Data";
                configuredIdentity = "special-sector-data";
                sourcePayloads.push_back(&ownershipPayload);
                break;
            case core::authority::AuthoritySource::AirportLocal:
                provider = "XVatsim";
                configuredIdentity = "retired-synthetic-airport-local";
                break;
        }

        core::authority::AuthoritySourceHealthReceipt receipt;
        receipt.source = core::authority::AuthoritySourceLabel(source);
        receipt.provider = std::move(provider);
        receipt.configuredIdentity = std::move(configuredIdentity);
        receipt.required = required;
        receipt.contentHash = HashPayloads(sourcePayloads);
        receipt.revision = HexIdentity(receipt.contentHash);
        for (const auto* payload : sourcePayloads) {
            if (payload != nullptr) {
                receipt.payloadBytes += payload->size();
            }
        }
        for (const auto& authority :
             dataset->controllerAuthorityCatalog.authorities) {
            if (authority.source == source) {
                ++receipt.acceptedPositions;
            }
        }
        for (const auto& polygon : dataset->authorityPolygonCatalog.polygons) {
            if (polygon.source == source) {
                ++receipt.acceptedPolygons;
                receipt.acceptedPolygonParts += polygon.parts.size();
            }
        }
        for (const auto& gap : dataset->controllerAuthorityCatalog.dataGaps) {
            if (gap.source == source) {
                ++receipt.rejectedRecords;
            }
        }
        for (const auto& gap : dataset->authorityPolygonCatalog.dataGaps) {
            if (gap.source == source) {
                ++receipt.rejectedRecords;
            }
        }
        receipt.acceptedRecords =
            receipt.acceptedPositions + receipt.acceptedPolygons;
        const auto anyPayload = std::any_of(
            sourcePayloads.begin(),
            sourcePayloads.end(),
            [](const auto* payload) {
                return payload != nullptr && !payload->empty();
            });
        receipt.state = !anyPayload
            ? core::authority::AuthoritySourceHealthState::Unavailable
            : receipt.rejectedRecords == 0
                ? core::authority::AuthoritySourceHealthState::Complete
                : core::authority::AuthoritySourceHealthState::Partial;
        receipt.reason = !anyPayload
            ? (required ? "required-source-missing" :
                          "optional-source-missing")
            : receipt.rejectedRecords == 0
                ? "source-records-reconciled"
                : "typed-data-gaps";
        dataset->receipt.sources.push_back(std::move(receipt));
    }

    return dataset;
}

}  // namespace xvatsim::modules::authority_map
