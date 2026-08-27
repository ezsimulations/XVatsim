#pragma once

#include <cstddef>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/modules/overlay/OverlayAccessoryCore.h"

namespace xvatsim::modules::overlay {

struct OfflineRasterImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgraPixels;
};

OfflineRasterImage RenderProductionMainCardForOfflineProof(
    const brain::OverlayViewModel& viewModel,
    int scrollOffset);

std::size_t BuildProductionMainCardSignatureForOfflineProof(
    const brain::OverlayViewModel& viewModel,
    int scrollOffset);

OfflineRasterImage RenderProductionAccessoryRailForOfflineProof(
    const AccessoryLayoutResult& layout,
    const brain::BrainOwnedAccessoryPresentationSnapshot& snapshot);

OfflineRasterImage RenderProductionAccessoryDrawerForOfflineProof(
    const AccessoryLayoutResult& layout,
    const AccessoryPresentationState& state);

}  // namespace xvatsim::modules::overlay
