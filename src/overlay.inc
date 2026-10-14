// =====================================================================
//  게임 오버레이 (투명·클릭 통과 창, 0.5초마다 갱신)
//  - 게임에 코드를 넣지 않는 바깥 창 방식 (안티치트 안전)
// =====================================================================
static ID2D1DCRenderTarget* g_ovlRt;
static ID2D1SolidColorBrush* g_ovlBrush;
static HDC g_ovlDC;
static HBITMAP g_ovlBmp;
static HGDIOBJ g_ovlOld;
static int g_ovlW = 0, g_ovlH = 0;
static bool g_ovlMoving = false;   // 위치 직접 옮기기 모드
static bool g_ovlShown = false;

static void OverlaySizeBase(int mode, float& w, float& h) {
    if (mode == OVL_SIMPLE) { w = 104; h = 44; }
    else if (mode == OVL_BASIC) { w = 178; h = 110; }
    else { w = 262; h = 194; }
}

static void DrawOverlayContent(float w, float h, const LiveStats& ls, bool sample) {
    R->Clear(D2D1::ColorF(0, 0, 0, 0));
    FillRR(0.5f, 0.5f, w - 1, h - 1, 10, 0x000000, 0.74f);
    StrokeRR(0.5f, 0.5f, w - 1, h - 1, 10, g_ovlMoving ? C_GREEN : 0xFFFFFF, g_ovlMoving ? 1.5f : 1.f, g_ovlMoving ? 0.9f : 0.10f);
    float fps = sample ? 144 : ls.fps, low1 = sample ? 118 : ls.low1, low01 = sample ? 96 : ls.low01, ms = sample ? 6.9f : ls.ms;
    float lat = sample ? 11.4f : ls.lat;
    int latKind = sample ? 1 : ls.latKind;
    std::wstring latK = latKind ? LatencyName(latKind) : L"PC 레이턴시";
    std::wstring latV = lat > 0 ? FmtF(lat, 1) + L" ms" : L"--";
    std::wstring fpsS = fps > 0 ? FmtF(fps, 0) : L"--";
    int mode = g_set.overlayMode;
    if (mode == OVL_SIMPLE) {
        Text(fpsS, 10, 2, w - 52, h - 4, 26, 700, C_GREEN, 2, 1, false, FONT_NUM);
        Text(L"FPS", w - 40, 4, 34, h - 8, 11, 700, C_DIM, 0, 1, false, FONT_NUM);
        return;
    }
    Text(fpsS, 12, 6, 90, 40, 30, 700, C_GREEN, 0, 1, false, FONT_NUM);
    Text(L"FPS", 12 + TextW(fpsS, 30, 700, FONT_NUM) + 5, 22, 40, 18, 11, 700, C_DIM, 0, 1, false, FONT_NUM);
    auto row = [&](float y, const std::wstring& k, const std::wstring& v, UINT32 vc) {
        Text(k, 12, y, w * 0.5f, 18, 11.5f, 400, C_DIM);
        Text(v, w * 0.4f, y, w * 0.6f - 12, 18, 12, 700, vc, 2, 1, false, FONT_NUM);
    };
    if (mode == OVL_BASIC) {
        row(48, L"1% Low", low1 > 0 ? FmtF(low1, 0) : L"--", C_TEXT);
        row(68, L"프레임타임", ms > 0 ? FmtF(ms, 1) + L" ms" : L"--", C_TEXT);
        row(86, latK, latV, C_TEXT);
        return;
    }
    // 자세히
    std::wstring mode2 = sample ? L"Hardware: Independent Flip" : ls.mode;
    int grade = sample ? 2 : PresentModeGrade(mode2);
    Text(ls.gpuBusy > 0 && !sample ? L"GPU " + FmtF(ls.gpuBusy, 0) + L"%" : (sample ? L"GPU 97%" : L""), w - 100, 10, 88, 18, 11.5f, 700, C_DIM, 2, 1, false, FONT_NUM);
    row(48, L"1% Low", low1 > 0 ? FmtF(low1, 0) : L"--", C_TEXT);
    row(66, L"0.1% Low", low01 > 0 ? FmtF(low01, 0) : L"--", C_TEXT);
    row(84, L"프레임타임", ms > 0 ? FmtF(ms, 1) + L" ms" : L"--", C_TEXT);
    row(102, latK, latV, C_TEXT);
    // 프레임타임 그래프
    float gx = 12, gy = 124, gw = w - 24, gh = 34;
    FillRR(gx, gy, gw, gh, 4, 0xFFFFFF, 0.04f);
    std::vector<float> sp = sample ? std::vector<float>() : ls.spark;
    if (sample) for (int i = 0; i < 120; i++) sp.push_back(6.9f + (float)(sin(i * 0.4) * 0.6 + (i % 37 == 0 ? 6 : 0)));
    if (sp.size() >= 2) {
        float mx = 0;
        for (float v : sp) mx = std::max(mx, v);
        mx = std::max(mx * 1.15f, 8.f);
        std::vector<D2D1_POINT_2F> pts;
        for (size_t i = 0; i < sp.size(); i++)
            pts.push_back({ gx + gw * i / (sp.size() - 1), gy + gh - 2 - (gh - 4) * std::min(1.f, sp[i] / mx) });
        StrokePoly(pts.data(), (int)pts.size(), C_GREEN, 1.2f, false);
    }
    UINT32 mc = grade == 2 ? C_GREEN : grade == 1 ? C_WARN : grade == 0 ? C_ERR : C_FAINT;
    FillCircle(17, 174, 3.5f, mc);
    Text(mode2.empty() ? L"Present Mode 확인 중" : mode2, 26, 165, w - 36, 18, 11, 700, mc, 0, 1, false, FONT_NUM);
}

