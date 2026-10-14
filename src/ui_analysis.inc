// =====================================================================
//  화면: 프레임 분석 (기록 목록 · 통계 · 그래프 확대/이동 · 비교)
// =====================================================================
static int g_selSess = -1, g_cmpSess = -1;
static std::vector<float> g_selFt, g_cmpFt;
static std::vector<float> g_selLat, g_cmpLat;     // 프레임별 레이턴시 (0 = 값 없음)
static std::vector<double> g_selCum, g_cmpCum;     // 누적 시간 (ms)
static double g_viewA = 0, g_viewB = 0;            // 보이는 구간 (초)
static int g_graphMode = 0;                         // 0 프레임타임, 1 FPS, 2 레이턴시
static bool g_graphDrag = false;
static float g_dragX0 = 0;
static double g_dragA = 0, g_dragB = 0;
static int g_delArm = -1;

static const float GP_X = 632, GP_Y = 250, GP_W = 500, GP_H = 340;   // 그래프 영역
static const float SL_X = 252, SL_Y = 104, SL_W = 284, SL_H = 584, SL_PITCH = 66;

static void BuildCum(const std::vector<float>& ft, std::vector<double>& cum) {
    cum.resize(ft.size());
    double t = 0;
    for (size_t i = 0; i < ft.size(); i++) { t += ft[i]; cum[i] = t; }
}

static double TotalSec(const std::vector<double>& cum) { return cum.empty() ? 0 : cum.back() / 1000.0; }

static void SelectSession(int i) {
    if (i < 0 || i >= (int)g_sessions.size()) { g_selSess = -1; g_selFt.clear(); g_selLat.clear(); g_selCum.clear(); return; }
    g_selSess = i;
    g_selFt = LoadSessionFrames(g_sessions[i].file, &g_selLat);
    if (g_graphMode == 2 && g_selLat.empty()) g_graphMode = 0;
    BuildCum(g_selFt, g_selCum);
    g_viewA = 0; g_viewB = std::max(1.0, TotalSec(g_selCum));
    if (g_cmpSess == i) { g_cmpSess = -1; g_cmpFt.clear(); g_cmpLat.clear(); g_cmpCum.clear(); }
    g_delArm = -1;
}

static void SetCompare(int i) {
    if (i == g_cmpSess || i == g_selSess || i < 0 || i >= (int)g_sessions.size()) {
        g_cmpSess = -1; g_cmpFt.clear(); g_cmpLat.clear(); g_cmpCum.clear();
        return;
    }
    g_cmpSess = i;
    g_cmpFt = LoadSessionFrames(g_sessions[i].file, &g_cmpLat);
    BuildCum(g_cmpFt, g_cmpCum);
}

static void ReloadSessions(bool selectNewest) {
    std::wstring keepSel = g_selSess >= 0 && g_selSess < (int)g_sessions.size() ? g_sessions[g_selSess].file : L"";
    std::wstring keepCmp = g_cmpSess >= 0 && g_cmpSess < (int)g_sessions.size() ? g_sessions[g_cmpSess].file : L"";
    LoadSessionIndex();
    int sel = -1, cmp = -1;
    for (int i = 0; i < (int)g_sessions.size(); i++) {
        if (g_sessions[i].file == keepSel) sel = i;
        if (g_sessions[i].file == keepCmp) cmp = i;
    }
    if (selectNewest || sel < 0) { SelectSession(g_sessions.empty() ? -1 : 0); g_cmpSess = -1; g_cmpFt.clear(); g_cmpLat.clear(); g_cmpCum.clear(); }
    else { g_selSess = sel; g_cmpSess = cmp; }
}

static size_t IndexAt(const std::vector<double>& cum, double ms) {
    return (size_t)(std::lower_bound(cum.begin(), cum.end(), ms) - cum.begin());
}

static FrameStats RangeStats(const std::vector<float>& ft, const std::vector<double>& cum, double a, double b) {
    if (ft.empty()) return FrameStats();
    size_t ia = IndexAt(cum, a * 1000.0), ib = std::min(ft.size(), IndexAt(cum, b * 1000.0) + 1);
    if (ib <= ia + 1) return FrameStats();
    return ComputeStats(ft.data() + ia, ib - ia);
}

