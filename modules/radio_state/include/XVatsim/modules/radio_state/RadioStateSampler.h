#pragma once

#include "XPLMProcessing.h"
#include "XVatsim/brain/BrainTypes.h"

namespace xvatsim::modules::radio_state {

class RadioStateSampler {
public:
    RadioStateSampler() = default;
    ~RadioStateSampler();

    brain::RadioStateSnapshot Sample();
    void Reset();
    bool SetCom1StandbyFrequency(const std::string& frequency);

private:
    void ResolveDataRefs();
    void ResetDataRefs();
    static std::string FormatFrequencyChannel(int channelNumber);
    static int ParseFrequencyChannel(const std::string& frequency, bool useLegacyChannelFormat);
    static bool IsValidComFrequencyChannel(int channelNumber, bool useLegacyChannelFormat);
    static bool ReadBool(const void* dataRef);
    static int ReadInt(const void* dataRef);
    static float ReadFloat(const void* dataRef);
    static std::string ReadString(const void* dataRef);
    static bool IsModeCEquivalent(int transponderMode);
    static bool IsRotateMd11ModeCEquivalent(int transponderMode);
    bool IsTransmitSelected(int selectorValue, int radioSelectorValue) const;
    bool IsReceiveSelected(bool audioAuto, bool manualSelection, int radioSelectorValue) const;
    void EnsureActivityPolling();
    static float ActivityPollCallback(
        float elapsedSinceLastCall,
        float elapsedTimeSinceLastFlightLoop,
        int counter,
        void* refcon);

    void* com1ActiveFrequencyRef_ = nullptr;
    void* com2ActiveFrequencyRef_ = nullptr;
    void* com1StandbyFrequencyRef_ = nullptr;
    bool com1StandbyUsesLegacyFormat_ = false;
    void* com1PowerRef_ = nullptr;
    void* com2PowerRef_ = nullptr;
    void* groundComPowerRef_ = nullptr;
    void* audioComSelectionRef_ = nullptr;
    void* audioComSelectionManualRef_ = nullptr;
    void* audioSelectionAutoRef_ = nullptr;
    void* audioSelectionCom1Ref_ = nullptr;
    void* audioSelectionCom2Ref_ = nullptr;
    void* com1TxRef_ = nullptr;
    void* com2TxRef_ = nullptr;
    void* com1RxRef_ = nullptr;
    void* com2RxRef_ = nullptr;
    void* com1ActiveRef_ = nullptr;
    void* com2ActiveRef_ = nullptr;
    void* com1RxOverrideRef_ = nullptr;
    void* com2RxOverrideRef_ = nullptr;
    void* com1TxOverrideRef_ = nullptr;
    void* com2TxOverrideRef_ = nullptr;
    void* userAircraftTransmittingRef_ = nullptr;
    void* xpilotCom1RxRef_ = nullptr;
    void* xpilotCom2RxRef_ = nullptr;
    void* xpilotCom1HeadsetRef_ = nullptr;
    void* xpilotCom2HeadsetRef_ = nullptr;
    void* xpilotAudioVuRef_ = nullptr;
    void* xpilotPttRef_ = nullptr;
    void* xpilotSplitAudioRef_ = nullptr;
    void* xpilotCom1StationCallsignRef_ = nullptr;
    void* xpilotCom2StationCallsignRef_ = nullptr;
    void* transponderModeRef_ = nullptr;
    void* legacyTransponderModeRef_ = nullptr;
    void* rotateMd11AtcModeRef_ = nullptr;
    bool didResolveDataRefs_ = false;
    bool didRegisterActivityPolling_ = false;
    bool cachedCom1RxSignal_ = false;
    bool cachedCom2RxSignal_ = false;
    float lastCom1RxSeenSeconds_ = -1000.0f;
    float lastCom2RxSeenSeconds_ = -1000.0f;
};

}  // namespace xvatsim::modules::radio_state
