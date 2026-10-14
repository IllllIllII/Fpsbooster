// =====================================================================
//  화면: 사이드바 / 대시보드 / 부스트
// =====================================================================
enum {
    H_NONE = 0, H_NAV0, H_NAV1, H_NAV2, H_NAV3,
    H_BOOSTBTN, H_BOOST_OFF, H_ROW_APPS, H_ROW_POWER, H_ROW_MEM, H_LIVE_ACTION,
    H_POWER_SLIDER, H_POWER_0, H_POWER_1, H_POWER_2,
    H_OVL_TOGGLE, H_OVL_MODE0, H_OVL_MODE1, H_OVL_MODE2, H_OVL_POSLINK, H_UPD_BADGE,
    H_REFRESH, H_OPT_DVR, H_OPT_GAMEMODE, H_OPT_MEM, H_OPT_POWERLINK, H_LEGACY_REVERT,
    H_GRAPH_FT, H_GRAPH_FPS, H_GRAPH_LAT, H_SESS_DEL, H_GRAPH,
    H_SET_STARTUP, H_SET_AUTORESTORE, H_SET_FORCE, H_SET_CONFIRM, H_SET_MONITOR, H_SET_OVERLAY,
    H_SET_MODE0, H_SET_MODE1, H_SET_MODE2, H_OVL_MOVE, H_UPD_CHECK, H_OPEN_FOLDER, H_OPEN_LOG, H_ADMIN, H_TOOL,
    H_CONF_OK, H_CONF_CANCEL, H_CONF_NOASK, H_UPD_OK, H_UPD_LATER, H_LOG_CLOSE, H_LOG_CLEAR, H_MODAL_BG,
    H_POS0 = 500,                 // ~ H_POS0 + 8
    H_ROW = 1000, H_SESS = 3000, H_CMP = 4000
};

enum { MODAL_NONE = 0, MODAL_CONFIRM, MODAL_UPDATE, MODAL_LOG };
static int g_modal = MODAL_NONE;
static std::vector<AppInfo> g_confirmTargets;
static bool g_confirmNoAsk = false;
static float g_listScroll = 0, g_sessScroll = 0, g_logScroll = 0;
static bool g_dragPower = false;
static float g_dragPowerX = 0;
static std::wstring g_currentPlanName;
static int g_currentPlanMode = -1;
static LiveStats g_live;          // 화면 갱신 때마다 새로 읽음

// 최적화 (윈도우 설정은 최소한으로: 게임 녹화·게임 모드만)
static bool g_twDvr = false, g_twGameMode = false;
static int g_legacyCount = 0;     // 이전 버전에서 바꾼 설정 수
static std::vector<std::wstring> g_legacyIds;   // 이 프로그램이 예전에 바꾼 항목

static void RefreshTweakState() {
    auto defs = BuildTweaks();
    auto bk = LoadBackups();
    g_legacyCount = 0;
    g_legacyIds.clear();
    for (auto& t : defs) {
        bool applied = IsTweakApplied(t);
        if (t.id == L"gamedvr") g_twDvr = applied;
        else if (t.id == L"gamemode") continue;   // 아래에서 윈도우 실제 값으로 읽음
        else {
            if (!IsLegacyTweak(t.id) || !applied) continue;
            bool ours = false;   // 이 프로그램이 바꾼 것만 (백업이 있는 것)
            for (auto& v : t.vals) if (bk.count(BackupKey(RootName(v.root), v.path, v.name))) ours = true;
            if (ours) { g_legacyCount++; g_legacyIds.push_back(t.id); }
        }
    }
    g_twGameMode = GameModeOn();
}

static void RefreshPowerState() {
    GUID g;
    if (GetActivePower(g)) { g_currentPlanName = PowerName(g); g_currentPlanMode = PowerModeOf(g); }
    else { g_currentPlanName = L"알 수 없음"; g_currentPlanMode = -1; }
}

// ---------------------------------------------------------------------
//  사이드바
// ---------------------------------------------------------------------
static void DrawLogo(float x, float y, float s) {
    FillRR(x, y, s, s, s * 0.26f, 0x161716);
    StrokeRR(x, y, s, s, s * 0.26f, 0xFFFFFF, 1.f, 0.07f);
    Glow(x + s / 2, y + s / 2, s * 0.5f, s * 0.5f, C_GREEN, 0.25f);
    IconBolt(x + s / 2, y + s / 2, s * 0.92f, C_GREEN);
}

