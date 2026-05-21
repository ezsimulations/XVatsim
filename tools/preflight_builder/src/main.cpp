#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "XVatsim/core/PreflightRouteCache.h"

namespace {

constexpr int kWindowWidth = 820;
constexpr int kWindowHeight = 590;
constexpr int kListId = 101;
constexpr int kRefreshId = 102;
constexpr int kPrepareId = 103;
constexpr int kStatusId = 104;
constexpr int kFolderId = 105;

struct PlanEntry {
    std::filesystem::path path;
    xvatsim::core::preflight::FmsPlan plan;
    bool valid = false;
    std::string message;
    std::string displayText;
};

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

HINSTANCE gInstance = nullptr;
HWND gMainWindow = nullptr;
HWND gListBox = nullptr;
HWND gRefreshButton = nullptr;
HWND gPrepareButton = nullptr;
HWND gStatusLabel = nullptr;
HWND gFolderLabel = nullptr;
HFONT gUiFont = nullptr;
HFONT gSmallFont = nullptr;
std::vector<PlanEntry> gPlans;
std::filesystem::path gExecutablePath;
std::filesystem::path gCachePath;

std::string FileTimeSummary(const std::filesystem::path& path) {
    const auto modified = xvatsim::core::preflight::GetFileModifiedUnixSeconds(path);
    if (modified <= 0) {
        return "modified time unavailable";
    }

    std::tm localTime = {};
    const std::time_t timeValue = static_cast<std::time_t>(modified);
    localtime_s(&localTime, &timeValue);
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &localTime);
    return buffer;
}

void SetStatus(const std::string& message) {
    if (gStatusLabel != nullptr) {
        SetWindowTextA(gStatusLabel, message.c_str());
    }
}

std::string BuildPlanDisplayText(const PlanEntry& entry) {
    std::ostringstream stream;
    stream << entry.path.filename().string() << "    ";
    if (!entry.valid) {
        stream << "Not ready - " << entry.message;
        return stream.str();
    }

    stream << entry.plan.departureIcao << " -> "
           << entry.plan.destinationIcao
           << "    Cycle " << entry.plan.cycle
           << "    " << entry.plan.waypoints.size() << " points"
           << "    " << FileTimeSummary(entry.path);
    return stream.str();
}

void RefreshPlanList() {
    gPlans.clear();
    SendMessageA(gListBox, LB_RESETCONTENT, 0, 0);
    EnableWindow(gPrepareButton, FALSE);

    const std::filesystem::path folder(
        xvatsim::core::preflight::kDefaultFmsPlansFolder);
    SetWindowTextA(gFolderLabel, ("Folder: " + folder.string()).c_str());

    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
        SetStatus(
            "No X-Plane FMS plans folder found. Export an X-Plane 11/12 plan from SimBrief Downloader, then refresh.");
        return;
    }

    std::vector<std::filesystem::path> fmsFiles;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        auto extension = entry.path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
        if (extension == ".fms") {
            fmsFiles.push_back(entry.path());
        }
    }

    std::sort(fmsFiles.begin(), fmsFiles.end());
    for (const auto& path : fmsFiles) {
        PlanEntry entry;
        entry.path = path;
        const auto parseResult = xvatsim::core::preflight::LoadFmsPlanFile(path);
        entry.valid = parseResult.ok;
        entry.plan = parseResult.plan;
        entry.message = parseResult.message;
        entry.displayText = BuildPlanDisplayText(entry);
        gPlans.push_back(entry);
        SendMessageA(
            gListBox,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(gPlans.back().displayText.c_str()));
    }

    if (gPlans.empty()) {
        SetStatus(
            "No X-Plane FMS plans found. Export an X-Plane 11/12 plan from SimBrief Downloader, then refresh.");
    } else {
        SetStatus("Select a valid flight plan, then press Prepare Selected Flight.");
    }
}

void PrepareSelectedPlan() {
    const auto selectedIndex = static_cast<int>(
        SendMessageA(gListBox, LB_GETCURSEL, 0, 0));
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(gPlans.size())) {
        SetStatus("Select a flight plan first.");
        return;
    }

    const auto& entry = gPlans[static_cast<std::size_t>(selectedIndex)];
    if (!entry.valid) {
        SetStatus("That FMS file is not ready: " + entry.message);
        return;
    }

    const auto cache =
        xvatsim::core::preflight::BuildPreflightRouteCache(entry.plan);
    std::string error;
    if (!xvatsim::core::preflight::WritePreflightRouteCacheFile(
            cache,
            gCachePath,
            &error)) {
        SetStatus(error);
        return;
    }

    std::ostringstream stream;
    stream << "Preflight route cache ready for "
           << entry.plan.departureIcao << " -> "
           << entry.plan.destinationIcao
           << ". You can now launch X-Plane and enjoy your flight.";
    SetStatus(stream.str());
}

