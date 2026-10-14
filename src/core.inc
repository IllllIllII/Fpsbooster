// =====================================================================
//  Frame Booster v3.1.2 - 공통 (헤더, 상수, 디자인 토큰, 전역, 설정)
// =====================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dwmapi.h>
#include <powrprof.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <dxgi.h>
#include <objbase.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <deque>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <cstdint>
#include "resource.h"

// ---------------------------------------------------------------------
//  상수
// ---------------------------------------------------------------------
static const wchar_t* APP_NAME  = L"Frame Booster";
static const wchar_t* APP_VER   = L"v3.1.2";
static const wchar_t* WND_CLASS = L"FrameBoosterMainWnd";
static const wchar_t* OVL_CLASS = L"FrameBoosterOverlay";
static const wchar_t* RUN_KEY   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUN_VALUE = L"FrameBooster";
static const wchar_t* TASK_NAME = L"FrameBooster";

static const float BASE_W = 1180.f, BASE_H = 720.f;

enum { L_INFO = 0, L_OK = 1, L_WARN = 2, L_ERR = 3 };
enum { PAGE_DASH = 0, PAGE_BOOST, PAGE_ANALYSIS, PAGE_SETTINGS, PAGE_COUNT };
enum { OVL_SIMPLE = 0, OVL_BASIC, OVL_DETAIL };
enum { PWR_BALANCED = 0, PWR_HIGH, PWR_ULTIMATE };
enum { POS_TL = 0, POS_TC, POS_TR, POS_ML, POS_MC, POS_MR, POS_BL, POS_BC, POS_BR, POS_CUSTOM };

#define WM_APP_LOG       (WM_APP + 1)
#define WM_APP_DONE      (WM_APP + 2)
#define WM_APP_TRAY      (WM_APP + 3)
#define WM_APP_SHOW      (WM_APP + 4)
#define WM_APP_UPD_CHECK (WM_APP + 5)
#define WM_APP_UPD_PROG  (WM_APP + 6)
#define WM_APP_UPD_DONE  (WM_APP + 7)
#define WM_APP_PM_DONE   (WM_APP + 8)    // PresentMon 측정 종료 (세션 저장됨)
#define WM_APP_TOOL      (WM_APP + 9)    // 측정 도구 다운로드 결과

#define TIMER_WATCH   1   // 게임 종료 감시 (부스트 중)
#define TIMER_UPDATE  2   // 업데이트 확인
#define TIMER_MON     3   // 측정 대상 감지 (1초)
#define TIMER_UI      4   // 화면 갱신 (실시간 타일, 0.5초)
#define TIMER_OVL     5   // 오버레이 갱신

enum { IDC_EDIT = 101, IDM_OPEN = 200, IDM_BOOST, IDM_RESTORE, IDM_OVERLAY, IDM_EXIT };

// ---------------------------------------------------------------------
//  디자인 토큰 (레이저 코어텍스 느낌: 검정 + 네온 그린)
// ---------------------------------------------------------------------
static const UINT32 C_BG      = 0x0B0B0B;
static const UINT32 C_SIDE    = 0x0F0F0F;
static const UINT32 C_CARD    = 0x151515;
static const UINT32 C_CARD2   = 0x1C1C1C;
static const UINT32 C_CARD3   = 0x262626;
static const UINT32 C_LINE    = 0x242424;
static const UINT32 C_TEXT    = 0xF2F2F2;
static const UINT32 C_DIM     = 0xA6A6A6;
static const UINT32 C_FAINT   = 0x707070;
static const UINT32 C_GREEN   = 0x44D62C;
static const UINT32 C_GREENHI = 0x6CEB55;
static const UINT32 C_GREENDK = 0x1E6E13;
static const UINT32 C_GREENSF = 0x13240F;
static const UINT32 C_INK     = 0x061404;
static const UINT32 C_WARN    = 0xFFB547;
static const UINT32 C_ERR     = 0xFF5A5A;
static const UINT32 C_BLUE    = 0x4CB8FF;

static const wchar_t* FONT_UI = L"Malgun Gothic";   // 테스트용 FB_FONT 로 바꿀 수 있음
static const wchar_t* FONT_NUM = L"Bahnschrift";

