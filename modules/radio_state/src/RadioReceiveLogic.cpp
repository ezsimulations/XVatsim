#include "XVatsim/modules/radio_state/RadioStateSampler.h"

namespace xvatsim::modules::radio_state {

bool EvaluateRadioReceiveActive(
    const RadioReceiveActivityEvidence& evidence) {
    return evidence.powered &&
        (evidence.xpilotReceiveActive ||
         evidence.latchedReceiveActive ||
         evidence.receiveOverrideActive ||
         evidence.simulatorReceiveActive);
}

} // namespace xvatsim::modules::radio_state