void CreateFonts() {
    gUiFont = CreateFontA(
        -17,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI");
    gSmallFont = CreateFontA(
        -15,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI");
}

void ApplyFont(HWND window, HFONT font) {
    if (window != nullptr && font != nullptr) {
        SendMessageA(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void CreateControls(HWND hwnd) {
    CreateFonts();

    const DWORD staticStyle = WS_CHILD | WS_VISIBLE;
    CreateWindowExA(
        0,
        "STATIC",
        "Select the SimBrief/X-Plane flight plan you want XVatsim to prepare before launch.",
        staticStyle,
        32,
        104,
        744,
        24,
        hwnd,
        nullptr,
        gInstance,
        nullptr);

    HWND instructionTwo = CreateWindowExA(
        0,
        "STATIC",
        "Use SimBrief Downloader with the X-Plane 11/12 format, saved to C:\\X-Plane 12\\Output\\FMS plans.",
        staticStyle,
        32,
        132,
        744,
        24,
        hwnd,
        nullptr,
        gInstance,
        nullptr);

    gFolderLabel = CreateWindowExA(
        0,
        "STATIC",
        "",
        staticStyle,
        32,
        164,
        744,
        24,
        hwnd,
        ControlId(kFolderId),
        gInstance,
        nullptr);

    gListBox = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "LISTBOX",
        "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        32,
        198,
        744,
        210,
        hwnd,
        ControlId(kListId),
        gInstance,
        nullptr);

    gRefreshButton = CreateWindowExA(
        0,
        "BUTTON",
        "Refresh Plans",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        32,
        424,
        145,
        36,
        hwnd,
        ControlId(kRefreshId),
        gInstance,
        nullptr);

    gPrepareButton = CreateWindowExA(
        0,
        "BUTTON",
        "Prepare Selected Flight",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        192,
        424,
        210,
        36,
        hwnd,
        ControlId(kPrepareId),
        gInstance,
        nullptr);
    EnableWindow(gPrepareButton, FALSE);

    gStatusLabel = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        32,
        478,
        744,
        52,
        hwnd,
        ControlId(kStatusId),
        gInstance,
        nullptr);

    ApplyFont(instructionTwo, gSmallFont);
    ApplyFont(gFolderLabel, gSmallFont);
    ApplyFont(gListBox, gSmallFont);
    ApplyFont(gRefreshButton, gUiFont);
    ApplyFont(gPrepareButton, gUiFont);
    ApplyFont(gStatusLabel, gUiFont);

    RefreshPlanList();
}

void PaintHeader(HWND hwnd) {
    PAINTSTRUCT ps = {};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT clientRect = {};
    GetClientRect(hwnd, &clientRect);

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(244, 247, 249));
    FillRect(dc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    RECT headerRect = {0, 0, clientRect.right, 86};
    HBRUSH headerBrush = CreateSolidBrush(RGB(18, 33, 43));
    FillRect(dc, &headerRect, headerBrush);
    DeleteObject(headerBrush);

    RECT accentRect = {0, 82, clientRect.right, 86};
    HBRUSH accentBrush = CreateSolidBrush(RGB(36, 149, 194));
    FillRect(dc, &accentRect, accentBrush);
    DeleteObject(accentBrush);

    HFONT titleFont = CreateFontA(
        -30,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI");
    HFONT previousFont = static_cast<HFONT>(SelectObject(dc, titleFont));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245, 250, 252));
    RECT titleRect = {32, 16, 420, 52};
    DrawTextA(dc, "XVatsim", -1, &titleRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    SelectObject(dc, previousFont);
    DeleteObject(titleFont);

    HFONT subtitleFont = CreateFontA(
        -18,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI");
    previousFont = static_cast<HFONT>(SelectObject(dc, subtitleFont));
    SetTextColor(dc, RGB(174, 211, 226));
    RECT subtitleRect = {32, 50, 420, 76};
    DrawTextA(
        dc,
        "Preflight Builder",
        -1,
        &subtitleRect,
        DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    SelectObject(dc, previousFont);
    DeleteObject(subtitleFont);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            CreateControls(hwnd);
            return 0;
        case WM_PAINT:
            PaintHeader(hwnd);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kRefreshId) {
                RefreshPlanList();
                return 0;
            }
            if (LOWORD(wParam) == kPrepareId) {
                PrepareSelectedPlan();
                return 0;
            }
            if (LOWORD(wParam) == kListId && HIWORD(wParam) == LBN_SELCHANGE) {
                const auto selectedIndex = static_cast<int>(
                    SendMessageA(gListBox, LB_GETCURSEL, 0, 0));
                const auto validSelection =
                    selectedIndex >= 0 &&
                    selectedIndex < static_cast<int>(gPlans.size()) &&
                    gPlans[static_cast<std::size_t>(selectedIndex)].valid;
                EnableWindow(gPrepareButton, validSelection ? TRUE : FALSE);
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(25, 39, 51));
            static HBRUSH backgroundBrush = CreateSolidBrush(RGB(244, 247, 249));
            return reinterpret_cast<LRESULT>(backgroundBrush);
        }
        case WM_DESTROY:
            if (gUiFont != nullptr) {
                DeleteObject(gUiFont);
                gUiFont = nullptr;
            }
            if (gSmallFont != nullptr) {
                DeleteObject(gSmallFont);
                gSmallFont = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

std::filesystem::path GetCurrentExecutablePath() {
    char pathBuffer[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, pathBuffer, static_cast<DWORD>(sizeof(pathBuffer)));
    return pathBuffer;
}

}  // namespace

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previousInstance,
    LPSTR commandLine,
    int showCommand) {
    (void)previousInstance;
    (void)commandLine;

    gInstance = instance;
    gExecutablePath = GetCurrentExecutablePath();
    gCachePath =
        xvatsim::core::preflight::BuildCachePathBesideExecutable(gExecutablePath);

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "XVatsimPreflightBuilderWindow";
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(244, 247, 249));

    RegisterClassA(&windowClass);

    gMainWindow = CreateWindowExA(
        0,
        windowClass.lpszClassName,
        "XVatsim Preflight Builder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (gMainWindow == nullptr) {
        return 1;
    }

    ShowWindow(gMainWindow, showCommand);
    UpdateWindow(gMainWindow);

    MSG message = {};
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    return static_cast<int>(message.wParam);
}
