#include "XVatsim/modules/pilot_identity/PilotIdentityResolver.h"

#include <cctype>
#include <string>

namespace xvatsim::modules::pilot_identity {

namespace {

std::string NormalizeCallsign(const std::string& callsign) {
    std::string normalized;
    normalized.reserve(callsign.size());

    for (const auto character : callsign) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))));
    }

    return normalized;
}

}  // namespace

brain::PilotIdentitySnapshot PilotIdentityResolver::Resolve(
    const brain::XPilotSessionSnapshot& xPilotSessionSnapshot) const {
    brain::PilotIdentitySnapshot snapshot;
    snapshot.connected = xPilotSessionSnapshot.connected;
    snapshot.callsign = xPilotSessionSnapshot.callsign;
    snapshot.normalizedCallsign = NormalizeCallsign(xPilotSessionSnapshot.callsign);
    snapshot.ready = snapshot.connected && !snapshot.normalizedCallsign.empty();

    if (!snapshot.connected) {
        snapshot.statusLine = "ID waiting for xPilot connection";
        return snapshot;
    }

    if (!snapshot.ready) {
        snapshot.statusLine = "ID connected, callsign unavailable";
        return snapshot;
    }

    snapshot.statusLine = "ID " + snapshot.normalizedCallsign;
    return snapshot;
}

}  // namespace xvatsim::modules::pilot_identity
