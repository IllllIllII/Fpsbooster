// =====================================================================
//  Frame Booster v3.0  (C++ / Win32 + Direct2D, 단일 exe)
//  게임 전 백그라운드 앱 정리 · 프레임 측정 · 오버레이 · 부스트 전후 비교
// =====================================================================
#include "core.inc"
#include "util.inc"
#include "sys.inc"
#include "updater.inc"
#include "monitor.inc"
#include "render.inc"
#include "tray.inc"
#include "overlay.inc"
#include "ui_main.inc"
#include "ui_analysis.inc"
#include "ui_settings.inc"

// =====================================================================
//  동작
// =====================================================================
static void UpdateEditVisibility() {
    bool show = g_page == PAGE_SETTINGS && g_modal == MODAL_NONE && IsWindowVisible(g_hwnd);
    ShowWindow(g_edit, show ? SW_SHOW : SW_HIDE);
}

static void RefreshList() { g_apps = GetAppList(); Redraw(); }

static void SaveGamesFromEdit() {
    int len = GetWindowTextLengthW(g_edit);
    std::wstring t(len + 1, L'\0');
    GetWindowTextW(g_edit, &t[0], len + 1);
    t.resize(len);
    std::vector<std::wstring> games;
    size_t st = 0;
    while (st <= t.size()) {
        size_t e = t.find(L',', st);
        if (e == std::wstring::npos) e = t.size();
        std::wstring gname = StripExe(Trim(t.substr(st, e - st)));
        if (!gname.empty() && !ContainsI(games, gname)) games.push_back(gname);
        st = e + 1;
    }
    if (games != g_set.games) {
        g_set.games = games;
        SaveSettings();
        AddLog(L_INFO, L"감시 게임 저장: " + std::to_wstring(games.size()) + L"개");
    }
}

static void FillGamesEdit() {
    std::wstring t;
    for (size_t i = 0; i < g_set.games.size(); i++) { if (i) t += L", "; t += g_set.games[i]; }
    SetWindowTextW(g_edit, t.c_str());
}

static void SetPage(int p) {
    if (g_page == PAGE_SETTINGS && p != PAGE_SETTINGS) SaveGamesFromEdit();
    g_page = p;
    if ((p == PAGE_DASH || p == PAGE_BOOST) && g_apps.empty()) g_apps = GetAppList();
    if (p == PAGE_BOOST) RefreshTweakState();
    if (p == PAGE_ANALYSIS && (g_selSess < 0 || g_selSess >= (int)g_sessions.size())) SelectSession(g_sessions.empty() ? -1 : 0);
    if (p == PAGE_DASH) RefreshPowerState();
    UpdateEditVisibility();
    Redraw();
}

static void SetModal(int m) { g_modal = m; UpdateEditVisibility(); Redraw(); }

static void SetBusy(bool b) { g_busy = b; Redraw(); }

static void StartWatch() {
    g_gameSeen = false; g_missCount = 0;
    SetTimer(g_hwnd, TIMER_WATCH, 3000, nullptr);
}

static void StartRestore(bool fromAuto) {
    if (g_busy) return;
    SetBusy(true);
    HANDLE th = CreateThread(nullptr, 0, RestoreThread, (LPVOID)(INT_PTR)(fromAuto ? 3 : 2), 0, nullptr);
    if (th) CloseHandle(th); else SetBusy(false);
}

static void RunBoost(const std::vector<AppInfo>& targets) {
    if (g_busy) return;
    SetBusy(true);
    BoostJob* job = new BoostJob{ targets, g_set.forceKill, g_set.autoRestore, EffectivePowerMode(), g_set.memClean, g_set.games };
    HANDLE th = CreateThread(nullptr, 0, BoostThread, job, 0, nullptr);
    if (th) CloseHandle(th); else { delete job; SetBusy(false); }
}

static void BeginBoost() {
    if (g_busy) return;
    bool visible = IsWindowVisible(g_hwnd) != FALSE;
    if (visible && g_page == PAGE_SETTINGS) SaveGamesFromEdit();
    std::vector<AppInfo> targets;
    for (auto& a : (visible && !g_apps.empty() ? g_apps : GetAppList())) if (IsChecked(a)) targets.push_back(a);
    if (visible && g_set.confirmBoost && !targets.empty()) {
        g_confirmTargets = targets;
        g_confirmNoAsk = false;
        SetModal(MODAL_CONFIRM);
        return;
    }
    if (!visible && g_set.confirmBoost && !targets.empty()) {
        std::wstring msg = L"앱 " + std::to_wstring(targets.size()) + L"개를 정리하고 부스트할까요?\n\n";
        size_t show = std::min<size_t>(targets.size(), 12);
        for (size_t i = 0; i < show; i++) msg += L"  • " + targets[i].display + L"\n";
        if (targets.size() > show) msg += L"  … 외 " + std::to_wstring(targets.size() - show) + L"개\n";
        if (MessageBoxW(nullptr, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST) != IDYES) return;
    }
    RunBoost(targets);
}

static void StartTweakJob(const std::vector<std::wstring>& apply, const std::vector<std::wstring>& revert) {
    if (g_busy || (apply.empty() && revert.empty())) return;
    TweakJob* job = new TweakJob{ apply, revert };
    SetBusy(true);
    HANDLE th = CreateThread(nullptr, 0, TweakThread, job, 0, nullptr);
    if (th) CloseHandle(th); else { delete job; SetBusy(false); }
}