static void DrawSidebar() {
    FillRR(0, 0, 220, BASE_H, 0, C_SIDE);
    Line(220, 0, 220, BASE_H, 0xFFFFFF, 1.f, 0.05f);
    DrawLogo(22, 24, 34);
    Text(L"FRAME", 66, 24, 80, 34, 17, 700, C_TEXT, 0, 1, false, FONT_NUM);
    Text(L"BOOSTER", 66 + TextW(L"FRAME ", 17, 700, FONT_NUM), 24, 100, 34, 17, 700, C_GREEN, 0, 1, false, FONT_NUM);

    const wchar_t* names[4] = { L"대시보드", L"부스트", L"프레임 분석", L"설정" };
    for (int i = 0; i < 4; i++) {
        float y = 92.f + i * 50.f;
        bool act = g_page == i, hv = g_hover == H_NAV0 + i;
        if (act) { FillRR(12, y, 196, 42, 10, C_CARD2); FillRR(12, y + 11, 3, 20, 1.5f, C_GREEN); }
        else if (hv) FillRR(12, y, 196, 42, 10, 0x161616);
        UINT32 ic = act ? C_GREEN : hv ? C_TEXT : C_DIM;
        float cx = 38, cy = y + 21;
        if (i == 0) IconGrid(cx, cy, 16, ic);
        else if (i == 1) IconBolt(cx, cy, 24, ic);
        else if (i == 2) IconChart(cx, cy, 16, ic);
        else IconSliders(cx, cy, 16, ic);
        Text(names[i], 58, y, 140, 42, 14, act ? 700 : 400, act ? C_TEXT : hv ? C_TEXT : C_DIM);
        AddHot(12, y, 196, 42, H_NAV0 + i);
    }

    // 상태 상자
    FillRR(12, 572, 196, 96, 12, C_CARD);
    FillCircle(30, 596, 4.5f, g_active ? C_GREEN : 0x4A4A4A);
    if (g_active) Glow(30, 596, 12, 12, C_GREEN, 0.35f);
    Text(g_active ? L"부스트 켜짐" : L"부스트 꺼짐", 42, 584, 160, 24, 13, 700, g_active ? C_GREEN : C_TEXT);
    std::wstring ms; UINT32 mc = C_DIM;
    if (g_live.active) { ms = L"측정 중 · " + g_live.exe; mc = C_TEXT; }
    else if (!g_set.monitor) ms = L"측정 꺼짐";
    else if (!g_isAdmin) ms = L"측정: 관리자 권한 필요";
    else if (g_toolState != TOOL_READY) ms = L"측정 준비 필요";
    else ms = L"게임 대기 중";
    Text(ms, 24, 612, 176, 20, 11.5f, 400, mc);
    std::wstring cs = g_currentPlanName.empty() ? L"" : L"전원: " + g_currentPlanName;
    Text(cs, 24, 634, 176, 20, 11.5f, 400, C_FAINT);

    Text(APP_VER, 24, 680, 60, 24, 11.5f, 700, C_FAINT, 0, 1, false, FONT_NUM);
}

// ---------------------------------------------------------------------
//  대시보드
// ---------------------------------------------------------------------
static int CheckedCount(unsigned long long* mem) {
    int n = 0; unsigned long long m = 0;
    for (auto& a : g_apps) if (IsChecked(a)) { n++; m += a.memMB; }
    if (mem) *mem = m;
    return n;
}

