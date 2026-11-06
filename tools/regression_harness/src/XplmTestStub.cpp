#include "XPLMDataAccess.h"
#include "XPLMNavigation.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

namespace {

void ClearString(char* value) {
    if (value != nullptr) value[0] = '\0';
}

}  // namespace

XPLMDataRef XPLMFindDataRef(const char*) {
    return nullptr;
}

XPLMDataTypeID XPLMGetDataRefTypes(XPLMDataRef) {
    return xplmType_Unknown;
}

int XPLMGetDatai(XPLMDataRef) {
    return 0;
}

int XPLMGetDatab(XPLMDataRef, void*, int, int) {
    return 0;
}

XPLMNavRef XPLMFindNavAid(
    const char*,
    const char*,
    float*,
    float*,
    int*,
    XPLMNavType) {
    return XPLM_NAV_NOT_FOUND;
}

void XPLMGetNavAidInfo(
    XPLMNavRef,
    XPLMNavType* outType,
    float* outLatitude,
    float* outLongitude,
    float* outHeight,
    int* outFrequency,
    float* outHeading,
    char* outId,
    char* outName,
    char* outRegion) {
    if (outType != nullptr) *outType = xplm_Nav_Unknown;
    if (outLatitude != nullptr) *outLatitude = 0.0f;
    if (outLongitude != nullptr) *outLongitude = 0.0f;
    if (outHeight != nullptr) *outHeight = 0.0f;
    if (outFrequency != nullptr) *outFrequency = 0;
    if (outHeading != nullptr) *outHeading = 0.0f;
    ClearString(outId);
    ClearString(outName);
    ClearString(outRegion);
}

int XPLMCountFMSEntries() {
    return 0;
}

void XPLMGetFMSEntryInfo(
    int,
    XPLMNavType* outType,
    char* outId,
    XPLMNavRef* outRef,
    int* outAltitude,
    float* outLatitude,
    float* outLongitude) {
    if (outType != nullptr) *outType = xplm_Nav_Unknown;
    ClearString(outId);
    if (outRef != nullptr) *outRef = XPLM_NAV_NOT_FOUND;
    if (outAltitude != nullptr) *outAltitude = 0;
    if (outLatitude != nullptr) *outLatitude = 0.0f;
    if (outLongitude != nullptr) *outLongitude = 0.0f;
}

XPLMNavType XPLMGetGPSDestinationType() {
    return xplm_Nav_Unknown;
}

XPLMNavRef XPLMGetGPSDestination() {
    return XPLM_NAV_NOT_FOUND;
}

XPLMPluginID XPLMFindPluginBySignature(const char*) {
    return XPLM_NO_PLUGIN_ID;
}

void XPLMGetPluginInfo(
    XPLMPluginID,
    char* outName,
    char* outFilePath,
    char* outSignature,
    char* outDescription) {
    ClearString(outName);
    ClearString(outFilePath);
    ClearString(outSignature);
    ClearString(outDescription);
}

void XPLMGetSystemPath(char* outSystemPath) {
    ClearString(outSystemPath);
}