static void ToggleKeep(size_t idx) {
    if (idx >= g_apps.size()) return;
    const std::wstring name = g_apps[idx].name;
    auto& k = g_set.keep;
    bool wasChecked = IsChecked(g_apps[idx]);
    k.erase(std::remove_if(k.begin(), k.end(), [&](const std::wstring& x) { return IEq(x, name); }), k.end());
    if (wasChecked) k.push_back(name);
    SaveSettings();
    Redraw();
}

static void SetPowerMode(int m) {
    m = std::max(0, std::min(2, m));
    g_set.powerMode = m;
    SaveSettings();
    if (g_active && !g_busy) BoostPower(m, [](int l, const std::wstring& s) { AddLog(l, s); });   // 부스트 중이면 바로 반영
    RefreshPowerState();
    Redraw();
}

static void SetOverlayEnabled(bool on) {
    g_set.overlay = on;
    SaveSettings();
    if (on) SetTimer(g_hwnd, TIMER_OVL, 500, nullptr);
    else if (!g_ovlMoving) { KillTimer(g_hwnd, TIMER_OVL); OverlayRender(false); }
    Redraw();
}

static void HideToTray() {
    if (g_page == PAGE_SETTINGS) SaveGamesFromEdit();
    if (g_modal == MODAL_CONFIRM || g_modal == MODAL_LOG) g_modal = MODAL_NONE;
    ShowWindow(g_hwnd, SW_HIDE);
    std::vector<AppInfo>().swap(g_apps);
    std::vector<float>().swap(g_selFt); std::vector<double>().swap(g_selCum);
    std::vector<float>().swap(g_cmpFt); std::vector<double>().swap(g_cmpCum);
    std::vector<float>().swap(g_selLat); std::vector<float>().swap(g_cmpLat);
    g_selSess = g_cmpSess = -1;
    KillTimer(g_hwnd, TIMER_UI);
    DiscardMainTarget();
    if (!g_tipShown) { Balloon(L"트레이에서 계속 실행 중이에요. 우클릭으로 메뉴를 열 수 있어요."); g_tipShown = true; }
    TrimSelf();
}

static void ShowMain() {
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hwnd);
    ReloadPending();
    RefreshPowerState();
    g_apps = GetAppList();
    if (g_page == PAGE_ANALYSIS) SelectSession(g_sessions.empty() ? -1 : 0);
    if (g_page == PAGE_BOOST) RefreshTweakState();
    UpdateEditVisibility();
    SetTimer(g_hwnd, TIMER_UI, 500, nullptr);
    Redraw();
}

static void OnWatchTimer() {
    if (g_busy) return;
    ReloadPending();
    if (!g_active) { KillTimer(g_hwnd, TIMER_WATCH); TrayTip(APP_NAME); Redraw(); return; }
    if (!g_set.autoRestore || g_set.games.empty()) return;
    std::wstring found;
    for (auto& p : Snapshot()) if (ContainsI(g_set.games, p.name)) { found = p.name; break; }
    if (!found.empty()) {
        g_missCount = 0;
        if (!g_gameSeen) {
            g_gameSeen = true;
            g_gameName = found;
            AddLog(L_OK, L"게임 감지: " + g_gameName);
            TrayTip(std::wstring(APP_NAME) + L" - " + g_gameName + L" 부스트 중");
        }
    } else if (g_gameSeen && ++g_missCount >= 3) {   // 9초 동안 안 보이면 게임 종료
        g_gameSeen = false; g_missCount = 0;
        AddLog(L_INFO, L"게임 종료: " + g_gameName + L" → 원래대로 되돌림");
        StartRestore(true);
    }
}

// ---------------------------------------------------------------------
//  업데이트
// ---------------------------------------------------------------------
static bool g_updManual = false;

static void StartUpdateCheck(bool manual) {
    if (g_updState == UPD_CHECKING || g_updState == UPD_DOWNLOADING || g_updState == UPD_APPLYING) return;
    g_updManual = manual;
    g_updState = UPD_CHECKING;
    Redraw();
    HANDLE th = CreateThread(nullptr, 0, UpdateCheckThread, nullptr, 0, nullptr);
    if (th) CloseHandle(th); else g_updState = UPD_FAILED;
}

static void OpenUpdateModal() { g_updError.clear(); SetModal(MODAL_UPDATE); }