static void DrawBoostHero() {
    float x = 244, y = 24, w = 560, h = 276;
    Card(x, y, w, h);
    PushClip(x, y, w, h);
    Glow(x + 150, y + 138, 190, 170, C_GREEN, g_active ? 0.16f : 0.06f);
    PopClip();

    float cx = x + 150, cy = y + 140, r = 86;
    bool hv = g_hover == H_BOOSTBTN && !g_busy;
    if (g_active) {
        Glow(cx, cy, r + 40, r + 40, C_GREEN, hv ? 0.45f : 0.32f);
        FillCircle(cx, cy, r, hv ? C_GREENHI : C_GREEN);
        StrokeCircle(cx, cy, r - 10, 0x000000, 1.5f, 0.18f);
        IconBolt(cx, cy - 14, 58, C_INK);
        Text(L"ON", cx - 60, cy + 22, 120, 30, 19, 700, C_INK, 1, 1, false, FONT_NUM);
    } else {
        if (hv) Glow(cx, cy, r + 34, r + 34, C_GREEN, 0.22f);
        FillCircle(cx, cy, r, 0x121212);
        StrokeCircle(cx, cy, r - 1.5f, hv ? C_GREENHI : C_GREEN, 3.f);
        StrokeCircle(cx, cy, r - 13, 0xFFFFFF, 1.f, 0.06f);
        IconBolt(cx, cy - 14, 58, g_busy ? C_FAINT : C_GREEN);
        Text(g_busy ? L"···" : L"BOOST", cx - 60, cy + 22, 120, 30, 19, 700, g_busy ? C_FAINT : C_TEXT, 1, 1, false, FONT_NUM);
    }
    if (!g_busy) AddHot(cx - r, cy - r, r * 2, r * 2, H_BOOSTBTN);

    float tx = x + 312, tw = w - 336;
    Text(L"게임 부스트", tx, y + 26, tw, 32, 22, 700, C_TEXT);
    if (g_busy) Text(L"처리 중이에요...", tx, y + 60, tw, 22, 13, 400, C_DIM);
    else if (g_active) Text(L"켜짐 · 게임이 끝나면 자동으로 원래대로", tx, y + 60, tw, 22, 13, 700, C_GREEN);
    else Text(L"게임 전에 한 번 눌러주세요", tx, y + 60, tw, 22, 13, 400, C_DIM);

    unsigned long long mem = 0;
    int n = CheckedCount(&mem);
    struct Row { const wchar_t* k; std::wstring v; int id; UINT32 vc; } rows[3] = {
        { L"정리할 앱", g_active ? std::to_wstring(g_pending) + L"개 정리됨" : std::to_wstring(n) + L"개 · " + FmtNum(mem) + L" MB", H_ROW_APPS, C_TEXT },
        { L"전원 모드", PowerModeName(EffectivePowerMode()), H_ROW_POWER, C_TEXT },
        { L"메모리 정리", g_set.memClean ? L"켜짐" : L"꺼짐", H_ROW_MEM, g_set.memClean ? C_TEXT : C_FAINT } };
    for (int i = 0; i < 3; i++) {
        float ry = y + 100 + i * 42;
        bool rh = g_hover == rows[i].id;
        if (rh) FillRR(tx - 10, ry, tw + 10, 40, 8, C_CARD2);
        Text(rows[i].k, tx, ry, 120, 40, 13, 400, C_DIM);
        Text(rows[i].v + L"  ›", tx + 100, ry, tw - 104, 40, 13.5f, 700, rh ? C_GREEN : rows[i].vc, 2);
        if (i < 2) Line(tx, ry + 41, tx + tw, ry + 41, 0xFFFFFF, 1.f, 0.05f);
        AddHot(tx - 10, ry, tw + 10, 40, rows[i].id);
    }
    if (g_active && !g_busy) Button(tx, y + 228, 132, 32, L"부스트 해제", H_BOOST_OFF, false, true, 12.5f);
    else Text(L"디스코드와 게임 런처는 그대로 둬요", tx, y + 228, tw, 32, 11.5f, 400, C_FAINT);
}

static void DrawSpark(float x, float y, float w, float h, const std::vector<float>& sp, UINT32 c) {
    if (sp.size() < 2) return;
    float mx = 0;
    for (float v : sp) mx = std::max(mx, v);
    mx = std::max(mx * 1.15f, 8.f);
    std::vector<D2D1_POINT_2F> pts;
    for (size_t i = 0; i < sp.size(); i++)
        pts.push_back({ x + w * i / (sp.size() - 1), y + h - (h - 2) * std::min(1.f, sp[i] / mx) });
    PushClip(x, y - 2, w, h + 2);
    std::vector<D2D1_POINT_2F> poly = pts;
    poly.push_back({ x + w, y + h }); poly.push_back({ x, y + h });
    FillPoly(poly.data(), (int)poly.size(), c, 0.14f);
    StrokePoly(pts.data(), (int)pts.size(), c, 1.5f, false);
    PopClip();
}

