#include "XVatsim/modules/controller_feed/ControllerFeedClient.h"

#include <string>

namespace xvatsim::modules::controller_feed {

brain::ControllerFeedSnapshot ControllerFeedClient::BuildSnapshot(
    const xvatsim::modules::vatsim_data_feed::VatsimDataFeedSnapshot& feedSnapshot) const {
    brain::ControllerFeedSnapshot snapshot;
    if (!feedSnapshot.hasCache) {
        snapshot.available = false;
        snapshot.stale = true;
        snapshot.statusLine = feedSnapshot.fetchInProgress
                                  ? "ATC feed pending"
                                  : "ATC feed unavailable";
        return snapshot;
    }

    snapshot.stale = feedSnapshot.stale;
    snapshot.generation = feedSnapshot.generation;
    if (feedSnapshot.stale) {
        snapshot.available = false;
        snapshot.connectedControllers = 0;
        snapshot.statusLine = "ATC feed stale";
        return snapshot;
    }

    snapshot.available = true;
    snapshot.connectedControllers = feedSnapshot.connectedControllers;
    snapshot.controllers = &feedSnapshot.controllers;
    snapshot.statusLine = "ATC " + std::to_string(snapshot.connectedControllers) + " online";
    return snapshot;
}

}  // namespace xvatsim::modules::controller_feed

