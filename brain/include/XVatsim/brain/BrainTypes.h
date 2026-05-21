#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xvatsim::brain {

enum class OverlayMode {
    Dormant,
    Prewarm,
    Active,
    Locked,
    Cooldown,
};

enum class FlightPhase {
    Unknown,
    Parked,
    Taxi,
    GroundRoll,
    Departure,
    Climb,
    Cruise,
    Descent,
    Approach,
};

enum class WorkflowStage {
    None,
    Departure,
    Enroute,
    Arrival,
};

enum class AirportSource {
    None,
    CurrentLocation,
    OnboardFms,
    OnboardGps,
    VatsimFiled,
};

struct AircraftStateSnapshot {
    bool valid = false;
    bool onGround = false;
    bool batteryOn = false;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double altitudeMslFt = 0.0;
    double altitudeOperationalFt = 0.0;
    double altimeterSettingInHg = 29.92;
    bool hasAltimeterSetting = false;
    double altitudeAglFt = 0.0;
    double groundSpeedKt = 0.0;
    double verticalSpeedFpm = 0.0;
    bool hasTrack = false;
    double trackTrueDeg = 0.0;
};

struct PhaseSnapshot {
    FlightPhase phase = FlightPhase::Unknown;
};

struct FlightPlanSnapshot {
    bool available = false;
    std::string currentAirportIcao;
    double currentAirportLatDeg = 0.0;
    double currentAirportLonDeg = 0.0;
    bool hasCurrentAirportCoordinates = false;
    AirportSource currentAirportSource = AirportSource::None;
    std::string departureIcao;
    double departureLatDeg = 0.0;
    double departureLonDeg = 0.0;
    bool hasDepartureCoordinates = false;
    AirportSource departureSource = AirportSource::None;
    std::string destinationIcao;
    double destinationLatDeg = 0.0;
    double destinationLonDeg = 0.0;
    bool hasDestinationCoordinates = false;
    AirportSource destinationSource = AirportSource::None;
};

struct RadioStateSnapshot {
    bool valid = false;
    std::string com1ActiveFrequency;
    std::string com2ActiveFrequency;
    std::string com1StandbyFrequency;
    bool standbyAssistEnabled = false;
    bool com1Powered = false;
    bool com2Powered = false;
    bool com1TxAvailable = false;
    bool com1RxAvailable = false;
    bool com2TxAvailable = false;
    bool com2RxAvailable = false;
    bool com1TxActive = false;
    bool com1RxActive = false;
    bool com2TxActive = false;
    bool com2RxActive = false;
    bool modeCActive = false;
};

struct XPilotSessionSnapshot {
    bool loaded = false;
    bool connected = false;
    std::string callsign;
    std::string rawStatus;
    std::string statusLine;
};

struct XPilotPrivateMessageSnapshot {
    bool loaded = false;
    bool available = false;
    int sequence = 0;
    std::string from;
    std::string body;
};

struct PilotIdentitySnapshot {
    bool connected = false;
    bool ready = false;
    std::string callsign;
    std::string normalizedCallsign;
    std::string statusLine;
};

struct NetworkPlanSnapshot {
    bool feedAvailable = false;
    bool stale = true;
    bool matched = false;
    int cid = 0;
    std::string matchedCallsign;
    std::string departureIcao;
    double departureLatDeg = 0.0;
    double departureLonDeg = 0.0;
    bool hasDepartureCoordinates = false;
    std::string destinationIcao;
    double destinationLatDeg = 0.0;
    double destinationLonDeg = 0.0;
    bool hasDestinationCoordinates = false;
    double filedCruiseAltitudeFt = 0.0;
    bool hasFiledCruiseAltitude = false;
    std::string routeText;
    std::string statusLine;
};

struct ControllerSnapshot {
    std::string callsign;
    std::string frequency;
    int facility = 0;
    int visualRangeNm = 0;
    bool actionable = true;
    bool atis = false;
    std::string textAtis;
};

struct ControllerFeedSnapshot {
    bool available = false;
    bool stale = true;
    std::uint64_t generation = 0;
    int connectedControllers = 0;
    std::string statusLine;
    const std::vector<ControllerSnapshot>* controllers = nullptr;

    const std::vector<ControllerSnapshot>& Controllers() const {
        static const std::vector<ControllerSnapshot> kEmptyControllers;
        if (!available || stale || controllers == nullptr) {
            return kEmptyControllers;
        }
        return *controllers;
    }
};