static void DrawLiveTile() {
    float x = 820, y = 24, w = 336, h = 276;
    Card(x, y, w, h);
    Text(L"실시간 프레임", x + 24, y + 18, 200, 28, 15, 700, C_TEXT);
    bool measuring = g_live.active;
    std::wstring chip = measuring ? L"측정 중" : (g_set.monitor ? L"대기" : L"꺼짐");
    float cw = ChipW(chip);
    if (measuring) Chip(x + w - 24 - cw, y + 21, chip, C_GREEN, C_GREENSF);
    else Chip(x + w - 24 - cw, y + 21, chip, C_DIM, C_CARD3);

    if (measuring) {
        Text(g_live.exe, x + 24, y + 48, w - 48, 20, 12, 400, C_DIM);
        std::wstring f = g_live.fps > 0 ? FmtF(g_live.fps, 0) : L"--";
        Text(f, x + 22, y + 66, 200, 76, 62, 700, C_TEXT, 0, 1, false, FONT_NUM);
        Text(L"FPS", x + 30 + TextW(f, 62, 700, FONT_NUM), y + 104, 60, 26, 15, 700, C_GREEN, 0, 1, false, FONT_NUM);
        auto stat = [&](float sx, const wchar_t* k, const std::wstring& v, const wchar_t* unit) {
            Text(k, sx, y + 146, 100, 18, 11.5f, 400, C_DIM);
            Text(v, sx, y + 162, 100, 28, 21, 700, C_TEXT, 0, 1, false, FONT_NUM);
            if (unit && v != L"--") Text(unit, sx + TextW(v, 21, 700, FONT_NUM) + 3, y + 169, 30, 18, 11, 700, C_DIM, 0, 1, false, FONT_NUM);
        };
        stat(x + 24, L"1% Low", g_live.low1 > 0 ? FmtF(g_live.low1, 0) : L"--", nullptr);
        stat(x + 124, L"프레임타임", g_live.ms > 0 ? FmtF(g_live.ms, 1) : L"--", L"ms");
        stat(x + 224, g_live.latKind ? LatencyName(g_live.latKind) : L"PC 레이턴시", g_live.lat > 0 ? FmtF(g_live.lat, 1) : L"--", L"ms");
        FillRR(x + 24, y + 200, w - 48, 38, 6, 0xFFFFFF, 0.03f);
        DrawSpark(x + 24, y + 200, w - 48, 38, g_live.spark, C_GREEN);
        int gr = PresentModeGrade(g_live.mode);
        UINT32 mc = gr == 2 ? C_GREEN : gr == 1 ? C_WARN : gr == 0 ? C_ERR : C_FAINT;
        FillCircle(x + 29, y + 256, 3.5f, mc);
        Text(g_live.mode.empty() ? L"Present Mode 확인 중" : g_live.mode, x + 38, y + 246, w - 62, 20, 11.5f, 700, mc, 0, 1, false, FONT_NUM);
        return;
    }
    FillCircle(x + w / 2, y + 104, 30, C_CARD2);
    IconChart(x + w / 2, y + 104, 22, C_DIM);
    std::wstring msg, sub, btn;
    if (!g_set.monitor) { msg = L"프레임 측정이 꺼져 있어요"; btn = L"측정 켜기"; }
    else if (!g_isAdmin) { msg = L"프레임 측정은 관리자 권한이 필요해요"; sub = L"한 번만 다시 시작하면 돼요"; btn = L"관리자로 다시 시작"; }
    else if (g_toolState == TOOL_DOWNLOADING) { msg = L"측정 도구 받는 중 " + std::to_wstring(g_toolProgress) + L"%"; sub = L"Intel PresentMon · 1MB"; }
    else if (g_toolState != TOOL_READY) { msg = g_toolState == TOOL_FAILED ? L"측정 도구를 받지 못했어요" : L"측정 도구가 필요해요"; sub = L"Intel PresentMon · 1MB · 처음 한 번만"; btn = L"받기"; }
    else if (!g_monErr.empty()) { msg = L"측정이 멈췄어요"; sub = g_monErr; }
    else { msg = L"게임을 실행하면 자동으로 측정해요"; sub = L"전체 화면 게임 또는 감시 게임 목록"; }
    Text(msg, x + 16, y + 146, w - 32, 24, 13.5f, 700, C_TEXT, 1);
    Text(sub, x + 16, y + 172, w - 32, 20, 11.5f, 400, C_FAINT, 1);
    if (!btn.empty()) {
        float bw = TextW(btn, 13, 700) + 40;
        Button(x + (w - bw) / 2, y + 206, bw, 36, btn, H_LIVE_ACTION, true);
    }
}

static void DrawEffectBars(float x, float y, const wchar_t* label, float off, float on) {
    Text(label, x, y, 110, 40, 13, 700, C_DIM);
    float bx = x + 104, bw = 270;
    float mx = std::max(std::max(off, on), 1.f) * 1.08f;
    FillRR(bx, y + 2, bw, 14, 4, 0xFFFFFF, 0.03f);
    FillRR(bx, y + 2, std::max(4.f, bw * off / mx), 14, 4, 0x4A4A4A);
    Text(FmtF(off, 0), bx + std::max(4.f, bw * off / mx) + 8, y, 60, 18, 12.5f, 700, C_DIM, 0, 1, false, FONT_NUM);
    FillRR(bx, y + 22, bw, 14, 4, 0xFFFFFF, 0.03f);
    FillRR(bx, y + 22, std::max(4.f, bw * on / mx), 14, 4, C_GREEN);
    Text(FmtF(on, 0), bx + std::max(4.f, bw * on / mx) + 8, y + 20, 60, 18, 12.5f, 700, C_TEXT, 0, 1, false, FONT_NUM);
    float d = off > 0 ? (on - off) / off * 100.f : 0;
    if (fabsf(d) < 0.05f) d = 0;   // "-0.0%" 방지
    std::wstring ds = (d > 0 ? L"+" : L"") + FmtF(d, fabsf(d) >= 10 ? 0 : 1) + L"%";
    Text(ds, x + 420, y, 92, 40, 19, 700, d >= 0.5f ? C_GREEN : d <= -0.5f ? C_ERR : C_DIM, 2, 1, false, FONT_NUM);
}