// 보이는 구간의 레이턴시 평균 / 상위 1% (0 = 값 없음)
static void RangeLatency(const std::vector<float>& lat, const std::vector<double>& cum, double a, double b, float& avg, float& p99) {
    avg = p99 = 0;
    if (lat.empty() || lat.size() != cum.size()) return;
    size_t ia = IndexAt(cum, a * 1000.0), ib = std::min(lat.size(), IndexAt(cum, b * 1000.0) + 1);
    std::vector<float> v;
    for (size_t i = ia; i < ib; i++) if (lat[i] > 0) v.push_back(lat[i]);
    if (v.empty()) return;
    double sum = 0;
    for (float x : v) sum += x;
    avg = (float)(sum / v.size());
    size_t k = std::min(v.size() - 1, (size_t)(0.99 * (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    p99 = v[k];
}

static std::wstring FmtClock(double sec) {
    int s = (int)(sec + 0.5);
    wchar_t b[32];
    if (s >= 3600) swprintf(b, 32, L"%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else swprintf(b, 32, L"%d:%02d", s / 60, s % 60);
    return b;
}

static double NiceStep(double range, int ticks) {
    double raw = range / ticks;
    double mag = pow(10.0, floor(log10(raw)));
    double n = raw / mag;
    double step = n < 1.5 ? 1 : n < 2.25 ? 2 : n < 3.5 ? 2.5 : n < 7.5 ? 5 : 10;
    return step * mag;
}

// 열(픽셀)마다 최소/평균/최대 프레임타임
struct Col3 { float mn, avg, mx; bool ok; };
// vals: 그릴 값 (프레임타임 또는 레이턴시, 0은 건너뜀) / cum: 프레임타임 누적 시간
static std::vector<Col3> Columns(const std::vector<float>& ft, const std::vector<double>& cum, double a, double b, int n) {
    std::vector<Col3> c(n, { 0, 0, 0, false });
    if (ft.empty() || ft.size() != cum.size()) return c;
    double span = b - a;
    for (int i = 0; i < n; i++) {
        double t0 = (a + span * i / n) * 1000.0, t1 = (a + span * (i + 1) / n) * 1000.0;
        size_t i0 = IndexAt(cum, t0), i1 = IndexAt(cum, t1);
        if (i0 >= ft.size()) break;
        if (i1 >= ft.size()) i1 = ft.size() - 1;
        float mn = 1e9f, mx = 0; double sum = 0; int k = 0;
        for (size_t j = i0; j <= i1; j++) { if (ft[j] <= 0) continue; mn = std::min(mn, ft[j]); mx = std::max(mx, ft[j]); sum += ft[j]; k++; }
        if (k) c[i] = { mn, (float)(sum / k), mx, true };
    }
    return c;
}

static void DrawSeries(const std::vector<Col3>& cols, double ymax, UINT32 color, bool envelope) {
    std::vector<D2D1_POINT_2F> pts;
    int n = (int)cols.size();
    for (int i = 0; i < n; i++) {
        if (!cols[i].ok) continue;
        float x = GP_X + GP_W * (i + 0.5f) / n;
        auto Y = [&](double v) { return (float)(GP_Y + GP_H - GP_H * std::min(1.0, std::max(0.0, v / ymax))); };
        bool inv = g_graphMode == 1;   // FPS 는 1000/ms
        double vAvg = inv ? 1000.0 / cols[i].avg : cols[i].avg;
        if (envelope) {
            double v1 = inv ? 1000.0 / cols[i].mx : cols[i].mx;   // 가장 나쁜 쪽
            Line(x, Y(vAvg), x, Y(v1), color, 1.f, 0.22f);
        }
        pts.push_back({ x, Y(vAvg) });
    }
    if (pts.size() >= 2) StrokePoly(pts.data(), (int)pts.size(), color, envelope ? 1.6f : 1.4f, false);
}

static void DrawStatBox(float x, float y, float w, const wchar_t* label, const std::wstring& v, float delta, bool hasDelta, bool higherBetter,
                        const wchar_t* unit = nullptr) {
    FillRR(x, y, w, 70, 10, C_CARD2);
    Text(label, x + 12, y + 8, w - 16, 18, 11, 400, C_DIM);
    Text(v, x + 12, y + 26, w - 16, 28, 21, 700, C_TEXT, 0, 1, false, FONT_NUM);
    if (unit) Text(unit, x + 14 + TextW(v, 21, 700, FONT_NUM), y + 33, 30, 18, 11, 700, C_DIM, 0, 1, false, FONT_NUM);
    if (hasDelta) {
        bool good = higherBetter ? delta > 0.5f : delta < -0.5f;
        bool bad = higherBetter ? delta < -0.5f : delta > 0.5f;
        if (fabsf(delta) < 0.05f) delta = 0;   // "-0.0%" 방지
        std::wstring ds = (delta > 0 ? L"+" : L"") + FmtF(delta, 1) + L"%";
        Text(ds, x + 12, y + 50, w - 16, 16, 11, 700, good ? C_GREEN : bad ? C_ERR : C_DIM, 0, 1, false, FONT_NUM);
    }
}

static void DrawAnalysis() {
    // ── 왼쪽: 기록 목록
    float x = 244, y = 24, w = 300, h = 672;
    Card(x, y, w, h);
    Text(L"플레이 기록", x + 24, y + 18, 200, 30, 17, 700, C_TEXT);
    Text(std::to_wstring(g_sessions.size()) + L"개 · 앱 전체 100MB 안에서 최신순 보관", x + 24, y + 48, w - 48, 22, 12, 400, C_DIM);
    Line(x + 16, y + 76, x + w - 16, y + 76, 0xFFFFFF, 1.f, 0.05f);
    if (g_sessions.empty()) {
        Text(L"아직 기록이 없어요", x, y + 240, w, 24, 14, 700, C_TEXT, 1);
        Text(L"측정이 켜진 상태로 게임을\n20초 이상 하면 자동으로 기록돼요", x + 20, y + 268, w - 40, 44, 12, 400, C_FAINT, 1, 0, true);
    } else {
        float maxs = std::max(0.f, g_sessions.size() * SL_PITCH - 4 - SL_H);
        g_sessScroll = std::max(0.f, std::min(g_sessScroll, maxs));
        PushClip(SL_X, SL_Y, SL_W, SL_H);
        for (size_t i = 0; i < g_sessions.size(); i++) {
            float ry = SL_Y + i * SL_PITCH - g_sessScroll;
            if (ry + 62 < SL_Y || ry > SL_Y + SL_H) continue;
            auto& s = g_sessions[i];
            bool sel = (int)i == g_selSess, cmp = (int)i == g_cmpSess;
            int id = H_SESS + (int)i;
            bool hv = g_hover == id || g_hover == H_CMP + (int)i;
            float rw = SL_W - 10;
            if (sel) { FillRR(SL_X, ry, rw, 62, 10, C_CARD2); FillRR(SL_X, ry + 14, 3, 34, 1.5f, C_GREEN); }
            else if (cmp) { FillRR(SL_X, ry, rw, 62, 10, 0x18202A); FillRR(SL_X, ry + 14, 3, 34, 1.5f, C_BLUE); }
            else if (hv) FillRR(SL_X, ry, rw, 62, 10, 0x1A1A1A);
            Text(s.exe, SL_X + 16, ry + 8, 170, 22, 13.5f, 700, C_TEXT);
            Text(FmtF(s.avgFps, 0), SL_X + rw - 96, ry + 6, 82, 26, 19, 700, sel ? C_GREEN : C_TEXT, 2, 1, false, FONT_NUM);
            Text(FmtSessionTime(s.start) + L" · " + FmtDuration(s.durSec), SL_X + 16, ry + 32, 170, 20, 11, 400, C_FAINT);
            float bx = SL_X + rw - 14;
            if (s.boosted) { float cw = ChipW(L"BOOST", 9.5f); bx -= cw; Chip(bx, ry + 34, L"BOOST", C_GREEN, C_GREENSF, 1.f, 18, 9.5f); bx -= 6; }
            if (!sel && (hv || cmp)) {
                std::wstring ct = cmp ? L"비교 중" : L"비교";
                float cw = ChipW(ct, 9.5f);
                bx -= cw;
                bool ch = g_hover == H_CMP + (int)i;
                FillRR(bx, ry + 34, cw, 18, 9, cmp ? C_BLUE : ch ? 0x2A3A4A : 0x22303D);
                Text(ct, bx, ry + 34, cw, 18, 9.5f, 700, cmp ? 0x05121F : C_BLUE, 1);
                float hy = std::max(ry + 34, SL_Y), hb = std::min(ry + 52, SL_Y + SL_H);
                if (hb > hy) AddHot(bx - 2, hy, cw + 4, hb - hy, H_CMP + (int)i);
            }
            float hy = std::max(ry, SL_Y), hb = std::min(ry + 62, SL_Y + SL_H);
            if (hb > hy) g_hots.insert(g_hots.begin(), { SL_X, hy, rw, hb - hy, id });   // 비교 버튼보다 아래 우선순위
        }
        PopClip();
        if (maxs > 0) {
            float th = std::max(40.f, SL_H * SL_H / (SL_H + maxs));
            FillRR(SL_X + SL_W - 4, SL_Y + (SL_H - th) * (g_sessScroll / maxs), 4, th, 2, C_CARD3);
        }
    }

    // ── 오른쪽: 선택한 기록
    x = 560; w = 596;
    Card(x, y, w, h);
    if (g_selSess < 0 || g_selSess >= (int)g_sessions.size() || g_selFt.empty()) {
        FillCircle(x + w / 2, y + 290, 36, C_CARD2);
        IconChart(x + w / 2, y + 290, 28, C_DIM);
        Text(L"왼쪽에서 기록을 고르면 프레임 그래프를 보여드려요", x, y + 340, w, 24, 13.5f, 700, C_TEXT, 1);
        Text(L"다른 기록의 '비교'를 누르면 부스트 전후를 겹쳐 볼 수 있어요", x, y + 366, w, 22, 12, 400, C_FAINT, 1);
        return;
    }
    auto& s = g_sessions[g_selSess];
    Text(s.exe, x + 24, y + 16, 330, 30, 19, 700, C_TEXT);
    float hx = x + 24;
    // 통계 (보이는 구간 기준, 로딩·메뉴 구간은 뺌)
    FrameStats st = RangeStats(g_selFt, g_selCum, g_viewA, g_viewB);
    std::wstring sub = FmtSessionTime(s.start) + L" · " + FmtDuration(s.durSec);
    if (st.playSec > 0 && st.durSec - st.playSec >= 1.f) sub += L" · 플레이 " + FmtDuration(st.playSec) + L" 기준";
    Text(sub, hx, y + 46, TextW(sub, 12, 400) + 4, 22, 12, 400, C_DIM);
    hx += TextW(sub, 12, 400) + 10;
    if (s.boosted) { Chip(hx, y + 46, L"BOOST", C_GREEN, C_GREENSF, 1.f, 20, 10); hx += ChipW(L"BOOST", 10) + 6; }
    if (s.power >= 0) { std::wstring pn = PowerModeName(s.power); Chip(hx, y + 46, pn, C_DIM, C_CARD3, 1.f, 20, 10); hx += ChipW(pn, 10) + 6; }
    std::wstring dl = g_delArm == g_selSess ? L"정말 삭제?" : L"삭제";
    float dw = TextW(dl, 12, 700) + 30;
    Button(x + w - 24 - dw, y + 20, dw, 30, dl, H_SESS_DEL, false, true, 12);

    bool cmp = g_cmpSess >= 0 && !g_cmpFt.empty();
    FrameStats cs;
    if (cmp) cs = ComputeStats(g_cmpFt.data(), g_cmpFt.size());
    auto pct = [](float a, float b) { return b > 0 ? (a - b) / b * 100.f : 0.f; };
    float lat = 0, latP99 = 0, clat = 0, clatP99 = 0;
    RangeLatency(g_selLat, g_selCum, g_viewA, g_viewB, lat, latP99);
    if (cmp) RangeLatency(g_cmpLat, g_cmpCum, 0, 1e12, clat, clatP99);
    bool cmpLat = cmp && lat > 0 && clat > 0 && g_sessions[g_cmpSess].latKind == s.latKind;
    float bw = (w - 48 - 5 * 8) / 6.f, by = y + 80;
    DrawStatBox(x + 24 + 0 * (bw + 8), by, bw, L"평균 FPS", FmtF(st.avgFps, 0), pct(st.avgFps, cs.avgFps), cmp, true);
    DrawStatBox(x + 24 + 1 * (bw + 8), by, bw, L"1% Low", FmtF(st.low1, 0), pct(st.low1, cs.low1), cmp, true);
    DrawStatBox(x + 24 + 2 * (bw + 8), by, bw, L"0.1% Low", FmtF(st.low01, 0), pct(st.low01, cs.low01), cmp, true);
    DrawStatBox(x + 24 + 3 * (bw + 8), by, bw, L"프레임타임", FmtF(st.avgMs, st.avgMs < 10 ? 2 : 1), pct(st.avgMs, cs.avgMs), cmp, false, L"ms");
    DrawStatBox(x + 24 + 4 * (bw + 8), by, bw, s.latKind ? LatencyName(s.latKind) : L"레이턴시", lat > 0 ? FmtF(lat, lat < 10 ? 2 : 1) : L"--",
                pct(lat, clat), cmpLat, false, lat > 0 ? L"ms" : nullptr);
    DrawStatBox(x + 24 + 5 * (bw + 8), by, bw, L"끊김", std::to_wstring(st.stutters) + L"회", 0, false, false);

    // 그래프 헤더
    bool hasLat = !g_selLat.empty();
    if (!hasLat && g_graphMode == 2) g_graphMode = 0;
    const wchar_t* gl[3] = { L"프레임타임", L"FPS", L"레이턴시" };
    DrawSegmented(x + 24, y + 166, hasLat ? 270 : 180, 32, gl, hasLat ? 3 : 2, g_graphMode, H_GRAPH_FT);
    double total = TotalSec(g_selCum);
    bool zoomed = g_viewA > 0.01 || g_viewB < total - 0.01;
    float zx = x + 24 + (hasLat ? 270 : 180) + 14;
    Text((zoomed ? L"구간 " : L"전체 ") + FmtClock(g_viewA) + L" – " + FmtClock(g_viewB), zx, y + 166, 150, 32, 12, 700, zoomed ? C_GREEN : C_DIM);
    Text(hasLat ? L"휠 확대 · 드래그 이동" : L"휠 확대 · 드래그 이동 · 더블클릭 전체", x + w - 224, y + 166, 200, 32, 11, 400, C_FAINT, 2);

    // 그래프
    FillRR(x + 16, y + 208, w - 32, 400, 10, 0x111111);
    bool latMode = g_graphMode == 2;
    std::vector<Col3> cols = Columns(latMode ? g_selLat : g_selFt, g_selCum, g_viewA, g_viewB, (int)GP_W);
    std::vector<Col3> ccols;
    bool cmpDraw = cmp && (!latMode || cmpLat);
    if (cmpDraw) ccols = Columns(latMode ? g_cmpLat : g_cmpFt, g_cmpCum, g_viewA, g_viewB, (int)GP_W);
    double ymax;
    if (latMode) {
        ymax = std::max(10.0, (double)std::max(latP99, cmpLat ? clatP99 : 0.f) * 1.3);
    } else if (g_graphMode == 0) {
        float p = st.low01 > 0 ? 1000.f / st.low01 : 20.f;
        ymax = std::max(10.0, (double)p * 1.6);
    } else {
        double mx = std::max(st.avgFps, cmp ? cs.avgFps : 0.f);
        ymax = std::max(30.0, mx * 1.5);
    }
    double step = NiceStep(ymax, 4);
    ymax = ceil(ymax / step) * step;
    for (double v = 0; v <= ymax + 1e-6; v += step) {
        float yy = (float)(GP_Y + GP_H - GP_H * v / ymax);
        Line(GP_X, yy, GP_X + GP_W, yy, 0xFFFFFF, 1.f, v == 0 ? 0.12f : 0.05f);
        Text(FmtF(v, step < 1 ? 1 : 0), GP_X - 50, yy - 9, 42, 18, 10.5f, 400, C_FAINT, 2, 1, false, FONT_NUM);
    }
    Text(g_graphMode == 1 ? L"FPS" : L"ms", GP_X - 50, GP_Y - 30, 42, 18, 10.5f, 700, C_DIM, 2, 1, false, FONT_NUM);
    double span = g_viewB - g_viewA;
    static const double tsteps[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
    double tstep = 3600;
    for (double ts : tsteps) if (span / ts <= 6) { tstep = ts; break; }
    for (double t = ceil(g_viewA / tstep) * tstep; t <= g_viewB; t += tstep) {
        float xx = (float)(GP_X + GP_W * (t - g_viewA) / span);
        Line(xx, GP_Y + GP_H, xx, GP_Y + GP_H + 4, 0xFFFFFF, 1.f, 0.2f);
        Text(FmtClock(t), xx - 30, GP_Y + GP_H + 6, 60, 18, 10.5f, 400, C_FAINT, 1, 1, false, FONT_NUM);
    }
    PushClip(GP_X, GP_Y - 2, GP_W, GP_H + 4);
    if (cmpDraw) DrawSeries(ccols, ymax, C_BLUE, false);
    DrawSeries(cols, ymax, C_GREEN, true);
    PopClip();
    AddHot(GP_X, GP_Y, GP_W, GP_H, H_GRAPH);

    // 마우스 위치 값
    if (g_mouseX >= GP_X && g_mouseX < GP_X + GP_W && g_mouseY >= GP_Y && g_mouseY < GP_Y + GP_H && !g_graphDrag && g_modal == MODAL_NONE) {
        int ci = (int)(g_mouseX - GP_X);
        Line(g_mouseX, GP_Y, g_mouseX, GP_Y + GP_H, 0xFFFFFF, 1.f, 0.25f);
        if (ci >= 0 && ci < (int)cols.size() && cols[ci].ok) {
            double t = g_viewA + span * ci / GP_W;
            std::wstring l1 = FmtClock(t);
            auto fv = [&](float v) { return g_graphMode == 1 ? FmtF(1000.0 / v, 0) + L" FPS" : FmtF(v, 2) + L" ms"; };
            std::wstring l2 = fv(cols[ci].avg);
            std::wstring l3 = cmpDraw && ci < (int)ccols.size() && ccols[ci].ok ? fv(ccols[ci].avg) : L"";
            float tw = 128, th = l3.empty() ? 50 : 70;
            float tx = g_mouseX + 12 + tw > GP_X + GP_W ? g_mouseX - 12 - tw : g_mouseX + 12;
            float ty = std::max(GP_Y, std::min(g_mouseY - th / 2, GP_Y + GP_H - th));
            FillRR(tx, ty, tw, th, 8, 0x000000, 0.85f);
            StrokeRR(tx, ty, tw, th, 8, 0xFFFFFF, 1.f, 0.1f);
            Text(l1, tx + 10, ty + 6, tw - 20, 16, 10.5f, 400, C_DIM, 0, 1, false, FONT_NUM);
            FillCircle(tx + 14, ty + 34, 3.5f, C_GREEN);
            Text(l2, tx + 24, ty + 24, tw - 32, 20, 13, 700, C_TEXT, 0, 1, false, FONT_NUM);
            if (!l3.empty()) { FillCircle(tx + 14, ty + 54, 3.5f, C_BLUE); Text(l3, tx + 24, ty + 44, tw - 32, 20, 13, 700, C_TEXT, 0, 1, false, FONT_NUM); }
        }
    }

    // 범례 / Present Mode
    float ly = y + h - 46;
    FillRR(x + 24, ly + 9, 14, 4, 2, C_GREEN);
    Text(L"이 기록", x + 44, ly, 80, 22, 11.5f, 400, C_DIM);
    if (cmp) {
        FillRR(x + 112, ly + 9, 14, 4, 2, C_BLUE);
        Text(L"비교: " + FmtSessionTime(g_sessions[g_cmpSess].start) + (g_sessions[g_cmpSess].boosted ? L" (BOOST)" : L""), x + 132, ly, 220, 22, 11.5f, 400, C_DIM);
    }
    int gr = PresentModeGrade(s.mode);
    UINT32 mc = gr == 2 ? C_GREEN : gr == 1 ? C_WARN : gr == 0 ? C_ERR : C_FAINT;
    std::wstring pm = s.mode.empty() ? L"Present Mode 알 수 없음" : s.mode;
    float pw = TextW(pm, 11.5f, 700, FONT_NUM);
    FillCircle(x + w - 34 - pw, ly + 11, 3.5f, mc);
    Text(pm, x + w - 24 - pw, ly, pw, 22, 11.5f, 700, mc, 2, 1, false, FONT_NUM);
}

static void GraphZoom(float mouseX, float factor) {
    double total = TotalSec(g_selCum);
    if (total <= 0) return;
    double span = g_viewB - g_viewA;
    double t = g_viewA + span * std::max(0.f, std::min(1.f, (mouseX - GP_X) / GP_W));
    double ns = std::max(2.0, std::min(total, span * factor));
    double a = t - (t - g_viewA) * ns / span;
    a = std::max(0.0, std::min(a, total - ns));
    g_viewA = a; g_viewB = a + ns;
}

static void GraphPan(float dx) {
    double total = TotalSec(g_selCum);
    double span = g_dragB - g_dragA;
    double a = g_dragA - dx / GP_W * span;
    a = std::max(0.0, std::min(a, total - span));
    g_viewA = a; g_viewB = a + span;
}

static void GraphReset() { g_viewA = 0; g_viewB = std::max(1.0, TotalSec(g_selCum)); }