struct ReceivableControllerSnapshot {
    std::string callsign;
    std::string frequency;
    double distanceNm = 0.0;
    double score = 0.0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct TransceiverResolutionSnapshot {
    bool available = false;
    bool stale = true;
    int receivableControllers = 0;
    std::string statusLine;
    std::vector<ReceivableControllerSnapshot> candidates;
};

struct RouteWaypointSnapshot {
    std::string ident;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct RouteSectorMatchSnapshot {
    std::string identifier;
    double entryDistanceNm = 0.0;
    std::vector<std::string> matchTokens;
    std::vector<std::string> controllerCallsignPatterns;
    std::vector<std::string> controllerPrefixes;
    bool centerCoverage = false;
    bool terminalCoverage = false;
};

struct RouteSectorSnapshot {
    bool available = false;
    bool stale = true;
    bool routeResolved = false;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string departureIcao;
    std::string destinationIcao;
    std::string statusLine;
    std::vector<RouteWaypointSnapshot> waypoints;
    std::vector<RouteSectorMatchSnapshot> currentSectors;
    std::vector<RouteSectorMatchSnapshot> nextSectors;
};

enum class AuthorityRelevanceKind {
    Center,
    Terminal,
    Extension,
};

struct RelevantAuthoritySnapshot {
    std::string callsign;
    std::string frequency;
    std::string authorityId;
    std::string polygonId;
    std::string polygonKey;
    std::string matchedPattern;
    std::string proofSource;
    std::string proofDetail;
    AuthorityRelevanceKind kind = AuthorityRelevanceKind::Center;
    bool aircraftInside = false;
    bool routeIntersects = false;
    double routeEntryDistanceNm = 0.0;
};

struct AuthorityRelevanceSnapshot {
    bool available = false;
    bool stale = true;
    std::uint64_t controllerFeedGeneration = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t terminalCoverageGeneration = 0;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string diagnosticWorkStage;
    double diagnosticWindowNm = 0.0;
    int diagnosticDeferredSectorCount = 0;
    std::string statusLine;
    std::vector<std::string> diagnostics;
    std::vector<RelevantAuthoritySnapshot> relevantAuthorities;
};

struct AirportSectorSnapshot {
    bool available = false;
    bool stale = true;
    bool hasCenterCoverageData = false;
    bool hasTerminalCoverageData = false;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t terminalCoverageGeneration = 0;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string airportIcao;
    std::string statusLine;
    std::vector<RouteSectorMatchSnapshot> coveringSectors;
};

enum class BoardSource {
    None,
    Departure,
    Arrival,
    Enroute,
};

enum class StationRole {
    Other,
    Delivery,
    Ground,
    Tower,
    Departure,
    Approach,
    Center,
    Atis,
    Ctaf,
    Unicom,
};

enum class DisplayRelation {
    Unknown,
    CurrentPolygon,
    NextPolygon,
    ArrivalPrep,
    Filtered,
    Hidden,
};

struct BoardStationSnapshot {
    StationRole role = StationRole::Other;
    std::string callsign;
    std::string frequency;
    bool tuned = false;
    bool sectorActive = false;
    bool online = false;
    bool offline = false;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    std::string polygonKey;
};

struct ModuleBoardSnapshot {
    bool available = false;
    BoardSource source = BoardSource::None;
    std::string airportIcao;
    std::vector<BoardStationSnapshot> stations;
};

struct FinalDisplayStationSnapshot {
    StationRole role = StationRole::Other;
    std::string callsign;
    std::string frequency;
    std::string annotation;
    bool tuned = false;
    bool next = false;
    bool standby = false;
    bool sectorActive = false;
    bool online = false;
    bool offline = false;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    std::string polygonKey;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
};

struct FinalDisplaySnapshot {
    bool available = false;
    BoardSource source = BoardSource::None;
    std::string airportIcao;
    std::vector<FinalDisplayStationSnapshot> stations;
};

enum class OverlayTone {
    Normal,
    Active,
    Next,
};

struct OverlayTextLine {
    std::string text;
    OverlayTone tone = OverlayTone::Normal;
};

enum class RecommendationKind {
    None,
    Controller,
    DepartureCtaf,
    ArrivalCtaf,
};

struct RecommendationSnapshot {
    bool visible = false;
    bool satisfied = false;
    RecommendationKind kind = RecommendationKind::None;
    std::string line;
    std::vector<OverlayTextLine> lines;
    std::string airportIcao;
    std::string frequency;
};

struct ManualQuerySnapshot {
    bool visible = false;
    std::string line;
};

struct OverlayViewModel {
    OverlayMode mode = OverlayMode::Dormant;
    bool visible = false;
    std::string title;
    std::string headerRightText;
    RadioStateSnapshot radioState;
    std::vector<OverlayTextLine> bodyLines;
    bool showMessageAcknowledge = false;
    bool showMessageRecall = false;
};

}  // namespace xvatsim::brain