static void DrawEffectTile() {
    float x = 244, y = 316, w = 560, h = 220;
    Card(x, y, w, h);
    Text(L"부스트 효과", x + 24, y + 18, 200, 28, 15, 700, C_TEXT);
    std::wstring game = g_sessions.empty() ? L"" : g_sessions[0].exe;
    if (!game.empty()) {
        float cw = ChipW(game);
        Chip(x + w - 24 - cw, y + 21, game, C_DIM, C_CARD3);
    }
    const SessionInfo *off = nullptr, *on = nullptr;
    if (!game.empty() && FindEffect(game, &off, &on)) {
        DrawEffectBars(x + 24, y + 62, L"평균 FPS", off->avgFps, on->avgFps);
        DrawEffectBars(x + 24, y + 116, L"1% Low", off->low1, on->low1);
        FillRR(x + 24, y + 178, 10, 10, 2, 0x4A4A4A);
        Text(L"부스트 없이 · " + FmtSessionTime(off->start), x + 40, y + 172, 200, 22, 11, 400, C_FAINT);
        FillRR(x + 254, y + 178, 10, 10, 2, C_GREEN);
        Text(L"부스트 · " + FmtSessionTime(on->start), x + 270, y + 172, 200, 22, 11, 400, C_FAINT);
        return;
    }
    // 아직 비교할 기록이 없음: 안내
    bool hasOff = false, hasOn = false;
    for (auto& s : g_sessions) if (game.empty() || IEq(s.exe, game)) { if (s.boosted) hasOn = true; else hasOff = true; }
    auto step = [&](float sy, int n, const wchar_t* t, bool done) {
        if (done) { FillCircle(x + 38, sy + 14, 12, C_GREEN); IconCheck(x + 38, sy + 14, 13, C_INK, 2.4f); }
        else { StrokeCircle(x + 38, sy + 14, 11.5f, 0x4A4A4A, 1.5f); Text(std::to_wstring(n), x + 26, sy, 24, 28, 12, 700, C_DIM, 1, 1, false, FONT_NUM); }
        Text(t, x + 60, sy, 360, 28, 13.5f, done ? 400 : 700, done ? C_DIM : C_TEXT);
    };
    step(y + 62, 1, L"부스트 없이 한 판 플레이", hasOff);
    step(y + 102, 2, L"부스트 켜고 같은 게임 한 판 플레이", hasOn);
    Text(L"두 판이 기록되면 평균 FPS와 1% Low 차이를 보여드려요.\n게임을 20초 이상 하면 자동으로 기록돼요 (측정 켜짐 필요).",
         x + 26, y + 146, w - 52, 48, 11.5f, 400, C_FAINT, 0, 0, true);
}

static float PowerStopX(int i) { return 868.f + i * 120.f; }

static void DrawPowerTile() {
    float x = 820, y = 316, w = 336, h = 220;
    Card(x, y, w, h);
    Text(L"전원 모드", x + 24, y + 18, 200, 28, 15, 700, C_TEXT);
    float cw = ChipW(L"부스트 중 적용");
    Chip(x + w - 24 - cw, y + 21, L"부스트 중 적용", C_DIM, C_CARD3);
    int mode = EffectivePowerMode();
    float ty = y + 96;
    float kx = g_dragPower ? std::max(PowerStopX(0), std::min(PowerStopX(2), g_dragPowerX)) : PowerStopX(mode);
    FillRR(PowerStopX(0), ty - 3, PowerStopX(2) - PowerStopX(0), 6, 3, C_CARD3);
    FillRR(PowerStopX(0), ty - 3, kx - PowerStopX(0), 6, 3, C_GREEN);
    const wchar_t* names[3] = { L"균형 조정", L"고성능", L"최고의 성능" };
    for (int i = 0; i < 3; i++) {
        float sx = PowerStopX(i);
        bool sel = i == mode;
        FillCircle(sx, ty, 5, sx <= kx + 0.5f ? C_GREEN : 0x3A3A3A);
        bool hv = g_hover == H_POWER_0 + i;
        Text(names[i], sx - 56, ty + 18, 112, 22, 12.5f, sel ? 700 : 400, sel ? C_TEXT : hv ? C_TEXT : C_DIM, 1);
        AddHot(sx - 56, ty + 16, 112, 26, H_POWER_0 + i);
    }
    if (g_isX3D) {
        float bx = PowerStopX(0);
        float bw2 = ChipW(L"X3D 권장", 10);
        Chip(bx - bw2 / 2, ty - 40, L"X3D 권장", C_GREEN, C_GREENSF, 1.f, 20, 10);
    }
    bool kh = g_hover == H_POWER_SLIDER || g_dragPower;
    if (kh) Glow(kx, ty, 22, 22, C_GREEN, 0.35f);
    FillCircle(kx, ty, 11, 0xFFFFFF);
    StrokeCircle(kx, ty, 11, C_GREEN, 3.f);
    AddHot(PowerStopX(0) - 16, ty - 16, PowerStopX(2) - PowerStopX(0) + 32, 32, H_POWER_SLIDER);

    std::wstring desc = mode == PWR_BALANCED ? (g_isX3D ? L"X3D CPU에 가장 좋아요. 게임 코어 배치를 윈도우가 알맞게 해줘요."
                                                        : L"필요할 때만 클럭을 올려요. 노트북·저발열에 좋아요.")
                      : mode == PWR_HIGH ? L"클럭을 높게 유지해 반응이 빨라요. 소비 전력이 조금 늘어요."
                                         : L"절전 기능을 모두 꺼요. 데스크톱 전용, 발열과 전력이 가장 커요.";
    Text(desc, x + 24, y + 150, w - 48, 40, 11.5f, 400, C_DIM, 0, 0, true);
    Text(L"지금 윈도우: " + g_currentPlanName, x + 24, y + 190, w - 48, 20, 11, 400, C_FAINT);
}