// ---------------------------------------------------------------------
//  클릭
// ---------------------------------------------------------------------
static void OnClick(int id) {
    if (g_modal == MODAL_CONFIRM) {
        if (id == H_CONF_NOASK) { g_confirmNoAsk = !g_confirmNoAsk; Redraw(); }
        else if (id == H_CONF_OK) {
            if (g_confirmNoAsk) { g_set.confirmBoost = false; SaveSettings(); }
            auto t = g_confirmTargets; g_confirmTargets.clear();
            SetModal(MODAL_NONE);
            RunBoost(t);
        } else if (id == H_CONF_CANCEL || id == H_MODAL_BG) { g_confirmTargets.clear(); SetModal(MODAL_NONE); }
        return;
    }
    if (g_modal == MODAL_UPDATE) {
        bool busy = g_updState == UPD_DOWNLOADING || g_updState == UPD_APPLYING;
        if ((id == H_UPD_LATER || id == H_MODAL_BG) && !busy) SetModal(MODAL_NONE);
        else if (id == H_UPD_OK && !g_busy && g_updState == UPD_AVAILABLE) {
            g_updState = UPD_DOWNLOADING; g_updProgress = 0; g_updError.clear();
            AddLog(L_INFO, L"업데이트 받는 중: " + g_updVer);
            HANDLE th = CreateThread(nullptr, 0, UpdateDownloadThread, nullptr, 0, nullptr);
            if (th) CloseHandle(th); else g_updState = UPD_AVAILABLE;
            Redraw();
        }
        return;
    }
    if (g_modal == MODAL_LOG) {
        if (id == H_LOG_CLOSE || id == H_MODAL_BG) SetModal(MODAL_NONE);
        else if (id == H_LOG_CLEAR) { g_logs.clear(); g_logScroll = 0; Redraw(); }
        return;
    }
    if (id >= H_CMP && id < H_CMP + 1000) { SetCompare(id - H_CMP); Redraw(); return; }
    if (id >= H_SESS && id < H_SESS + 1000) { SelectSession(id - H_SESS); Redraw(); return; }
    if (id >= H_ROW && id < H_ROW + 2000) { ToggleKeep((size_t)(id - H_ROW)); return; }
    if (id >= H_POS0 && id <= H_POS0 + 8) {
        g_set.overlayPos = id - H_POS0; SaveSettings();
        if (g_ovlMoving) SetOverlayMoving(false);
        OverlayTick(); Redraw(); return;
    }
    switch (id) {
    case H_NAV0: case H_NAV1: case H_NAV2: case H_NAV3: SetPage(id - H_NAV0); break;
    case H_BOOSTBTN: if (g_active) StartRestore(false); else BeginBoost(); break;
    case H_BOOST_OFF: StartRestore(false); break;
    case H_ROW_APPS: SetPage(PAGE_BOOST); break;
    case H_ROW_POWER: SetPowerMode((EffectivePowerMode() + 1) % 3); break;
    case H_ROW_MEM: case H_OPT_MEM: g_set.memClean = !g_set.memClean; SaveSettings(); Redraw(); break;
    case H_LIVE_ACTION:
        if (!g_set.monitor) { g_set.monitor = true; SaveSettings(); if (g_isAdmin && g_toolState != TOOL_READY) StartToolDownload(); Redraw(); }
        else if (!g_isAdmin) { if (RestartAsAdmin()) { g_reallyExit = true; DestroyWindow(g_hwnd); } }
        else StartToolDownload();
        Redraw();
        break;
    case H_POWER_0: case H_POWER_1: case H_POWER_2: SetPowerMode(id - H_POWER_0); break;
    case H_OVL_TOGGLE: case H_SET_OVERLAY: SetOverlayEnabled(!g_set.overlay); break;
    case H_OVL_MODE0: case H_OVL_MODE1: case H_OVL_MODE2:
        g_set.overlayMode = id - H_OVL_MODE0; SaveSettings(); OverlayTick(); Redraw(); break;
    case H_SET_MODE0: case H_SET_MODE1: case H_SET_MODE2:
        g_set.overlayMode = id - H_SET_MODE0; SaveSettings(); OverlayTick(); Redraw(); break;
    case H_OVL_POSLINK: SetPage(PAGE_SETTINGS); break;
    case H_OVL_MOVE:
        SetOverlayMoving(!g_ovlMoving);
        if (g_ovlMoving) SetTimer(g_hwnd, TIMER_OVL, 500, nullptr);
        else if (!g_set.overlay) { KillTimer(g_hwnd, TIMER_OVL); OverlayRender(false); }
        Redraw();
        break;
    case H_REFRESH: RefreshList(); AddLog(L_INFO, L"목록 새로고침: 앱 " + std::to_wstring(g_apps.size()) + L"개"); break;
    case H_OPT_DVR:
        if (g_twDvr) StartTweakJob({}, { L"gamedvr" }); else StartTweakJob({ L"gamedvr" }, {});
        break;
    case H_OPT_GAMEMODE:
        if (SetGameMode(!g_twGameMode)) AddLog(L_OK, !g_twGameMode ? L"게임 모드 켬 (윈도우 설정과 같음)" : L"게임 모드 끔 (윈도우 설정과 같음)");
        else AddLog(L_ERR, L"게임 모드를 바꾸지 못했어요");
        RefreshTweakState(); Redraw();
        break;
    case H_OPT_POWERLINK: SetPage(PAGE_DASH); break;
    case H_LEGACY_REVERT: {
        if (!g_legacyIds.empty()) StartTweakJob({}, g_legacyIds);
        break;
    }
    case H_GRAPH_FT: g_graphMode = 0; Redraw(); break;
    case H_GRAPH_FPS: g_graphMode = 1; Redraw(); break;
    case H_GRAPH_LAT: g_graphMode = 2; Redraw(); break;
    case H_SESS_DEL:
        if (g_selSess >= 0 && g_selSess < (int)g_sessions.size()) {
            if (g_delArm == g_selSess) {
                DeleteFileW(g_sessions[g_selSess].file.c_str());
                AddLog(L_INFO, L"기록 삭제: " + g_sessions[g_selSess].exe);
                g_delArm = -1; g_cmpSess = -1; g_cmpFt.clear(); g_cmpLat.clear(); g_cmpCum.clear();
                LoadSessionIndex();
                SelectSession(g_sessions.empty() ? -1 : 0);
            } else g_delArm = g_selSess;
        }
        Redraw();
        break;
    case H_SET_STARTUP:
        SetStartup(!g_startupOn);
        g_startupOn = IsStartupOn();
        AddLog(L_INFO, g_startupOn ? L"윈도우 시작 시 실행: 켬" : L"윈도우 시작 시 실행: 끔");
        Redraw();
        break;
    case H_SET_AUTORESTORE: g_set.autoRestore = !g_set.autoRestore; SaveSettings(); if (g_set.autoRestore && g_active) StartWatch(); Redraw(); break;
    case H_SET_FORCE: g_set.forceKill = !g_set.forceKill; SaveSettings(); Redraw(); break;
    case H_SET_CONFIRM: g_set.confirmBoost = !g_set.confirmBoost; SaveSettings(); Redraw(); break;
    case H_SET_MONITOR:
        g_set.monitor = !g_set.monitor; SaveSettings();
        if (!g_set.monitor) StopMonitor();
        else if (g_isAdmin && g_toolState != TOOL_READY) StartToolDownload();
        Redraw();
        break;
    case H_ADMIN: if (RestartAsAdmin()) { g_reallyExit = true; DestroyWindow(g_hwnd); } break;
    case H_TOOL: StartToolDownload(); Redraw(); break;
    case H_UPD_CHECK: if (g_updState == UPD_AVAILABLE) OpenUpdateModal(); else StartUpdateCheck(true); break;
    case H_UPD_BADGE: OpenUpdateModal(); break;
    case H_OPEN_FOLDER: ShellExecuteW(g_hwnd, L"open", g_dataDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case H_OPEN_LOG: g_logScroll = 0; SetModal(MODAL_LOG); break;
    }
}

static void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_OPEN, L"열기");
    AppendMenuW(m, MF_STRING | (g_busy ? MF_GRAYED : 0), IDM_BOOST, g_active ? L"부스트 해제" : L"부스트 시작");
    AppendMenuW(m, MF_STRING | (g_set.overlay ? MF_CHECKED : 0), IDM_OVERLAY, L"게임 오버레이");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"완전히 종료");
    SetMenuDefaultItem(m, IDM_OPEN, FALSE);
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// ---------------------------------------------------------------------
//  그리기 / 배치
// ---------------------------------------------------------------------
static void OnPaint(HWND h) {
    PAINTSTRUCT ps;
    BeginPaint(h, &ps);
    EndPaint(h, &ps);
    if (!EnsureMainTarget()) return;
    R = g_rt; RB = g_mainBrush;
    g_rt->BeginDraw();
    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    g_rt->Clear(Col(C_BG));
    g_hots.clear();
    DrawSidebar();
    if (g_page == PAGE_DASH) DrawDashboard();
    else if (g_page == PAGE_BOOST) DrawBoostPage();
    else if (g_page == PAGE_ANALYSIS) DrawAnalysis();
    else DrawSettings();
    if (g_updState == UPD_AVAILABLE || g_updState == UPD_DOWNLOADING) {   // 사이드바 업데이트 배지
        std::wstring t = g_updState == UPD_DOWNLOADING ? L"받는 중 " + std::to_wstring(g_updProgress) + L"%" : L"업데이트 " + g_updVer;
        float w = ChipW(t, 10.5f);
        bool hv = g_hover == H_UPD_BADGE;
        FillRR(208 - w, 681, w, 22, 11, hv ? C_GREEN : C_GREENSF);
        Text(t, 208 - w, 681, w, 22, 10.5f, 700, hv ? C_INK : C_GREEN, 1);
        AddHot(208 - w, 681, w, 22, H_UPD_BADGE);
    }
    if (g_modal == MODAL_CONFIRM) DrawConfirmModal();
    else if (g_modal == MODAL_UPDATE) DrawUpdateModal();
    else if (g_modal == MODAL_LOG) DrawLogModal();
    if (g_rt->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) DiscardMainTarget();
}