static void OverlayFreeSurface() {
    if (g_ovlDC) { SelectObject(g_ovlDC, g_ovlOld); DeleteDC(g_ovlDC); g_ovlDC = nullptr; }
    if (g_ovlBmp) { DeleteObject(g_ovlBmp); g_ovlBmp = nullptr; }
    g_ovlW = g_ovlH = 0;
}

static bool OverlayEnsureSurface(int w, int h) {
    if (!g_ovlRt) {
        D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
        if (FAILED(g_d2d->CreateDCRenderTarget(&rp, &g_ovlRt))) return false;
        g_ovlRt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        g_ovlRt->CreateSolidColorBrush(Col(C_TEXT), &g_ovlBrush);
    }
    if (g_ovlDC && w == g_ovlW && h == g_ovlH) return true;
    OverlayFreeSurface();
    BITMAPINFO bi; ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    g_ovlBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    g_ovlDC = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!g_ovlBmp || !g_ovlDC) { OverlayFreeSurface(); return false; }
    g_ovlOld = SelectObject(g_ovlDC, g_ovlBmp);
    g_ovlW = w; g_ovlH = h;
    return true;
}

// 게임 창이 있는 모니터 (없으면 주 모니터)
static HMONITOR OverlayMonitor() {
    EnterCriticalSection(&g_monCs);
    DWORD pid = g_mon.running ? g_mon.pid : 0;
    LeaveCriticalSection(&g_monCs);
    HWND fg = GetForegroundWindow();
    DWORD fpid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fpid);
    if (pid && fpid == pid) return MonitorFromWindow(fg, MONITOR_DEFAULTTOPRIMARY);
    if (g_ovl && g_ovlShown) return MonitorFromWindow(g_ovl, MONITOR_DEFAULTTOPRIMARY);
    POINT p = { 0, 0 };
    return MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
}

