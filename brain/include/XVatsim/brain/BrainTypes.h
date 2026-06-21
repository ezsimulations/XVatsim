#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xvatsim::brain {

constexpr double kBrainOwnedMaxRadioBoardCandidateDistanceNm = 300.0;

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

struct TransceiverParserHygieneCounters {
    int invalidClientCallsign = 0;
    int invalidTransceiverFrequency = 0;
    int invalidPosition = 0;
    int invalidHeight = 0;
    int parseException = 0;
    int emptyPayload = 0;
    bool maxTransceiverTruncation = false;
};

struct TransceiverSourceEvidenceSnapshot {
    bool feedCacheExists = false;
    int cachedTransceiverCount = 0;
    bool sourceControllerCountKnown = false;
    int sourceControllerCount = 0;
    bool cacheFresh = false;
    bool cacheStale = true;
    bool holdoverUsed = false;
    bool holdoverExpired = false;
    bool hasFeedAgeSeconds = false;
    long long feedAgeSeconds = 0;
    bool fetchAttempted = false;
    bool fetchInProgress = false;
    bool fetchFailed = false;
    std::string failureReason;
    TransceiverParserHygieneCounters parser;
};

struct TransceiverStationEvidenceSnapshot {
    std::string sourceFrequency;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double heightAglFt = 0.0;
    bool hasAircraftDistance = false;
    double aircraftDistanceNm = 0.0;
    double maxCandidateDistanceNm = kBrainOwnedMaxRadioBoardCandidateDistanceNm;
    bool withinMaxCandidateDistance = false;
    bool hasReceivableRange = false;
    double receivableRangeNm = 0.0;
    bool withinReceivableRange = false;
    double score = 0.0;
    bool bestByModuleScore = false;
    bool transceiverFrequencyGuard = false;
};

struct TransceiverControllerEvidenceSnapshot {
    std::string callsign;
    std::string controllerFrequency;
    int facility = 0;
    bool actionable = false;
    bool atis = false;
    int visualRangeNm = 0;
    bool hasTransceiverEntry = false;
    int matchingTransceiverCount = 0;
    std::string resolvedDisplayFrequency;
    std::string displayFrequencySource;
    std::string displayFrequencyUnavailableReason;
    std::string pathUnavailableReason;
    bool controllerFrequencyGuard = false;
    bool transceiverFrequencyGuard = false;
    std::vector<TransceiverStationEvidenceSnapshot> stations;
};