static void Layout() {
    if (g_editFont) DeleteObject(g_editFont);
    LOGFONTW lf; ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -S(14);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy(lf.lfFaceName, FONT_UI);
    g_editFont = CreateFontIndirectW(&lf);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    MoveWindow(g_edit, S(278), S(390), S(382), S(22), TRUE);
    if (g_rt) g_rt->SetDpi(96.f * g_scale, 96.f * g_scale);
    Redraw();
}

// 화면 크기에 맞춰 배율: 창 높이가 작업 영역의 약 68% (QHD·4K에서 글씨가 작지 않게)
static void ComputeScale(HMONITOR mon) {
    MONITORINFO mi; mi.cbSize = sizeof(mi); GetMonitorInfoW(mon, &mi);
    float workH = (float)(mi.rcWork.bottom - mi.rcWork.top), workW = (float)(mi.rcWork.right - mi.rcWork.left);
    float dpiScale = g_dpi / 96.f;
    float fit = std::min(workH * 0.68f / BASE_H, workW * 0.68f / BASE_W);
    float maxFit = std::min((workH - 50.f) / BASE_H, (workW - 30.f) / BASE_W);
    g_scale = std::min(std::max(dpiScale, fit), maxFit);
    if (g_scale < 0.5f) g_scale = dpiScale;
}

