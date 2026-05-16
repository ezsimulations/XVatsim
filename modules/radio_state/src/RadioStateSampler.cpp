#include "XVatsim/modules/radio_state/RadioStateSampler.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMProcessing.h"

namespace xvatsim::modules::radio_state {

namespace {

constexpr int kTransponderModeOff = 0;
constexpr int kTransponderModeStandby = 1;
constexpr int kTransponderModeOn = 2;
constexpr int kTransponderModeAltitude = 3;
constexpr int kTransponderModeTest = 4;
constexpr int kTransponderModeGround = 5;
constexpr int kTransponderModeTaOnly = 6;
constexpr int kTransponderModeTaRa = 7;
constexpr int kMaxDataRefStringBytes = 16 * 1024;

bool ReadDataBackedBool(XPLMDataRef dataRef) {
    std::array<std::byte, 8> buffer{};
    const auto size = XPLMGetDatab(dataRef, nullptr, 0, 0);
    if (size <= 0) {
        return false;
    }

    const auto readSize = std::min<int>(size, static_cast<int>(buffer.size()));
    XPLMGetDatab(dataRef, buffer.data(), 0, readSize);

    for (int index = 0; index < readSize; ++index) {
        if (std::to_integer<unsigned char>(buffer[static_cast<std::size_t>(index)]) != 0U) {
            return true;
        }
    }

    return false;
}

int ReadDataBackedInt(XPLMDataRef dataRef) {
    std::array<std::byte, 8> buffer{};
    const auto size = XPLMGetDatab(dataRef, nullptr, 0, 0);
    if (size <= 0) {
        return 0;
    }

    const auto readSize = std::min<int>(size, static_cast<int>(buffer.size()));
    XPLMGetDatab(dataRef, buffer.data(), 0, readSize);
    if (readSize >= static_cast<int>(sizeof(int))) {
        int value = 0;
        std::memcpy(&value, buffer.data(), sizeof(int));
        return value;
    }

    int value = 0;
    for (int index = 0; index < readSize; ++index) {
        value |= (std::to_integer<unsigned char>(buffer[static_cast<std::size_t>(index)]) << (index * 8));
    }
    return value;
}

float ReadDataBackedFloat(XPLMDataRef dataRef) {
    std::array<std::byte, 8> buffer{};
    const auto size = XPLMGetDatab(dataRef, nullptr, 0, 0);
    if (size <= 0) {
        return 0.0f;
    }

    const auto readSize = std::min<int>(size, static_cast<int>(buffer.size()));
    XPLMGetDatab(dataRef, buffer.data(), 0, readSize);
    if (readSize >= static_cast<int>(sizeof(float))) {
        float value = 0.0f;
        std::memcpy(&value, buffer.data(), sizeof(float));
        if (std::isfinite(value)) {
            return value;
        }
    }

    return static_cast<float>(ReadDataBackedInt(dataRef));
}

}  // namespace

RadioStateSampler::~RadioStateSampler() {
    if (didRegisterActivityPolling_) {
        XPLMUnregisterFlightLoopCallback(ActivityPollCallback, this);
    }
}

void RadioStateSampler::Reset() {
    if (didRegisterActivityPolling_) {
        XPLMUnregisterFlightLoopCallback(ActivityPollCallback, this);
        didRegisterActivityPolling_ = false;
    }
    ResetDataRefs();
    cachedCom1RxSignal_ = false;
    cachedCom2RxSignal_ = false;
    lastCom1RxSeenSeconds_ = -1000.0f;
    lastCom2RxSeenSeconds_ = -1000.0f;
}

brain::RadioStateSnapshot RadioStateSampler::Sample() {
    ResolveDataRefs();
    EnsureActivityPolling();

    brain::RadioStateSnapshot snapshot;
    snapshot.valid = com1ActiveFrequencyRef_ != nullptr && com2ActiveFrequencyRef_ != nullptr;
    if (!snapshot.valid) {
        return snapshot;
    }

    snapshot.com1ActiveFrequency = FormatFrequencyChannel(
        XPLMGetDatai(static_cast<XPLMDataRef>(com1ActiveFrequencyRef_)));
    snapshot.com2ActiveFrequency = FormatFrequencyChannel(
        XPLMGetDatai(static_cast<XPLMDataRef>(com2ActiveFrequencyRef_)));
    if (snapshot.com1ActiveFrequency.empty() || snapshot.com2ActiveFrequency.empty()) {
        return {};
    }

    if (com1StandbyFrequencyRef_ != nullptr) {
        snapshot.com1StandbyFrequency = FormatFrequencyChannel(
            XPLMGetDatai(static_cast<XPLMDataRef>(com1StandbyFrequencyRef_)));
    }

    snapshot.com1Powered = ReadBool(com1PowerRef_) || ReadBool(groundComPowerRef_);
    snapshot.com2Powered = ReadBool(com2PowerRef_);

    auto transmitSelection = ReadInt(audioComSelectionRef_);
    const auto manualTransmitSelection = ReadInt(audioComSelectionManualRef_);
    if (transmitSelection == 0 && manualTransmitSelection != 0) {
        transmitSelection = manualTransmitSelection;
    }

    const auto audioAuto = ReadBool(audioSelectionAutoRef_);
    const auto baseCom1ReceiveSelected =
        IsReceiveSelected(audioAuto, ReadBool(audioSelectionCom1Ref_), 6);
    const auto baseCom2ReceiveSelected =
        IsReceiveSelected(audioAuto, ReadBool(audioSelectionCom2Ref_), 7);
    const auto splitAudioEnabled = ReadBool(xpilotSplitAudioRef_);
    const auto xpilotCom1OnHeadset = ReadBool(xpilotCom1HeadsetRef_);
    const auto xpilotCom2OnHeadset = ReadBool(xpilotCom2HeadsetRef_);
    const auto xpilotCom1Station = ReadString(xpilotCom1StationCallsignRef_);
    const auto xpilotCom2Station = ReadString(xpilotCom2StationCallsignRef_);
    const auto com1ReceiveSelected =
        splitAudioEnabled ? (xpilotCom1OnHeadset || baseCom1ReceiveSelected) : baseCom1ReceiveSelected;
    const auto com2ReceiveSelected =
        splitAudioEnabled ? (xpilotCom2OnHeadset || baseCom2ReceiveSelected) : baseCom2ReceiveSelected;

    const auto pttActive = ReadBool(xpilotPttRef_);
    const auto xpilotCom1RxActive = ReadBool(xpilotCom1RxRef_);
    const auto xpilotCom2RxActive = ReadBool(xpilotCom2RxRef_);
    const auto nowSeconds = XPLMGetElapsedTime();
    const auto latchedCom1RxActive =
        cachedCom1RxSignal_ || ((nowSeconds - lastCom1RxSeenSeconds_) <= 0.35f);
    const auto latchedCom2RxActive =
        cachedCom2RxSignal_ || ((nowSeconds - lastCom2RxSeenSeconds_) <= 0.35f);
    const auto com1Active = ReadBool(com1ActiveRef_);
    const auto com2Active = ReadBool(com2ActiveRef_);
    const auto com1RxOverride = ReadBool(com1RxOverrideRef_);
    const auto com2RxOverride = ReadBool(com2RxOverrideRef_);
    const auto com1TxOverride = ReadBool(com1TxOverrideRef_);
    const auto com2TxOverride = ReadBool(com2TxOverrideRef_);
    const auto com1LikelyUsable =
        xpilotCom1OnHeadset || xpilotCom1RxActive || !xpilotCom1Station.empty() ||
        latchedCom1RxActive || ReadBool(com1RxRef_) || com1Active || com1RxOverride || com1TxOverride ||
        ((pttActive || ReadBool(com1TxRef_) || com1TxOverride) && IsTransmitSelected(transmitSelection, 6));
    const auto com2LikelyUsable =
        xpilotCom2OnHeadset || xpilotCom2RxActive || !xpilotCom2Station.empty() ||
        latchedCom2RxActive || ReadBool(com2RxRef_) || com2Active || com2RxOverride || com2TxOverride ||
        ((pttActive || ReadBool(com2TxRef_) || com2TxOverride) && IsTransmitSelected(transmitSelection, 7));
    snapshot.com1Powered = snapshot.com1Powered || com1LikelyUsable;
    snapshot.com2Powered = snapshot.com2Powered || com2LikelyUsable;
    const auto com1SelectedForTx = IsTransmitSelected(transmitSelection, 6);
    const auto com2SelectedForTx = IsTransmitSelected(transmitSelection, 7);
    snapshot.com1TxAvailable =
        snapshot.com1Powered && (com1SelectedForTx || ReadBool(com1TxRef_));
    snapshot.com2TxAvailable =
        snapshot.com2Powered && (com2SelectedForTx || ReadBool(com2TxRef_));
    snapshot.com1RxAvailable =
        snapshot.com1Powered &&
        (com1ReceiveSelected || xpilotCom1OnHeadset || xpilotCom1RxActive || latchedCom1RxActive);
    snapshot.com2RxAvailable =
        snapshot.com2Powered &&
        (com2ReceiveSelected || xpilotCom2OnHeadset || xpilotCom2RxActive || latchedCom2RxActive);

    const auto userAircraftTransmitting = ReadBool(userAircraftTransmittingRef_);
    snapshot.com1TxActive =
        snapshot.com1Powered &&
        ((com1SelectedForTx && (pttActive || userAircraftTransmitting)) ||
         ReadBool(com1TxRef_) ||
         com1TxOverride);
    snapshot.com2TxActive =
        snapshot.com2Powered &&
        ((com2SelectedForTx && (pttActive || userAircraftTransmitting)) ||
         ReadBool(com2TxRef_) ||
         com2TxOverride);
    snapshot.com1RxActive =
        snapshot.com1Powered &&
        (xpilotCom1RxActive ||
         latchedCom1RxActive ||
         com1RxOverride ||
         ReadBool(com1RxRef_) ||
         com1Active);
    snapshot.com2RxActive =
        snapshot.com2Powered &&
        (xpilotCom2RxActive ||
         latchedCom2RxActive ||
         com2RxOverride ||
         ReadBool(com2RxRef_) ||
         com2Active);

    snapshot.com1TxAvailable = snapshot.com1TxAvailable || snapshot.com1TxActive;
    snapshot.com2TxAvailable = snapshot.com2TxAvailable || snapshot.com2TxActive;
    snapshot.com1RxAvailable = snapshot.com1RxAvailable || snapshot.com1RxActive;
    snapshot.com2RxAvailable = snapshot.com2RxAvailable || snapshot.com2RxActive;

    const auto transponderMode = ReadInt(transponderModeRef_);
    const auto legacyTransponderMode = ReadInt(legacyTransponderModeRef_);
    const auto rotateMd11AtcMode = ReadInt(rotateMd11AtcModeRef_);
    snapshot.modeCActive =
        IsModeCEquivalent(transponderMode) ||
        IsModeCEquivalent(legacyTransponderMode) ||
        IsRotateMd11ModeCEquivalent(rotateMd11AtcMode);
    return snapshot;
}

void RadioStateSampler::ResolveDataRefs() {
    auto resolveIfMissing = [](void*& dataRef, const char* name) {
        if (dataRef == nullptr) {
            dataRef = XPLMFindDataRef(name);
        }
    };

    resolveIfMissing(com1ActiveFrequencyRef_, "sim/cockpit2/radios/actuators/com1_frequency_hz_833");
    resolveIfMissing(com2ActiveFrequencyRef_, "sim/cockpit2/radios/actuators/com2_frequency_hz_833");
    resolveIfMissing(com1StandbyFrequencyRef_, "sim/cockpit2/radios/actuators/com1_standby_frequency_hz_833");
    resolveIfMissing(com1PowerRef_, "sim/cockpit2/radios/actuators/com1_power");
    resolveIfMissing(com2PowerRef_, "sim/cockpit2/radios/actuators/com2_power");
    resolveIfMissing(groundComPowerRef_, "sim/cockpit2/switches/gnd_com_power_on");
    resolveIfMissing(audioComSelectionRef_, "sim/cockpit2/radios/actuators/audio_com_selection");
    resolveIfMissing(audioComSelectionManualRef_, "sim/cockpit2/radios/actuators/audio_com_selection_man");
    resolveIfMissing(audioSelectionAutoRef_, "sim/cockpit2/radios/actuators/audio_selection_com_auto");
    resolveIfMissing(audioSelectionCom1Ref_, "sim/cockpit2/radios/actuators/audio_selection_com1");
    resolveIfMissing(audioSelectionCom2Ref_, "sim/cockpit2/radios/actuators/audio_selection_com2");
    resolveIfMissing(com1TxRef_, "sim/atc/com1_tx");
    resolveIfMissing(com2TxRef_, "sim/atc/com2_tx");
    resolveIfMissing(com1RxRef_, "sim/atc/com1_rx");
    resolveIfMissing(com2RxRef_, "sim/atc/com2_rx");
    resolveIfMissing(com1ActiveRef_, "sim/atc/com1_active");
    resolveIfMissing(com2ActiveRef_, "sim/atc/com2_active");
    resolveIfMissing(com1RxOverrideRef_, "sim/atc/com1_rx_override");
    resolveIfMissing(com2RxOverrideRef_, "sim/atc/com2_rx_override");
    resolveIfMissing(com1TxOverrideRef_, "sim/atc/com1_tx_override");
    resolveIfMissing(com2TxOverrideRef_, "sim/atc/com2_tx_override");
    resolveIfMissing(userAircraftTransmittingRef_, "sim/atc/user_aircraft_transmitting");
    resolveIfMissing(xpilotCom1RxRef_, "xpilot/audio/com1_rx");
    resolveIfMissing(xpilotCom2RxRef_, "xpilot/audio/com2_rx");
    resolveIfMissing(xpilotCom1HeadsetRef_, "xpilot/audio/com1_on_headset");
    resolveIfMissing(xpilotCom2HeadsetRef_, "xpilot/audio/com2_on_headset");
    resolveIfMissing(xpilotAudioVuRef_, "xpilot/audio/vu");
    resolveIfMissing(xpilotPttRef_, "xpilot/ptt");
    resolveIfMissing(xpilotSplitAudioRef_, "xpilot/audio/split_audio_channels");
    resolveIfMissing(xpilotCom1StationCallsignRef_, "xpilot/com1_station_callsign");
    resolveIfMissing(xpilotCom2StationCallsignRef_, "xpilot/com2_station_callsign");
    resolveIfMissing(transponderModeRef_, "sim/cockpit2/radios/actuators/transponder_mode");
    resolveIfMissing(legacyTransponderModeRef_, "sim/cockpit/radios/transponder_mode");
    resolveIfMissing(rotateMd11AtcModeRef_, "Rotate/aircraft/controls/atc_mode_sel");

    if (com1ActiveFrequencyRef_ == nullptr) {
        com1ActiveFrequencyRef_ =
            XPLMFindDataRef("sim/cockpit/radios/com1_freq_hz");
    }
    if (com2ActiveFrequencyRef_ == nullptr) {
        com2ActiveFrequencyRef_ =
            XPLMFindDataRef("sim/cockpit/radios/com2_freq_hz");
    }
    if (com1StandbyFrequencyRef_ == nullptr) {
        com1StandbyFrequencyRef_ =
            XPLMFindDataRef("sim/cockpit/radios/com1_stdby_freq_hz");
        if (com1StandbyFrequencyRef_ != nullptr) {
            com1StandbyUsesLegacyFormat_ = true;
        }
    }

    didResolveDataRefs_ =
        com1ActiveFrequencyRef_ != nullptr &&
        com2ActiveFrequencyRef_ != nullptr;
}

void RadioStateSampler::ResetDataRefs() {
    com1ActiveFrequencyRef_ = nullptr;
    com2ActiveFrequencyRef_ = nullptr;
    com1StandbyFrequencyRef_ = nullptr;
    com1StandbyUsesLegacyFormat_ = false;
    com1PowerRef_ = nullptr;
    com2PowerRef_ = nullptr;
    groundComPowerRef_ = nullptr;
    audioComSelectionRef_ = nullptr;
    audioComSelectionManualRef_ = nullptr;
    audioSelectionAutoRef_ = nullptr;
    audioSelectionCom1Ref_ = nullptr;
    audioSelectionCom2Ref_ = nullptr;
    com1TxRef_ = nullptr;
    com2TxRef_ = nullptr;
    com1RxRef_ = nullptr;
    com2RxRef_ = nullptr;
    com1ActiveRef_ = nullptr;
    com2ActiveRef_ = nullptr;
    com1RxOverrideRef_ = nullptr;
    com2RxOverrideRef_ = nullptr;
    com1TxOverrideRef_ = nullptr;
    com2TxOverrideRef_ = nullptr;
    userAircraftTransmittingRef_ = nullptr;
    xpilotCom1RxRef_ = nullptr;
    xpilotCom2RxRef_ = nullptr;
    xpilotCom1HeadsetRef_ = nullptr;
    xpilotCom2HeadsetRef_ = nullptr;
    xpilotAudioVuRef_ = nullptr;
    xpilotPttRef_ = nullptr;
    xpilotSplitAudioRef_ = nullptr;
    xpilotCom1StationCallsignRef_ = nullptr;
    xpilotCom2StationCallsignRef_ = nullptr;
    transponderModeRef_ = nullptr;
    legacyTransponderModeRef_ = nullptr;
    rotateMd11AtcModeRef_ = nullptr;
    didResolveDataRefs_ = false;
}

void RadioStateSampler::EnsureActivityPolling() {
    if (didRegisterActivityPolling_) {
        return;
    }

    XPLMRegisterFlightLoopCallback(ActivityPollCallback, 0.05f, this);
    didRegisterActivityPolling_ = true;
}

bool RadioStateSampler::ReadBool(const void* dataRef) {
    if (dataRef == nullptr) {
        return false;
    }

    const auto dataRefHandle = static_cast<XPLMDataRef>(const_cast<void*>(dataRef));
    const auto dataRefTypes = XPLMGetDataRefTypes(dataRefHandle);
    if ((dataRefTypes & xplmType_Int) != 0) {
        return XPLMGetDatai(dataRefHandle) != 0;
    }
    if ((dataRefTypes & xplmType_Float) != 0) {
        return XPLMGetDataf(dataRefHandle) > 0.5f;
    }
    if ((dataRefTypes & xplmType_Double) != 0) {
        return XPLMGetDatad(dataRefHandle) > 0.5;
    }
    if ((dataRefTypes & xplmType_Data) != 0) {
        return ReadDataBackedBool(dataRefHandle);
    }

    return false;
}

int RadioStateSampler::ReadInt(const void* dataRef) {
    if (dataRef == nullptr) {
        return 0;
    }

    const auto dataRefHandle = static_cast<XPLMDataRef>(const_cast<void*>(dataRef));
    const auto dataRefTypes = XPLMGetDataRefTypes(dataRefHandle);
    if ((dataRefTypes & xplmType_Int) != 0) {
        return XPLMGetDatai(dataRefHandle);
    }
    if ((dataRefTypes & xplmType_Float) != 0) {
        return static_cast<int>(std::round(XPLMGetDataf(dataRefHandle)));
    }
    if ((dataRefTypes & xplmType_Double) != 0) {
        return static_cast<int>(std::round(XPLMGetDatad(dataRefHandle)));
    }
    if ((dataRefTypes & xplmType_Data) != 0) {
        return ReadDataBackedInt(dataRefHandle);
    }

    return 0;
}

float RadioStateSampler::ReadFloat(const void* dataRef) {
    if (dataRef == nullptr) {
        return 0.0f;
    }

    const auto dataRefHandle = static_cast<XPLMDataRef>(const_cast<void*>(dataRef));
    const auto dataRefTypes = XPLMGetDataRefTypes(dataRefHandle);
    if ((dataRefTypes & xplmType_Float) != 0) {
        return XPLMGetDataf(dataRefHandle);
    }
    if ((dataRefTypes & xplmType_Int) != 0) {
        return static_cast<float>(XPLMGetDatai(dataRefHandle));
    }
    if ((dataRefTypes & xplmType_Double) != 0) {
        return static_cast<float>(XPLMGetDatad(dataRefHandle));
    }
    if ((dataRefTypes & xplmType_Data) != 0) {
        return ReadDataBackedFloat(dataRefHandle);
    }

    return 0.0f;
}

std::string RadioStateSampler::ReadString(const void* dataRef) {
    if (dataRef == nullptr) {
        return {};
    }

    const auto dataRefHandle = static_cast<XPLMDataRef>(const_cast<void*>(dataRef));
    const auto dataRefTypes = XPLMGetDataRefTypes(dataRefHandle);
    if ((dataRefTypes & xplmType_Data) == 0) {
        return {};
    }

    const auto size = XPLMGetDatab(dataRefHandle, nullptr, 0, 0);
    if (size <= 0) {
        return {};
    }
    if (size > kMaxDataRefStringBytes) {
        return {};
    }

    std::vector<char> buffer(static_cast<std::size_t>(size) + 1U, '\0');
    XPLMGetDatab(dataRefHandle, buffer.data(), 0, size);
    return std::string(buffer.data());
}

bool RadioStateSampler::IsTransmitSelected(int selectorValue, int radioSelectorValue) const {
    return selectorValue == radioSelectorValue;
}

bool RadioStateSampler::IsModeCEquivalent(int transponderMode) {
    switch (transponderMode) {
        case kTransponderModeAltitude:
        case kTransponderModeGround:
        case kTransponderModeTaOnly:
        case kTransponderModeTaRa:
            return true;
        case kTransponderModeOff:
        case kTransponderModeStandby:
        case kTransponderModeOn:
        case kTransponderModeTest:
        default:
            return false;
    }
}

bool RadioStateSampler::IsRotateMd11ModeCEquivalent(int transponderMode) {
    return transponderMode == 2 || transponderMode == 3;
}

bool RadioStateSampler::IsReceiveSelected(
    bool audioAuto,
    bool manualSelection,
    int radioSelectorValue) const {
    if (audioAuto) {
        return IsTransmitSelected(ReadInt(audioComSelectionRef_), radioSelectorValue) ||
               IsTransmitSelected(ReadInt(audioComSelectionManualRef_), radioSelectorValue);
    }

    return manualSelection;
}

float RadioStateSampler::ActivityPollCallback(
    float elapsedSinceLastCall,
    float elapsedTimeSinceLastFlightLoop,
    int counter,
    void* refcon) {
    (void)elapsedSinceLastCall;
    (void)elapsedTimeSinceLastFlightLoop;
    (void)counter;

    auto* self = static_cast<RadioStateSampler*>(refcon);
    if (self == nullptr) {
        return 0.05f;
    }

    const auto nowSeconds = XPLMGetElapsedTime();
    self->cachedCom1RxSignal_ =
        ReadBool(self->xpilotCom1RxRef_) ||
        ReadBool(self->com1RxRef_) ||
        ReadBool(self->com1RxOverrideRef_) ||
        ReadBool(self->com1ActiveRef_);
    self->cachedCom2RxSignal_ =
        ReadBool(self->xpilotCom2RxRef_) ||
        ReadBool(self->com2RxRef_) ||
        ReadBool(self->com2RxOverrideRef_) ||
        ReadBool(self->com2ActiveRef_);

    if (self->cachedCom1RxSignal_) {
        self->lastCom1RxSeenSeconds_ = nowSeconds;
    }
    if (self->cachedCom2RxSignal_) {
        self->lastCom2RxSeenSeconds_ = nowSeconds;
    }

    return 0.05f;
}

std::string RadioStateSampler::FormatFrequencyChannel(int channelNumber) {
    if (channelNumber <= 0) {
        return {};
    }

    if (channelNumber < 100000) {
        channelNumber *= 10;
    }

    const auto major = channelNumber / 1000;
    const auto minor = channelNumber % 1000;

    std::string formatted = std::to_string(major) + ".";
    if (minor < 100) {
        formatted += "0";
    }
    if (minor < 10) {
        formatted += "0";
    }
    formatted += std::to_string(minor);
    return formatted;
}

int RadioStateSampler::ParseFrequencyChannel(const std::string& frequency, bool useLegacyChannelFormat) {
    std::string digits;
    bool sawDecimal = false;
    int decimals = 0;
    for (const auto character : frequency) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digits.push_back(character);
            if (sawDecimal && decimals < 3) {
                ++decimals;
            }
            continue;
        }

