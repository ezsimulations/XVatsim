#pragma once

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h"

namespace xvatsim::modules::controller_feed {

class ControllerFeedClient {
public:
    ControllerFeedClient() = default;

    brain::ControllerFeedSnapshot BuildSnapshot(
        const xvatsim::modules::vatsim_data_feed::VatsimDataFeedSnapshot& feedSnapshot) const;
};

}  // namespace xvatsim::modules::controller_feed