struct TransceiverResolutionSnapshot {
    bool available = false;
    bool stale = true;
    int receivableControllers = 0;
    int distanceRejectedControllers = 0;
    double maxCandidateDistanceNm = kBrainOwnedMaxRadioBoardCandidateDistanceNm;
    std::string statusLine;
    std::string resolutionPath;
    // Transceiver resolver paths are evidence producers. When
    // controllerEvidence is present, this vector is a legacy compatibility
    // projection only; brain-owned workers build live candidates from evidence.
    bool candidatesCompatibilityOnly = false;
    int droppedBeforeBrainControllers = 0;
    std::vector<ReceivableControllerSnapshot> candidates;
    TransceiverSourceEvidenceSnapshot sourceEvidence;
    std::vector<TransceiverControllerEvidenceSnapshot> controllerEvidence;
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

struct AuthorityRelevanceSourceEvidenceSnapshot {
    bool scheduled = false;
    std::string scheduleReason;
    bool controllerFeedAvailable = false;
    bool controllerFeedStale = true;
    std::uint64_t controllerFeedGeneration = 0;
    bool sourceControllerCountKnown = false;
    int sourceControllerCount = 0;
    bool routeSnapshotAvailable = false;
    bool routeSnapshotStale = true;
    bool routeResolved = false;
    std::string workStage;
    double workWindowNm = 0.0;
    int workDeferredSectorCount = 0;
    std::vector<std::string> routeAuthorityKeys;
    std::vector<std::string> routeAuthorityMatchKeys;
    bool authorityTransceiverSnapshotPresent = false;
    bool authorityTransceiverAvailable = false;
    bool authorityTransceiverStale = true;
    int authorityTransceiverCandidateCount = 0;
    bool cacheHit = false;
    std::string cacheStatus;
    std::string cacheReason;
};

struct AuthorityDecisionEvidenceSnapshot {
    std::string authorityId;
    std::string authoritySource;
    std::string authorityKind;
    std::string polygonKey;
    std::string matchedPattern;
    bool accepted = false;
    bool oldRouteScopeMatched = false;
    bool oldRelevantAuthoritySurvivor = false;
    std::vector<std::string> rejectionReasons;
};

struct AuthorityPolygonEvidenceSnapshot {
    std::string callsign;
    std::string authorityId;
    std::string polygonId;
    std::string polygonKey;
    std::string matchedPattern;
    std::string authoritySource;
    std::string authorityKind;
    bool routeKeyMatch = false;
    bool routeFamilyMatch = false;
    bool routeEndpointMatch = false;
    bool inOldScopedCatalog = false;
    std::string oldScopedOutReason;
    bool routeGeometryRelevant = false;
    bool aircraftInside = false;
    bool routeIntersects = false;
    double routeEntryDistanceNm = 0.0;
    bool activePolygon = false;
    std::string activeProofSource;
    std::string activeProofDetail;
    bool routeKeyCompatible = false;
    bool geometryCompatible = true;
    bool oldCompatibilityRelevantSurvivor = false;
    std::string compatibilityFilteredReason;
};

struct AuthorityTransceiverRouteProofEvidenceSnapshot {
    std::string callsign;
    int stationCandidateCount = 0;
    std::string stationCallsign;
    std::string stationFrequency;
    double stationLatitudeDeg = 0.0;
    double stationLongitudeDeg = 0.0;
    double stationScore = 0.0;
    bool bestByModuleScore = false;
    std::string polygonId;
    std::string polygonKey;
    std::string authoritySource;
    std::string authorityKind;
    double stationPolygonDistanceNm = 0.0;
    double toleranceNm = 0.0;
    bool withinTolerance = false;
    bool sourceOwnershipMatch = false;
    bool unownedBorderMismatch = false;
    bool blockedByDirectActiveProof = false;
    bool noStationCandidates = false;
    bool oldProofSurvivor = false;
    std::string proofRejectionReason;
};

struct AuthorityDuplicatedAtisProofEvidenceSnapshot {
    std::string callsign;
    bool textAtisPresent = false;
    std::vector<std::string> extractedCoveredTokens;
    std::string coveredToken;
    std::vector<std::string> matchedRouteAuthorityAliases;
    std::string authorityId;
    std::string authoritySource;
    std::string authorityKind;
    std::string polygonKey;
    bool sourceKindAllowed = false;
    bool routeRelevantPolygonFound = false;
    bool facilityEligible = false;
    bool missingSourceOwnership = false;
    bool oldProofSurvivor = false;
    std::string proofRejectionReason;
};

struct AuthorityControllerEvidenceSnapshot {
    std::string callsign;
    std::string frequency;
    int facility = 0;
    bool actionable = false;
    bool atis = false;
    bool guardFrequency = false;
    bool emptyCallsign = false;
    bool airportLocalCandidate = false;
    bool airspaceAuthorityCandidate = false;
    bool sourceControllerConsidered = false;
    std::vector<std::string> evidenceReasons;
    std::vector<AuthorityDecisionEvidenceSnapshot> authorityDecisions;
    std::vector<AuthorityPolygonEvidenceSnapshot> activePolygons;
};

struct AuthorityRelevanceEvidenceSnapshot {
    AuthorityRelevanceSourceEvidenceSnapshot source;
    std::vector<AuthorityControllerEvidenceSnapshot> controllerEvidence;
    std::vector<AuthorityPolygonEvidenceSnapshot> polygonEvidence;
    std::vector<AuthorityPolygonEvidenceSnapshot> activePolygonEvidence;
    std::vector<AuthorityTransceiverRouteProofEvidenceSnapshot>
        transceiverRouteProofEvidence;
    std::vector<AuthorityDuplicatedAtisProofEvidenceSnapshot>
        duplicatedAtisProofEvidence;
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
    // True when the route_sector-built survivor vector is retained only for
    // parity diagnostics. Live relevantAuthorities must then come from brain
    // evidence decisions, not route_sector survivor construction.
    bool relevantAuthoritiesCompatibilityOnly = false;
    bool liveRelevantAuthoritiesBrainOwned = false;
    int compatibilityRelevantAuthorityCount = 0;
    int droppedBeforeBrainControllers = 0;
    AuthorityRelevanceEvidenceSnapshot evidence;
    // Old route_sector survivor construction kept for diagnostics/parity only
    // after brain-owned projection has rebuilt live relevantAuthorities.
    std::vector<RelevantAuthoritySnapshot> compatibilityRelevantAuthorities;
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
    std::string sourceEvidenceId;
    std::string sourceEvidenceType;
    std::string sourceEvidenceDomain;
    std::string sourceDecisionId;
    std::string sourceEvidenceLinkStatus;
    std::string sourceEvidenceMissingReason;
    std::string stableCompletionKey;
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
    std::string sourceEvidenceId;
    std::string sourceEvidenceType;
    std::string sourceEvidenceDomain;
    std::string sourceDecisionId;
    std::string sourceEvidenceLinkStatus;
    std::string sourceEvidenceMissingReason;
    std::string displayDecisionId;
    std::string overlayCapDecisionId;
    std::string stableCompletionKey;
    std::string sourceOwnedStableCompletionKey;
    std::string generatedFallbackStableCompletionKey;
    bool sourceOwnedStableCompletionKeyPresent = false;
    bool sourceOwnedKeyMigrationReady = false;
    bool sourceOwnedKeyPlanContextAvailable = false;
    bool sourceOwnedKeyBehaviorConsumerEnabled = false;
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

enum class OverlayVersionTone {
    Unknown,
    Current,
    UpdateAvailable,
    Error,
};

struct OverlayVersionSnapshot {
    std::string text;
    std::string alternateText;
    OverlayVersionTone tone = OverlayVersionTone::Unknown;
    bool rotateAlternate = false;
};

enum class OverlayNoticeSeverity {
    Info,
    Success,
    Warning,
    Error,
};

struct OverlayNoticeSnapshot {
    bool visible = false;
    OverlayNoticeSeverity severity = OverlayNoticeSeverity::Info;
    std::string title;
    std::vector<std::string> bodyLines;
    std::string dismissText = "Close";
    bool dismissible = true;
};

enum class OverlayUpdateStatus {
    Unknown,
    Checking,
    Current,
    Available,
    Failed,
};

struct OverlayUpdateSnapshot {
    std::string installedVersion;
    std::string latestVersion;
    std::string downloadPageUrl;
    std::string errorClass;
    OverlayUpdateStatus status = OverlayUpdateStatus::Unknown;
    bool critical = false;
    bool manualNoticeRequested = false;
    bool automaticNoticeRequested = false;
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
    OverlayVersionSnapshot version;
    OverlayNoticeSnapshot systemNotice;
    std::vector<OverlayTextLine> bodyLines;
    bool showMessageAcknowledge = false;
    bool showMessageRecall = false;
};

}  // namespace xvatsim::brain