static std::wstring PosName(int p) {
    static const wchar_t* n[] = { L"왼쪽 위", L"위 가운데", L"오른쪽 위", L"왼쪽 가운데", L"가운데", L"오른쪽 가운데",
                                  L"왼쪽 아래", L"아래 가운데", L"오른쪽 아래", L"직접 지정" };
    return n[std::max(0, std::min(9, p))];
}

static void DrawPCTile() {
    float x = 244, y = 552, w = 560, h = 144;
    Card(x, y, w, h);
    Text(L"내 PC", x + 24, y + 16, 120, 26, 15, 700, C_TEXT);
    std::wstring tier = g_lowEnd ? L"저사양" : L"고사양";
    Chip(x + 24 + TextW(L"내 PC", 15, 700) + 10, y + 18, tier, g_lowEnd ? C_WARN : C_GREEN, g_lowEnd ? 0x2A2112 : C_GREENSF);
    auto row = [&](float ry, const wchar_t* k, const std::wstring& v) {
        Text(k, x + 24, ry, 50, 22, 12, 400, C_FAINT);
        Text(v.empty() ? L"알 수 없음" : v, x + 74, ry, 220, 22, 12.5f, 700, C_TEXT);
    };
    row(y + 52, L"CPU", g_cpuName);
    row(y + 78, L"GPU", g_gpuName + (g_vramGB ? L" · " + std::to_wstring(g_vramGB) + L"GB" : L""));
    row(y + 104, L"RAM", std::to_wstring(g_ramGB) + L" GB · " + std::to_wstring(g_cores) + L" 스레드");
    FillRR(x + 312, y + 16, 224, 112, 10, C_CARD2);
    Text(L"이 PC에서 효과 큰 것", x + 326, y + 26, 200, 20, 11.5f, 700, C_GREEN);
    Text(g_lowEnd ? L"백그라운드 앱 정리와 게임 녹화 끄기가 평균 FPS를 올려줘요."
                  : L"백그라운드 앱 정리가 끊김(1% Low)을 줄여줘요. 평균 FPS 차이는 작아요.",
         x + 326, y + 48, 200, 72, 11.5f, 400, C_DIM, 0, 0, true);
}

static void DrawSegmented(float x, float y, float w, float h, const wchar_t** labels, int n, int sel, int idBase, bool enabled = true) {
    FillRR(x, y, w, h, h / 2, C_CARD2);
    float sw = (w - 6) / n;
    for (int i = 0; i < n; i++) {
        float sx = x + 3 + i * sw;
        bool act = i == sel, hv = enabled && g_hover == idBase + i;
        if (act) FillRR(sx, y + 3, sw, h - 6, (h - 6) / 2, enabled ? C_GREEN : 0x3A3A3A);
        else if (hv) FillRR(sx, y + 3, sw, h - 6, (h - 6) / 2, 0x2A2A2A);
        Text(labels[i], sx, y, sw, h, 12.5f, 700, act ? (enabled ? C_INK : C_DIM) : hv ? C_TEXT : C_DIM, 1);
        if (enabled) AddHot(sx, y, sw, h, idBase + i);
    }
}

static void DrawOverlayTile() {
    float x = 820, y = 552, w = 336, h = 144;
    Card(x, y, w, h);
    Text(L"게임 오버레이", x + 24, y + 16, 200, 26, 15, 700, C_TEXT);
    Toggle(x + w - 68, y + 17, g_set.overlay, g_hover == H_OVL_TOGGLE);
    AddHot(x + w - 76, y + 10, 60, 38, H_OVL_TOGGLE);
    const wchar_t* labels[3] = { L"간단히", L"기본", L"자세히" };
    DrawSegmented(x + 24, y + 56, w - 48, 34, labels, 3, g_set.overlayMode, H_OVL_MODE0);
    bool lh = g_hover == H_OVL_POSLINK;
    Text(L"위치: " + PosName(g_set.overlayPos) + L"  ›", x + 24, y + 102, w - 48, 24, 12, lh ? 700 : 400, lh ? C_GREEN : C_DIM);
    AddHot(x + 24, y + 102, w - 48, 24, H_OVL_POSLINK);
}

