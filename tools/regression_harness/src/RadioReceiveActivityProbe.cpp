#include "RadioReceiveActivityProbe.h"

#include "XVatsim/modules/radio_state/RadioStateSampler.h"

#include <iostream>

namespace xvatsim::tools::radio_receive_activity {

int RunRadioReceiveActivityProbe() {
    using xvatsim::modules::radio_state::EvaluateRadioReceiveActive;
    using xvatsim::modules::radio_state::RadioReceiveActivityEvidence;

    RadioReceiveActivityEvidence evidence;
    evidence.powered = true;
    evidence.tunedFrequencyHasActivity = true;
    const auto frequencyActivityDoesNotReceive =
        !EvaluateRadioReceiveActive(evidence);

    evidence = {};
    evidence.powered = true;
    evidence.xpilotReceiveActive = true;
    const auto xpilotReceiveDisplays = EvaluateRadioReceiveActive(evidence);

    evidence = {};
    evidence.powered = true;
    evidence.simulatorReceiveActive = true;
    const auto simulatorReceiveDisplays = EvaluateRadioReceiveActive(evidence);

    evidence = {};
    evidence.powered = true;
    evidence.receiveOverrideActive = true;
    const auto receiveOverrideDisplays = EvaluateRadioReceiveActive(evidence);

    evidence = {};
    evidence.powered = true;
    evidence.latchedReceiveActive = true;
    const auto exactReceiveLatchDisplays = EvaluateRadioReceiveActive(evidence);

    evidence = {};
    evidence.xpilotReceiveActive = true;
    const auto unpoweredSignalDoesNotDisplay =
        !EvaluateRadioReceiveActive(evidence);

    std::cout
        << "RADIO_RECEIVE_ACTIVITY"
        << " frequencyActivityDoesNotReceive="
        << (frequencyActivityDoesNotReceive ? 1 : 0)
        << " xpilotReceiveDisplays=" << (xpilotReceiveDisplays ? 1 : 0)
        << " simulatorReceiveDisplays="
        << (simulatorReceiveDisplays ? 1 : 0)
        << " receiveOverrideDisplays="
        << (receiveOverrideDisplays ? 1 : 0)
        << " exactReceiveLatchDisplays="
        << (exactReceiveLatchDisplays ? 1 : 0)
        << " unpoweredSignalDoesNotDisplay="
        << (unpoweredSignalDoesNotDisplay ? 1 : 0)
        << "\n";

    if (!frequencyActivityDoesNotReceive ||
        !xpilotReceiveDisplays ||
        !simulatorReceiveDisplays ||
        !receiveOverrideDisplays ||
        !exactReceiveLatchDisplays ||
        !unpoweredSignalDoesNotDisplay) {
        std::cerr << "RADIO_RECEIVE_ACTIVITY_FAILED\n";
        return 1;
    }

    std::cout << "RADIO_RECEIVE_ACTIVITY_PASSED\n";
    return 0;
}

}  // namespace xvatsim::tools::radio_receive_activity
