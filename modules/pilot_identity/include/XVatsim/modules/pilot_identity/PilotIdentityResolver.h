#pragma once

#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::pilot_identity {

class PilotIdentityResolver {
public:
    PilotIdentityResolver() = default;

    brain::PilotIdentitySnapshot Resolve(
        const brain::XPilotSessionSnapshot& xPilotSessionSnapshot) const;
};

}  // namespace xvatsim::modules::pilot_identity