static void DrawDashboard() {
    DrawBoostHero();
    DrawLiveTile();
    DrawEffectTile();
    DrawPowerTile();
    DrawPCTile();
    DrawOverlayTile();
}

// ---------------------------------------------------------------------
//  부스트 페이지: 정리할 앱 + 효과 있는 최적화
// ---------------------------------------------------------------------
static const float AL_X = 252, AL_Y = 128, AL_W = 544, AL_H = 556, AL_PITCH = 54;
static float AppMaxScroll() { return std::max(0.f, g_apps.size() * AL_PITCH - 4 - AL_H); }

static void DrawImpact(float x, float y, int lvl, bool mine) {
    // lvl: 2 큼 / 1 1% Low / 0 적음
    const wchar_t* t = lvl == 2 ? L"큼" : lvl == 1 ? L"1% Low" : L"적음";
    float w = ChipW(t, 10.5f);
    if (lvl == 2) FillRR(x, y, w, 20, 10, C_GREEN, mine ? 1.f : 0.55f);
    else if (lvl == 1) { FillRR(x, y, w, 20, 10, C_GREENSF); StrokeRR(x, y, w, 20, 10, C_GREEN, 1.f, mine ? 0.9f : 0.45f); }
    else FillRR(x, y, w, 20, 10, C_CARD3);
    Text(t, x, y, w, 20, 10.5f, 700, lvl == 2 ? C_INK : lvl == 1 ? C_GREEN : C_DIM, 1, 1, false, FONT_UI, mine ? 1.f : 0.7f);
}

