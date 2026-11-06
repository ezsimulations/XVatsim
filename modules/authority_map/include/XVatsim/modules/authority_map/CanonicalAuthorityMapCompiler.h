#pragma once

#include <memory>
#include <vector>

#include "XVatsim/core/ControllerAuthority.h"

namespace xvatsim::modules::authority_map {

// The sole compiler for the immutable worldwide static Center + APP/DEP
// catalog. Compiling geometry does not activate a live controller or light a
// polygon.
// Source adapters parse provider payloads into typed catalogs; this component
// owns reconciliation, source receipts, identity, and referential validation.
std::shared_ptr<core::authority::CanonicalAuthorityMapDataset>
CompileCanonicalAuthorityMapDataset(
    const std::vector<unsigned char>& centerBoundaryPayload,
    const std::vector<unsigned char>& terminalBoundaryPayload,
    const std::vector<unsigned char>& positionCatalogPayload,
    const std::vector<unsigned char>& ownershipPayload,
    core::authority::ControllerAuthorityCatalog controllerCatalog,
    core::authority::AuthorityPolygonCatalog polygonCatalog);

}  // namespace xvatsim::modules::authority_map