static void OverlayRender(bool show) {
    if (!g_ovl) return;
    if (!show) {
        if (g_ovlShown) { ShowWindow(g_ovl, SW_HIDE); g_ovlShown = false; }
        return;
    }
    LiveStats ls = GetLiveStats(g_set.overlayMode == OVL_DETAIL ? 150 : 0);
    bool sample = g_ovlMoving && !ls.active;
    HMONITOR mon = OverlayMonitor();
    MONITORINFO mi; mi.cbSize = sizeof(mi); GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96;
    typedef HRESULT(WINAPI* PFN_GDFM)(HMONITOR, int, UINT*, UINT*);
    static PFN_GDFM gdfm = (PFN_GDFM)(void*)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");
    if (gdfm) gdfm(mon, 0, &dx, &dy);
    float sc = dx / 96.f;
    float bw, bh; OverlaySizeBase(g_set.overlayMode, bw, bh);
    int w = (int)(bw * sc + 0.5f), h = (int)(bh * sc + 0.5f);
    if (!OverlayEnsureSurface(w, h)) return;
    RECT rc = { 0, 0, w, h };
    if (FAILED(g_ovlRt->BindDC(g_ovlDC, &rc))) return;
    ID2D1RenderTarget* oldR = R; ID2D1SolidColorBrush* oldB = RB;
    R = g_ovlRt; RB = g_ovlBrush;
    g_ovlRt->SetDpi(96.f * sc, 96.f * sc);
    g_ovlRt->BeginDraw();
    DrawOverlayContent(bw, bh, ls, sample);
    g_ovlRt->EndDraw();
    R = oldR; RB = oldB;
    // 위치
    const RECT& wa = mi.rcMonitor;
    int m = (int)(14 * sc);
    int x, y;
    int pos = g_set.overlayPos;
    if (pos == POS_CUSTOM) {
        x = wa.left + (int)(g_set.overlayX * (wa.right - wa.left - w));
        y = wa.top + (int)(g_set.overlayY * (wa.bottom - wa.top - h));
    } else {
        int col = pos % 3, row = pos / 3;
        x = col == 0 ? wa.left + m : col == 1 ? (wa.left + wa.right - w) / 2 : wa.right - w - m;
        y = row == 0 ? wa.top + m : row == 1 ? (wa.top + wa.bottom - h) / 2 : wa.bottom - h - m;
    }
    if (g_ovlMoving && g_ovlShown) {   // 옮기는 중에는 현재 위치 유지
        RECT cur; GetWindowRect(g_ovl, &cur);
        x = cur.left; y = cur.top;
    }
    POINT ptDst = { x, y }, ptSrc = { 0, 0 };
    SIZE sz = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(g_ovl, screen, &ptDst, &sz, g_ovlDC, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    if (!g_ovlShown) {
        SetWindowPos(g_ovl, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_ovlShown = true;
    }
}

// 오버레이를 보여줄지: 켜져 있고, 측정 중인 게임이 앞에 있을 때 (옮기기 모드는 항상)
static bool OverlayShouldShow() {
    if (g_ovlMoving) return true;
    if (!g_set.overlay) return false;
    EnterCriticalSection(&g_monCs);
    DWORD pid = g_mon.running ? g_mon.pid : 0;
    LeaveCriticalSection(&g_monCs);
    if (!pid) return false;
    HWND fg = GetForegroundWindow();
    DWORD fpid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fpid);
    return fpid == pid;
}

static void OverlayTick() { OverlayRender(OverlayShouldShow()); }

static void SetOverlayMoving(bool on) {
    g_ovlMoving = on;
    LONG ex = GetWindowLongW(g_ovl, GWL_EXSTYLE);
    if (on) ex &= ~WS_EX_TRANSPARENT; else ex |= WS_EX_TRANSPARENT;
    SetWindowLongW(g_ovl, GWL_EXSTYLE, ex);
    OverlayTick();
}

static LRESULT CALLBACK OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        if (g_ovlMoving) return HTCAPTION;   // 끌어서 옮기기
        return HTTRANSPARENT;
    case WM_EXITSIZEMOVE: {
        RECT r; GetWindowRect(h, &r);
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& wa = mi.rcMonitor;
        float fx = (float)(r.left - wa.left) / std::max(1L, (wa.right - wa.left) - (r.right - r.left));
        float fy = (float)(r.top - wa.top) / std::max(1L, (wa.bottom - wa.top) - (r.bottom - r.top));
        g_set.overlayX = std::max(0.f, std::min(1.f, fx));
        g_set.overlayY = std::max(0.f, std::min(1.f, fy));
        g_set.overlayPos = POS_CUSTOM;
        SaveSettings();
        Redraw();
        return 0;
    }
    case WM_NCLBUTTONDBLCLK:
        SetOverlayMoving(false);
        Redraw();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void CreateOverlayWindow() {
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    wc.lpszClassName = OVL_CLASS;
    RegisterClassExW(&wc);
    g_ovl = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                            OVL_CLASS, L"Frame Booster Overlay", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, g_inst, nullptr);
}