static void DrawBoostPage() {
    // 왼쪽: 정리할 앱
    float x = 244, y = 24, w = 560, h = 672;
    Card(x, y, w, h);
    unsigned long long mem = 0;
    int n = CheckedCount(&mem);
    Text(L"정리할 앱", x + 24, y + 18, 300, 30, 17, 700, C_TEXT);
    Text(L"부스트하면 " + std::to_wstring(n) + L"개 종료 · 약 " + FmtNum(mem) + L" MB 확보", x + 24, y + 48, 400, 22, 12.5f, 400, C_DIM);
    Button(x + w - 124, y + 20, 100, 32, L"새로고침", H_REFRESH, false, !g_busy, 12.5f);
    Text(L"체크 해제한 앱은 유지돼요 · 디스코드와 게임 런처는 기본으로 유지", x + 24, y + 76, w - 48, 22, 11.5f, 400, C_FAINT);
    Line(x + 16, y + 102, x + w - 16, y + 102, 0xFFFFFF, 1.f, 0.05f);
    if (g_apps.empty()) {
        Text(L"목록이 비어 있어요. 새로고침을 눌러 주세요.", x, AL_Y, w, 200, 13, 400, C_DIM, 1);
    } else {
        g_listScroll = std::max(0.f, std::min(g_listScroll, AppMaxScroll()));
        PushClip(AL_X, AL_Y, AL_W, AL_H);
        for (size_t i = 0; i < g_apps.size(); i++) {
            float ry = AL_Y + i * AL_PITCH - g_listScroll;
            if (ry + 50 < AL_Y || ry > AL_Y + AL_H) continue;
            auto& a = g_apps[i];
            bool chk = IsChecked(a);
            int id = H_ROW + (int)i;
            bool hv = g_hover == id;
            float rw = AL_W - 12;
            if (hv) FillRR(AL_X, ry, rw, 50, 10, C_CARD2);
            if (chk) { FillRR(AL_X + 16, ry + 14, 22, 22, 6, C_GREEN); IconCheck(AL_X + 27, ry + 25, 13, C_INK, 2.4f); }
            else StrokeRR(AL_X + 16, ry + 14, 22, 22, 6, hv ? C_DIM : 0x4A4A4A, 1.6f);
            Text(a.display, AL_X + 52, ry + 5, 340, 22, 13.5f, 700, chk ? C_TEXT : C_FAINT);
            Text(a.name + L".exe" + (a.count > 1 ? L" ×" + std::to_wstring(a.count) : L"") + L"   " + a.path, AL_X + 52, ry + 27, 380, 18, 11, 400, C_FAINT);
            Text(FmtNum(a.memMB) + L" MB", AL_X + rw - 150, ry, 136, 50, 13, 700, chk ? C_TEXT : C_FAINT, 2, 1, false, FONT_NUM);
            float hy = std::max(ry, AL_Y), hb = std::min(ry + 50, AL_Y + AL_H);
            if (hb > hy) AddHot(AL_X, hy, rw, hb - hy, id);
        }
        PopClip();
        float maxs = AppMaxScroll();
        if (maxs > 0) {
            float th = std::max(40.f, AL_H * AL_H / (AL_H + maxs));
            FillRR(AL_X + AL_W - 4, AL_Y + (AL_H - th) * (g_listScroll / maxs), 4, th, 2, C_CARD3);
        }
    }

    // 오른쪽: 효과 있는 최적화
    x = 820; w = 336;
    Card(x, y, w, h);
    Text(L"최적화 효과", x + 24, y + 18, 200, 30, 17, 700, C_TEXT);
    Text(L"효과가 확인된 것만 골랐어요", x + 24, y + 48, w - 48, 22, 12.5f, 400, C_DIM);
    // 범례
    float lx = x + 24, ly = y + 80;
    DrawImpact(lx, ly, 2, true); lx += ChipW(L"큼", 10.5f) + 6;
    Text(L"평균 FPS", lx, ly, 60, 20, 11, 400, C_FAINT); lx += 62;
    DrawImpact(lx, ly, 1, true); lx += ChipW(L"1% Low", 10.5f) + 6;
    Text(L"끊김", lx, ly, 40, 20, 11, 400, C_FAINT); lx += 36;
    DrawImpact(lx, ly, 0, true);
    Line(x + 16, y + 112, x + w - 16, y + 112, 0xFFFFFF, 1.f, 0.05f);

    struct Item { const wchar_t* t; std::wstring d; int low, high; int kind; bool on; int id; };
    // kind: 0 항상(부스트) / 1 토글 / 2 링크
    Item items[5] = {
        { L"백그라운드 앱 정리", L"다른 앱이 쓰던 CPU·RAM을 게임에", 2, 1, 0, true, 0 },
        { L"게임 녹화 끄기", L"Xbox 백그라운드 녹화 중지", 2, 1, 1, g_twDvr, H_OPT_DVR },
        { L"게임 모드", L"윈도우가 게임에 자원을 먼저 배정", 1, 0, 1, g_twGameMode, H_OPT_GAMEMODE },
        { L"메모리 정리", L"부스트할 때 남은 앱 메모리 비우기", 1, 0, 1, g_set.memClean, H_OPT_MEM },
        { L"전원 모드", std::wstring(L"부스트 중 ") + PowerModeName(EffectivePowerMode()) + (g_isX3D ? L" · X3D는 균형" : L""), 2, 0, 2, false, H_OPT_POWERLINK } };
    float iy = y + 124;
    for (int i = 0; i < 5; i++) {
        auto& it = items[i];
        bool hv = it.id && g_hover == it.id && !g_busy;
        if (hv) FillRR(x + 12, iy - 6, w - 24, 88, 10, C_CARD2);
        Text(it.t, x + 24, iy, 200, 24, 14, 700, C_TEXT);
        Text(it.d, x + 24, iy + 24, w - 104, 20, 11.5f, 400, C_FAINT);
        float cx2 = x + 24;
        Text(L"저사양", cx2, iy + 50, 44, 20, 11, g_lowEnd ? 700 : 400, g_lowEnd ? C_TEXT : C_FAINT);
        DrawImpact(cx2 + 44, iy + 50, it.low, g_lowEnd);
        cx2 += 44 + ChipW(it.low == 2 ? L"큼" : it.low == 1 ? L"1% Low" : L"적음", 10.5f) + 16;
        Text(L"고사양", cx2, iy + 50, 44, 20, 11, !g_lowEnd ? 700 : 400, !g_lowEnd ? C_TEXT : C_FAINT);
        DrawImpact(cx2 + 44, iy + 50, it.high, !g_lowEnd);
        if (it.kind == 0) Chip(x + w - 24 - ChipW(L"부스트"), iy + 2, L"부스트", C_GREEN, C_GREENSF);
        else if (it.kind == 1) Toggle(x + w - 68, iy + 2, it.on, hv, !g_busy);
        else Text(L"대시보드  ›", x + w - 120, iy, 96, 24, 12, 700, hv ? C_GREEN : C_DIM, 2);
        if (it.id && !g_busy) AddHot(x + 12, iy - 6, w - 24, 88, it.id);
        iy += 94;
    }
    if (g_legacyCount > 0) {
        FillRR(x + 12, y + h - 70, w - 24, 58, 10, 0x2A2112);
        Text(L"이전 버전에서 바꾼 윈도우 설정 " + std::to_wstring(g_legacyCount) + L"개", x + 24, y + h - 64, w - 130, 24, 12, 700, C_WARN);
        Text(L"v3부터는 쓰지 않아요", x + 24, y + h - 42, w - 130, 22, 11, 400, C_DIM);
        Button(x + w - 104, y + h - 58, 80, 34, L"원래대로", H_LEGACY_REVERT, false, !g_busy, 12);
    }
}
