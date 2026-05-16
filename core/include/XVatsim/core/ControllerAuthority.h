#pragma once

#include <string>
#include <vector>

namespace xvatsim::core::authority {

enum class AuthoritySource {
    VatSpyFir,
    VatSpyUir,
};

enum class AuthorityKind {
    Center,
};

struct ControllerAuthority {
    std::string id;
    AuthoritySource source = AuthoritySource::VatSpyFir;
    AuthorityKind kind = AuthorityKind::Center;
    std::string name;
    std::string polygonKey;
    std::vector<std::string> lookupKeys;
    std::vector<std::string> controllerPrefixes;
    std::vector<std::string> controllerCallsignPatterns;
    std::string sourceRecord;
};

struct AuthorityDataGap {
    std::string authorityId;
    std::string polygonKey;
    std::string reason;
    std::string sourceRecord;
};

struct ControllerAuthorityCatalog {
    std::vector<ControllerAuthority> authorities;
    std::vector<AuthorityDataGap> dataGaps;
};

struct ActiveControllerAuthority {
    std::string callsign;
    std::string authorityId;
    std::string polygonKey;
    std::string matchedPattern;
};

std::string AuthoritySourceLabel(AuthoritySource source);
std::string NormalizeAuthorityToken(std::string value);
std::string NormalizeControllerCallsign(std::string value);
bool CallsignMatchesPattern(
    const std::string& rawPattern,
    const std::string& rawCallsign);

ControllerAuthorityCatalog CompileVatSpyAuthorityCatalog(
    const std::string& vatspyDat);

std::vector<ActiveControllerAuthority> ResolveControllerAuthority(
    const ControllerAuthorityCatalog& catalog,
    const std::string& callsign,
    int vatsimFacility);

}  // namespace xvatsim::core::authority