        if (character == '.' && !sawDecimal) {
            sawDecimal = true;
        }
    }

    if (digits.empty()) {
        return 0;
    }

    if (sawDecimal) {
        while (decimals < 3) {
            digits.push_back('0');
            ++decimals;
        }
    } else if (digits.size() == 5) {
        digits.push_back('0');
    }

    int channelNumber = 0;
    try {
        channelNumber = std::stoi(digits);
    } catch (...) {
        return 0;
    }

    if (useLegacyChannelFormat) {
        channelNumber /= 10;
    }

    return channelNumber;
}

bool RadioStateSampler::IsValidComFrequencyChannel(
    int channelNumber,
    bool useLegacyChannelFormat) {
    const auto normalizedChannelNumber =
        useLegacyChannelFormat ? channelNumber * 10 : channelNumber;
    return normalizedChannelNumber >= 118000 && normalizedChannelNumber <= 136975;
}

bool RadioStateSampler::SetCom1StandbyFrequency(const std::string& frequency) {
    ResolveDataRefs();
    if (com1StandbyFrequencyRef_ == nullptr) {
        return false;
    }

    const auto dataRefHandle = static_cast<XPLMDataRef>(com1StandbyFrequencyRef_);
    const auto dataRefTypes = XPLMGetDataRefTypes(dataRefHandle);
    if ((dataRefTypes & xplmType_Int) == 0) {
        return false;
    }
    if (!XPLMCanWriteDataRef(dataRefHandle)) {
        return false;
    }

    const auto channelNumber = ParseFrequencyChannel(frequency, com1StandbyUsesLegacyFormat_);
    if (channelNumber <= 0 ||
        !IsValidComFrequencyChannel(channelNumber, com1StandbyUsesLegacyFormat_)) {
        return false;
    }

    XPLMSetDatai(dataRefHandle, channelNumber);
    return true;
}

}  // namespace xvatsim::modules::radio_state