static void ResizeToDpi(const RECT* suggested) {
    DWORD style = (DWORD)GetWindowLongW(g_hwnd, GWL_STYLE);
    ComputeScale(suggested ? MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST) : MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY));
    RECT r = { 0, 0, S(BASE_W), S(BASE_H) };
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, g_dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    if (suggested) SetWindowPos(g_hwnd, nullptr, suggested->left, suggested->top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    else {
        MONITORINFO mi; mi.cbSize = sizeof(mi); GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
        SetWindowPos(g_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// ---------------------------------------------------------------------
//  창 프로시저
// ---------------------------------------------------------------------
static void MouseDown(HWND h, LPARAM lp, bool dbl) {
    SetFocus(h);
    float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
    int id = HitTest(x, y);
    if (dbl && id == H_GRAPH) { GraphReset(); Redraw(); return; }
    g_down = id;
    if (id == H_POWER_SLIDER) { g_dragPower = true; g_dragPowerX = x; }
    if (id == H_GRAPH) { g_graphDrag = true; g_dragX0 = x; g_dragA = g_viewA; g_dragB = g_viewB; }
    SetCapture(h);
    Redraw();
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated) { TrayAdd(); return 0; }
    switch (msg) {
    case WM_CREATE:
        g_hwnd = h;
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_EDIT, g_inst, nullptr);
        return 0;
    case WM_DPICHANGED:
        g_dpi = HIWORD(wp);
        ResizeToDpi((RECT*)lp);
        Layout();
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) { HideToTray(); return 0; }
        if (g_rt) g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGB(0xF2, 0xF2, 0xF2));
        SetBkColor((HDC)wp, RGB(0x1C, 0x1C, 0x1C));
        return (LRESULT)g_brCard2;

    case WM_MOUSEMOVE: {
        if (!g_tracking) { TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t); g_tracking = true; }
        float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
        bool inGraph = g_page == PAGE_ANALYSIS && x >= GP_X && x < GP_X + GP_W && y >= GP_Y && y < GP_Y + GP_H;
        bool wasGraph = g_page == PAGE_ANALYSIS && g_mouseX >= GP_X && g_mouseX < GP_X + GP_W && g_mouseY >= GP_Y && g_mouseY < GP_Y + GP_H;
        g_mouseX = x; g_mouseY = y;
        if (g_dragPower) { g_dragPowerX = x; Redraw(); return 0; }
        if (g_graphDrag) { GraphPan(x - g_dragX0); Redraw(); return 0; }
        int id = HitTest(x, y);
        if (id != g_hover || inGraph || wasGraph) { g_hover = id; Redraw(); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        g_mouseX = g_mouseY = -1;
        if (g_hover) { g_hover = 0; }
        Redraw();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && (HWND)wp == h && g_hover && g_hover != H_MODAL_BG) {
            SetCursor(LoadCursorW(nullptr, g_hover == H_GRAPH ? IDC_SIZEWE : IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: MouseDown(h, lp, false); return 0;
    case WM_LBUTTONDBLCLK: MouseDown(h, lp, true); return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
        int down = g_down; g_down = 0;
        if (g_dragPower) {
            g_dragPower = false;
            int best = 0;
            for (int i = 1; i < 3; i++) if (fabsf(PowerStopX(i) - x) < fabsf(PowerStopX(best) - x)) best = i;
            if (best != EffectivePowerMode() || g_set.powerMode < 0) SetPowerMode(best); else Redraw();
            return 0;
        }
        if (g_graphDrag) { g_graphDrag = false; Redraw(); return 0; }
        int id = HitTest(x, y);
        if (id && id == down) OnClick(id); else Redraw();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        float x = pt.x / g_scale, y = pt.y / g_scale;
        float d = -(float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        if (g_modal == MODAL_LOG) { g_logScroll = std::max(0.f, g_logScroll - d * 72); Redraw(); return 0; }
        if (g_modal != MODAL_NONE) return 0;
        if (g_page == PAGE_BOOST && x >= AL_X && x < AL_X + AL_W && y >= AL_Y && y < AL_Y + AL_H) {
            g_listScroll = std::max(0.f, std::min(g_listScroll + d * AL_PITCH * 2, AppMaxScroll())); g_hover = 0; Redraw();
        } else if (g_page == PAGE_ANALYSIS && x >= SL_X && x < SL_X + SL_W && y >= SL_Y && y < SL_Y + SL_H) {
            g_sessScroll = std::max(0.f, g_sessScroll + d * SL_PITCH * 2); g_hover = 0; Redraw();
        } else if (g_page == PAGE_ANALYSIS && x >= GP_X && x < GP_X + GP_W && y >= GP_Y && y < GP_Y + GP_H) {
            GraphZoom(x, d < 0 ? 0.75f : 1.333f); Redraw();
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_modal == MODAL_CONFIRM || g_modal == MODAL_LOG) SetModal(MODAL_NONE);
            else if (g_modal == MODAL_UPDATE && g_updState != UPD_DOWNLOADING && g_updState != UPD_APPLYING) SetModal(MODAL_NONE);
            else if (g_ovlMoving) { SetOverlayMoving(false); Redraw(); }
        } else if (wp == VK_RETURN && g_modal == MODAL_CONFIRM) OnClick(H_CONF_OK);
        else if (wp == VK_F5) OnClick(H_REFRESH);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_EDIT) {
            if (code == EN_SETFOCUS) { g_editFocus = true; Redraw(); }
            if (code == EN_KILLFOCUS) { g_editFocus = false; SaveGamesFromEdit(); Redraw(); }
        }
        else if (id == IDM_OPEN) ShowMain();
        else if (id == IDM_BOOST) { if (g_active) StartRestore(false); else BeginBoost(); }
        else if (id == IDM_OVERLAY) SetOverlayEnabled(!g_set.overlay);
        else if (id == IDM_EXIT) { g_reallyExit = true; SendMessageW(h, WM_CLOSE, 0, 0); }
        return 0;
    }

    case WM_APP_LOG: { std::wstring* s = (std::wstring*)lp; AddLog((int)wp, *s); delete s; return 0; }
    case WM_APP_DONE: {
        g_busy = false;
        ReloadPending();
        RefreshPowerState();
        bool visible = IsWindowVisible(h) != FALSE;
        if (wp == 4) { RefreshTweakState(); Redraw(); return 0; }
        if (wp == 1) {
            if (g_active) StartWatch();
            TrayTip(std::wstring(APP_NAME) + L" - 부스트 중");
            if (visible) g_apps = GetAppList();
            else { Balloon(L"부스트 켜짐 · 앱 " + std::to_wstring((int)lp) + L"개 정리. 게임이 끝나면 되돌릴게요."); TrimSelf(); }
        } else {
            if (wp == 3) Balloon(L"게임이 끝나서 원래대로 되돌렸어요.");
            g_gameSeen = false;
            if (!g_active) { KillTimer(h, TIMER_WATCH); TrayTip(APP_NAME); }
            if (visible) g_apps = GetAppList(); else TrimSelf();
        }
        Redraw();
        return 0;
    }
    case WM_APP_PM_DONE: {
        PmResult* r = (PmResult*)lp;
        if (!r) return 0;
        if (r->saved) { ReloadSessions(false); AddLog(L_OK, L"플레이 기록 저장: " + r->exe + L" · " + FmtDuration((float)r->sec)); }
        if (r->failed) {
            if (r->pid == g_monFailPid) g_monFails++; else { g_monFailPid = r->pid; g_monFails = 1; }
            g_monRetryAt = GetTickCount() + (g_monFails == 1 ? 10000 : 60000);
            std::wstring le = Lower(r->err);
            g_monErr = r->err.empty() ? L"측정 도구가 바로 종료됐어요"
                     : le.find(L"access denied") != std::wstring::npos ? L"측정 권한이 없어요 (관리자로 실행 필요)"
                     : r->err;
            AddLog(L_ERR, L"측정 중단: " + r->exe + L" · " + g_monErr + (g_monFails >= 3 ? L" (이 게임은 다시 켤 때까지 측정 안 함)" : L""));
        } else if (r->sec > 1) {
            g_monErr.clear();
            if (r->pid == g_monFailPid) { g_monFailPid = 0; g_monFails = 0; }
        }
        // 측정하던 게임이 실제로 꺼졌을 때만 자동 복원 (대상 변경·측정 중단은 제외)
        if (r->exited && g_active && g_set.autoRestore && r->sec >= 60 && !g_busy) {
            AddLog(L_INFO, L"게임 종료: " + r->exe + L" → 원래대로 되돌림");
            StartRestore(true);
        }
        delete r;
        g_live = GetLiveStats(120);
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        Redraw();
        return 0;
    }
    case WM_APP_TOOL:
        g_toolState = wp == 1 ? TOOL_READY : TOOL_FAILED;
        AddLog(wp == 1 ? L_OK : L_ERR, wp == 1 ? L"측정 도구 준비 완료" : L"측정 도구를 받지 못했어요 (인터넷 확인)");
        Redraw();
        return 0;
    case WM_APP + 19: g_toolProgress = (int)wp; Redraw(); return 0;
    case WM_APP_UPD_CHECK:
        if (wp == 1) {
            bool first = g_updState != UPD_AVAILABLE;
            g_updState = UPD_AVAILABLE;
            if (first || g_updManual) AddLog(L_OK, L"새 버전 나옴: " + g_updVer);
            if (!IsWindowVisible(h) && !g_updNotified) Balloon(L"새 버전 나옴: " + g_updVer + L"\n열어서 업데이트하세요.");
            g_updNotified = true;
        } else if (wp == 0) { g_updState = UPD_LATEST; if (g_updManual) AddLog(L_OK, L"최신 버전이에요"); }
        else { g_updState = UPD_FAILED; if (g_updManual) AddLog(L_WARN, L"업데이트 확인 실패: " + g_updError); }
        Redraw();
        return 0;
    case WM_APP_UPD_PROG: g_updProgress = (int)wp; Redraw(); return 0;
    case WM_APP_UPD_DONE:
        if (wp == 1) {
            g_updState = UPD_APPLYING; g_updProgress = 100;
            Redraw(); UpdateWindow(h);
            AddLog(L_OK, L"업데이트 받기 완료 · 다시 시작합니다");
            StopMonitor();
            if (ApplyUpdateAndRestart()) { g_reallyExit = true; DestroyWindow(h); return 0; }
        }
        g_updState = UPD_AVAILABLE;
        AddLog(L_ERR, L"업데이트 실패: " + g_updError);
        Redraw();
        return 0;

    case WM_TIMER:
        if (wp == TIMER_WATCH) OnWatchTimer();
        else if (wp == TIMER_UPDATE) {
            KillTimer(h, TIMER_UPDATE);
            SetTimer(h, TIMER_UPDATE, 6 * 60 * 60 * 1000, nullptr);
            StartUpdateCheck(false);
        } else if (wp == TIMER_MON) {
            OnMonitorTimer();
            EnterCriticalSection(&g_monCs); bool run = g_mon.running; LeaveCriticalSection(&g_monCs);
            // 게임 측정 중에는 이 프로그램 우선순위를 낮춰 게임에 양보
            SetPriorityClass(GetCurrentProcess(), run ? BELOW_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
            if (run && g_set.overlay) SetTimer(h, TIMER_OVL, 500, nullptr);
        } else if (wp == TIMER_UI) {
            if (!IsWindowVisible(h) || IsIconic(h)) return 0;
            LiveStats ls = GetLiveStats(120);
            bool changed = ls.active || g_live.active;
            g_live = ls;
            if (changed && (g_page == PAGE_DASH)) Redraw();
            else if (changed) InvalidateRect(h, nullptr, FALSE);
        } else if (wp == TIMER_OVL) {
            OverlayTick();
            if (!g_set.overlay && !g_ovlMoving) { KillTimer(h, TIMER_OVL); OverlayRender(false); }
        }
        return 0;

    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK: ShowMain(); break;
        case WM_RBUTTONUP: case WM_CONTEXTMENU: ShowTrayMenu(); break;
        }
        return 0;
    case WM_APP_SHOW: ShowMain(); return 0;

    case WM_CLOSE: {
        if (!g_reallyExit) { HideToTray(); return 0; }
        g_reallyExit = false;
        HWND owner = IsWindowVisible(h) ? h : nullptr;
        if (g_busy) { MessageBoxW(owner, L"작업이 진행 중이에요. 끝난 뒤에 다시 시도해 주세요.", APP_NAME, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND); return 0; }
        ReloadPending();
        if (g_active && !g_pending && !FileExists(g_powerFile)) { DeleteFileW(g_boostFile.c_str()); g_active = false; }   // 되돌릴 게 없으면 그냥 해제
        if (g_active) {
            std::wstring q = (g_pending ? L"정리한 앱 " + std::to_wstring(g_pending) + L"개가 아직 꺼져 있어요."
                                        : std::wstring(L"전원 모드가 부스트 상태예요.")) + L"\n지금 원래대로 돌릴까요?\n\n(아니요 = 다음 실행 때 되돌릴 수 있어요)";
            int r = MessageBoxW(owner, q.c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND);
            if (r == IDCANCEL) return 0;
            if (r == IDYES) RestoreCore([](int l, const std::wstring& m) { AddLog(l, m); });
        }
        if (g_page == PAGE_SETTINGS) SaveGamesFromEdit();
        DestroyWindow(h);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_WATCH); KillTimer(h, TIMER_UPDATE); KillTimer(h, TIMER_MON); KillTimer(h, TIMER_UI); KillTimer(h, TIMER_OVL);
        StopMonitor();
        if (g_ovl) DestroyWindow(g_ovl);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
//  시작
// =====================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR cmdLine, int) {
    g_inst = hInst;
    std::wstring cl = Lower(cmdLine ? cmdLine : L"");
    bool startTray = cl.find(L"--tray") != std::wstring::npos;

    // 관리자 보조 실행: --tweaks "apply=a,b;revert=c" --data "폴더" [--user]
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring spec, data;
        bool userPart = false;
        for (int i = 1; argv && i < argc; i++) {
            if (!wcscmp(argv[i], L"--user")) userPart = true;
            if (i + 1 >= argc) continue;
            if (!wcscmp(argv[i], L"--tweaks")) spec = argv[i + 1];
            if (!wcscmp(argv[i], L"--data")) data = argv[i + 1];
        }
        if (argv) LocalFree(argv);
        if (!spec.empty()) {
            InitPaths();
    TrimLogFile();
            if (!data.empty()) {
                g_dataDir = data;
                g_settingsFile = g_dataDir + L"\\settings.ini";
                g_logFile = g_dataDir + L"\\booster.log";
                g_backupFile = g_dataDir + L"\\tweak_backup.txt";
            }
            std::vector<std::wstring> apply, revert;
            size_t sc = spec.find(L';');
            std::wstring a = spec.substr(0, sc), r = sc == std::wstring::npos ? L"" : spec.substr(sc + 1);
            if (a.rfind(L"apply=", 0) == 0) apply = SplitIds(a.substr(6));
            if (r.rfind(L"revert=", 0) == 0) revert = SplitIds(r.substr(7));
            auto clog = [](int l, const std::wstring& m) { AddLog(l, L"[관리자] " + m); };
            int fail = 0;
            if (userPart) { fail += RunTweakBatch(apply, revert, false, clog); ApplyLiveSettings(apply, revert); }
            fail += RunTweakBatch(apply, revert, true, clog);
            return fail;
        }
    }

    // 업데이트·관리자 재시작 직후: 이전 창이 닫힐 때까지 잠깐 기다림
    bool justRestarted = cl.find(L"--updated") != std::wstring::npos;
    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\FrameBooster_SingleInstance");
    for (int i = 0; justRestarted && GetLastError() == ERROR_ALREADY_EXISTS && i < 50; i++) {
        CloseHandle(g_mutex);
        Sleep(200);
        g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\FrameBooster_SingleInstance");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND ex = FindWindowW(WND_CLASS, nullptr);
        if (ex && !startTray) PostMessageW(ex, WM_APP_SHOW, 0, 0);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    InitializeCriticalSection(&g_monCs);
    {
        static wchar_t ff[64] = {}, fn[64] = {};
        if (GetEnvironmentVariableW(L"FB_FONT", ff, 64) > 0) FONT_UI = ff;
        if (GetEnvironmentVariableW(L"FB_FONTNUM", fn, 64) > 0) FONT_NUM = fn;
    }
    if (!InitGraphics()) { MessageBoxW(nullptr, L"그래픽 초기화에 실패했어요.", APP_NAME, MB_ICONERROR); return 1; }

    InitPaths();
    TrimLogFile();
    g_isAdmin = IsElevated();
    { wchar_t fa[4] = {}; if (GetEnvironmentVariableW(L"FB_FORCEADMIN", fa, 4) > 0) g_isAdmin = true; }   // 테스트용
    DetectPC();
    LoadSettings();
    ReloadPending();
    CheckTool();
    LoadSessionIndex();
    RefreshTweakState();
    RefreshPowerState();
    g_startupOn = IsStartupOn();
    // 관리자 권한으로 켜졌고 예전 방식(레지스트리)으로 시작 등록돼 있으면 작업 스케줄러로 옮김
    if (g_isAdmin && IsRunKeyStartup()) SetStartup(true);
    g_brCard2 = CreateSolidBrush(RGB(0x1C, 0x1C, 0x1C));
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x0B, 0x0B, 0x0B));
    wc.lpszClassName = WND_CLASS;
    wc.hIcon = LoadAppIcon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = LoadAppIcon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    std::wstring title = std::wstring(APP_NAME) + (g_isAdmin ? L"" : L"");
    CreateWindowExW(0, WND_CLASS, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));
    COLORREF cap = RGB(0x0F, 0x0F, 0x0F), capText = RGB(0xF2, 0xF2, 0xF2);
    DwmSetWindowAttribute(g_hwnd, 35, &cap, sizeof(cap));
    DwmSetWindowAttribute(g_hwnd, 36, &capText, sizeof(capText));

    {   // 테스트용: FB_PAGE=0..3
        wchar_t tb[8] = {};
        if (GetEnvironmentVariableW(L"FB_PAGE", tb, 8) > 0) g_page = std::max(0, std::min(3, _wtoi(tb)));
    }
    g_dpi = GetDpiForWindow(g_hwnd);
    ResizeToDpi(nullptr);
    Layout();
    FillGamesEdit();
    // 관리자 권한일 때도 트레이 클릭·작업 표시줄 재시작·두 번째 실행 알림을 받을 수 있게
    ChangeWindowMessageFilterEx(g_hwnd, WM_APP_TRAY, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, WM_APP_SHOW, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, g_msgTaskbarCreated, MSGFLT_ALLOW, nullptr);
    CreateOverlayWindow();
    TrayAdd();

    AddLog(L_INFO, std::wstring(L"Frame Booster ") + APP_VER + L" 시작" + (g_isAdmin ? L" · 관리자" : L"") +
                   L" · " + (g_lowEnd ? L"저사양" : L"고사양") + L" PC" + (g_isX3D ? L" · X3D" : L""));
    if (justRestarted) AddLog(L_OK, std::wstring(L"다시 시작 완료: ") + APP_VER);
    for (int i = 0; i < 10 && FileExists(OldExePath()) && !DeleteFileW(OldExePath().c_str()); i++) Sleep(200);
    DeleteFileW(NewExePath().c_str());
    if (g_active) { AddLog(L_WARN, L"이전 부스트가 아직 켜져 있어요"); StartWatch(); }
    if (g_set.monitor && g_isAdmin && g_toolState != TOOL_READY) StartToolDownload();   // 측정 도구 자동 준비
    SetTimer(g_hwnd, TIMER_UPDATE, 4000, nullptr);
    SetTimer(g_hwnd, TIMER_MON, 1000, nullptr);
    if (g_set.overlay) SetTimer(g_hwnd, TIMER_OVL, 500, nullptr);

    if (startTray) { g_tipShown = true; TrimSelf(); }
    else {
        g_apps = GetAppList();
        if (g_page == PAGE_ANALYSIS) SelectSession(g_sessions.empty() ? -1 : 0);
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        UpdateEditVisibility();
        SetTimer(g_hwnd, TIMER_UI, 500, nullptr);
        UpdateWindow(g_hwnd);
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    CoUninitialize();
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); }
    return 0;
}
