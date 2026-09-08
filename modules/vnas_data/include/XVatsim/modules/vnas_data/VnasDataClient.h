#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "XVatsim/brain/BrainTypes.h"
#include "XVatsim/brain/RadioReachableSnapshot.h"

namespace xvatsim::modules::vnas_data {

struct VnasLivePosition {
    std::string facilityId;
    std::string facilityName;
    std::string positionId;
    std::string positionName;
    std::string positionType;
    std::string radioName;
    std::string defaultCallsign;
    std::string frequency;
    std::string areaId;
    bool primary = false;
    bool active = false;
    std::vector<std::string> assumedTcps;
};

struct VnasLiveController {
    std::string artccId;
    std::string primaryFacilityId;
    std::string primaryPositionId;
    std::string vatsimCallsign;
    std::string vatsimFrequency;
    bool active = false;
    bool observer = false;
    std::vector<VnasLivePosition> positions;
};

struct VnasControllerFeedDocument {
    bool hasCache = false;
    bool stale = true;
    std::string updatedAt;
    std::uint64_t stableHash = 0;
    std::vector<VnasLiveController> controllers;
};

struct VnasFacilityPosition {
    std::string positionId;
    std::string positionName;
    std::string callsign;
    std::string frequency;
    std::string areaId;
    std::string areaName;
    std::string tcpId;
    std::string tcpToken;
};

struct VnasFacilityArea {
    std::string areaId;
    std::string areaName;
    std::vector<std::string> airports;
};

struct VnasTcp {
    std::string tcpId;
    std::string tcpToken;
    std::string parentTcpId;
};

struct VnasDepartureCoordination {
    std::string airportIcao;
    std::vector<std::string> receivingTcpTokens;
};

struct VnasFacility {
    std::string facilityId;
    std::string parentFacilityId;
    std::string facilityType;
    std::string facilityName;
    std::vector<std::string> childFacilityIds;
    std::vector<std::string> internalAirports;
    std::vector<VnasFacilityArea> areas;
    std::vector<VnasTcp> tcps;
    bool tcpGraphValid = false;
    std::vector<VnasDepartureCoordination> departureCoordinations;
    std::vector<std::string> departureFrequencies;
    std::vector<VnasFacilityPosition> positions;
};

struct VnasArtccFacilityDocument {
    bool hasCache = false;
    std::string artccId;
    std::string lastUpdatedAt;
    std::uint64_t stableHash = 0;
    std::vector<VnasFacility> facilities;
};

VnasControllerFeedDocument DecodeVnasControllerFeedDocument(
    const std::string& payload);
VnasArtccFacilityDocument DecodeVnasArtccFacilityDocument(
    const std::string& payload);

bool IsUnitedStatesTerminalIcao(const std::string& airportIcao);

xvatsim::brain::VnasTerminalEvidenceSnapshot
BuildVnasTerminalEvidenceSnapshot(
    const VnasControllerFeedDocument& controllerFeed,
    const std::vector<std::shared_ptr<const VnasArtccFacilityDocument>>&
        facilityDocuments,
    const std::string& departureIcao,
    const std::string& arrivalIcao,
    const std::vector<xvatsim::brain::RadioReachableControllerCandidate>&
        candidates,
    bool enabled);

class VnasDataClient {
public:
    VnasDataClient();
    ~VnasDataClient();

    VnasDataClient(const VnasDataClient&) = delete;
    VnasDataClient& operator=(const VnasDataClient&) = delete;

    std::shared_ptr<const xvatsim::brain::VnasTerminalEvidenceSnapshot> Poll(
        const std::string& departureIcao,
        const std::string& arrivalIcao,
        const std::vector<xvatsim::brain::RadioReachableControllerCandidate>&
            candidates,
        bool enabled);
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xvatsim::modules::vnas_data