// ---------------------------------------------------------------------
//  기본 목록
// ---------------------------------------------------------------------
static const wchar_t* kSystemNames[] = {
    L"explorer", L"dwm", L"csrss", L"winlogon", L"sihost", L"ctfmon", L"ShellExperienceHost",
    L"StartMenuExperienceHost", L"SearchHost", L"SearchApp", L"RuntimeBroker", L"TextInputHost",
    L"fontdrvhost", L"svchost", L"taskhostw", L"SecurityHealthSystray", L"SecurityHealthService",
    L"audiodg", L"conhost", L"ApplicationFrameHost", L"LockApp", L"smartscreen", L"dllhost",
    L"WmiPrvSE", L"powershell", L"pwsh", L"WindowsTerminal", L"OpenConsole", L"Widgets",
    L"WidgetService", L"PhoneExperienceHost", L"CrossDeviceService", L"MsMpEng", L"NisSrv",
    L"Taskmgr", L"backgroundTaskHost", L"UserOOBEBroker", L"LogonUI", L"lsass", L"services",
    L"wininit", L"smss", L"SgrmBroker", L"unsecapp", L"MpDefenderCoreService", L"FrameBooster",
    L"PresentMon", L"PresentMon-2.6.0-x64"
};

static const wchar_t* kDefaultKeep[] = {
    L"Discord",
    L"steam", L"steamwebhelper", L"steamservice", L"GameOverlayUI",
    L"Battle.net", L"Agent", L"BlizzardError",
    L"EpicGamesLauncher", L"EpicWebHelper", L"RiotClientServices", L"RiotClientUx",
    L"RiotClientUxRender", L"vgtray", L"EADesktop", L"EABackgroundService", L"upc", L"UplayWebCore",
    L"BEService", L"EasyAntiCheat", L"EasyAntiCheat_EOS",
    L"nvcontainer", L"NVDisplay.Container", L"NVIDIA Share", L"NVIDIA Overlay", L"nvsphelper64",
    L"RtkAudUService64", L"lghub", L"lghub_agent", L"lghub_updater", L"RazerAppEngine",
    L"Razer Synapse 3", L"RTSS", L"MSIAfterburner", L"Claude"
};

static const wchar_t* kDefaultGames[] = {
    L"Overwatch", L"TslGame", L"VALORANT-Win64-Shipping", L"League of Legends", L"cs2",
    L"FortniteClient-Win64-Shipping", L"r5apex", L"LostArk", L"MapleStory"
};

// ---------------------------------------------------------------------
//  자료형
// ---------------------------------------------------------------------
struct AppInfo {
    std::wstring name, display, path, args;
    int count = 0;
    unsigned long long memMB = 0;
    bool hasWindow = false;
};
struct SavedApp { std::wstring name, display, path, args; };
struct ProcEntry { DWORD pid, ppid; std::wstring name; };
struct LogLine { int level; std::wstring text; };
struct Hot { float x, y, w, h; int id; };

struct Settings {
    std::vector<std::wstring> keep, games;
    bool autoRestore = true;
    bool forceKill = true;
    bool memClean = true;
    bool confirmBoost = true;
    int  powerMode = -1;          // -1 = PC에 맞게 자동 (X3D: 균형 / 그 외: 고성능)
    bool monitor = true;          // 게임 프레임 자동 측정
    bool overlay = false;
    int  overlayMode = OVL_BASIC;
    int  overlayPos = POS_TR;
    float overlayX = 0.f, overlayY = 0.f;   // POS_CUSTOM: 모니터 작업 영역 기준 비율
};

// ---------------------------------------------------------------------
//  전역
// ---------------------------------------------------------------------
static HINSTANCE g_inst;
static HWND g_hwnd, g_edit, g_ovl;
static HFONT g_editFont;
static HBRUSH g_brCard2;
static UINT g_dpi = 96;
static float g_scale = 1.f;
static HANDLE g_mutex;

static Settings g_set;
static std::vector<AppInfo> g_apps;
static std::vector<LogLine> g_logs;
static std::vector<Hot> g_hots;

static int  g_page = PAGE_DASH;
static int  g_hover = 0, g_down = 0;
static bool g_tracking = false;
static bool g_editFocus = false;
static float g_mouseX = -1, g_mouseY = -1;   // 기준 좌표

static bool g_busy = false;
static bool g_reallyExit = false;
static bool g_tipShown = false;
static bool g_startupOn = false;
static bool g_gameSeen = false;
static int  g_missCount = 0;
static size_t g_pending = 0;
static bool g_active = false;      // 부스트 중
static std::wstring g_gameName;
static bool g_isAdmin = false;

static std::wstring g_dataDir, g_exePath, g_settingsFile, g_stateFile, g_logFile, g_powerFile, g_boostFile, g_backupFile, g_sessionDir, g_toolDir;
static NOTIFYICONDATAW g_nid;
static UINT g_msgTaskbarCreated;

static void Redraw() { if (g_hwnd && IsWindowVisible(g_hwnd)) InvalidateRect(g_hwnd, nullptr, FALSE); }
static int S(float v) { return (int)(v * g_scale + 0.5f); }
