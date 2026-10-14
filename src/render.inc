// =====================================================================
//  Direct2D / DirectWrite 그리기 (소프트웨어 렌더링: 게임 GPU를 쓰지 않음)
//  모든 좌표는 1180x720 기준 단위. 렌더 타깃 DPI로 화면 배율 적용
// =====================================================================
static ID2D1Factory* g_d2d;
static IDWriteFactory* g_dw;
static ID2D1HwndRenderTarget* g_rt;
static ID2D1RenderTarget* R;            // 지금 그리는 대상 (메인 창 또는 오버레이)
static ID2D1SolidColorBrush* RB;        // 지금 대상의 브러시
static ID2D1SolidColorBrush* g_mainBrush;
static ID2D1StrokeStyle* g_roundStroke;

struct TextFmt { IDWriteTextFormat* f = nullptr; IDWriteInlineObject* sign = nullptr; };
static std::map<std::wstring, TextFmt> g_fmts;

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

static D2D1_COLOR_F Col(UINT32 rgb, float a = 1.f) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a);
}

static bool InitGraphics() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&g_d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw))) return false;
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    g_d2d->CreateStrokeStyle(sp, nullptr, 0, &g_roundStroke);
    return true;
}

static void DiscardMainTarget() { SafeRelease(g_mainBrush); SafeRelease(g_rt); }

static bool EnsureMainTarget() {
    if (g_rt) return true;
    RECT rc; GetClientRect(g_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp = D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(rc.right, rc.bottom));
    if (FAILED(g_d2d->CreateHwndRenderTarget(rp, hp, &g_rt))) return false;
    g_rt->SetDpi(96.f * g_scale, 96.f * g_scale);
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    g_rt->CreateSolidColorBrush(Col(C_TEXT), &g_mainBrush);
    return true;
}

static TextFmt GetFmt(float size, int weight, const wchar_t* family) {
    wchar_t key[96];
    swprintf(key, 96, L"%s|%.1f|%d", family, size, weight);
    auto it = g_fmts.find(key);
    if (it != g_fmts.end()) return it->second;
    TextFmt tf;
    if (SUCCEEDED(g_dw->CreateTextFormat(family, nullptr, (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, size, L"ko-kr", &tf.f)))
        g_dw->CreateEllipsisTrimmingSign(tf.f, &tf.sign);
    g_fmts[key] = tf;
    return tf;
}

// ── 도형 ─────────────────────────────────────────────────────────────
static void FillRR(float x, float y, float w, float h, float r, UINT32 c, float a = 1.f) {
    RB->SetColor(Col(c, a));
    if (r <= 0.f) R->FillRectangle(D2D1::RectF(x, y, x + w, y + h), RB);
    else R->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), RB);
}

static void StrokeRR(float x, float y, float w, float h, float r, UINT32 c, float width = 1.f, float a = 1.f) {
    RB->SetColor(Col(c, a));
    float i = width / 2;
    R->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + i, y + i, x + w - i, y + h - i), r, r), RB, width);
}

static void Line(float x1, float y1, float x2, float y2, UINT32 c, float w = 1.f, float a = 1.f) {
    RB->SetColor(Col(c, a));
    R->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), RB, w, g_roundStroke);
}

static void FillCircle(float cx, float cy, float r, UINT32 c, float a = 1.f) {
    RB->SetColor(Col(c, a));
    R->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), RB);
}

static void StrokeCircle(float cx, float cy, float r, UINT32 c, float w = 1.f, float a = 1.f) {
    RB->SetColor(Col(c, a));
    R->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), RB, w);
}

// 은은한 빛: 겹친 반투명 원 (어느 환경에서나 같은 모양)
static void Glow(float cx, float cy, float rx, float ry, UINT32 c, float a) {
    const int N = 16;
    float step = a / N * 1.6f;
    for (int i = 0; i < N; i++) {
        float t = 1.f - (float)i / N;          // 1 -> 바깥
        RB->SetColor(Col(c, std::min(1.f, step * (1.f - t) + step * 0.15f)));
        R->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx * t, ry * t), RB);
    }
}

static void FillPoly(const D2D1_POINT_2F* pts, int n, UINT32 c, float a = 1.f) {
    ID2D1PathGeometry* g = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&g))) return;
    ID2D1GeometrySink* k = nullptr;
    if (SUCCEEDED(g->Open(&k))) {
        k->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
        k->AddLines(pts + 1, n - 1);
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
        k->Close(); k->Release();
        RB->SetColor(Col(c, a));
        R->FillGeometry(g, RB);
    }
    g->Release();
}

static void StrokePoly(const D2D1_POINT_2F* pts, int n, UINT32 c, float w, bool closed, float a = 1.f) {
    if (n < 2) return;
    ID2D1PathGeometry* g = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&g))) return;
    ID2D1GeometrySink* k = nullptr;
    if (SUCCEEDED(g->Open(&k))) {
        k->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
        k->AddLines(pts + 1, n - 1);
        k->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        k->Close(); k->Release();
        RB->SetColor(Col(c, a));
        R->DrawGeometry(g, RB, w, g_roundStroke);
    }
    g->Release();
}

static void PushClip(float x, float y, float w, float h) { R->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + h), D2D1_ANTIALIAS_MODE_ALIASED); }
static void PopClip() { R->PopAxisAlignedClip(); }

// ── 글자 ─────────────────────────────────────────────────────────────
// ha: 0 왼쪽 1 가운데 2 오른쪽 / va: 0 위 1 가운데 2 아래
static void Text(const std::wstring& s, float x, float y, float w, float h, float size, int weight, UINT32 c,
                 int ha = 0, int va = 1, bool wrap = false, const wchar_t* family = FONT_UI, float a = 1.f) {
    if (s.empty()) return;
    TextFmt tf = GetFmt(size, weight, family);
    if (!tf.f) return;
    tf.f->SetTextAlignment(ha == 0 ? DWRITE_TEXT_ALIGNMENT_LEADING : ha == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
    tf.f->SetParagraphAlignment(va == 0 ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : va == 1 ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    tf.f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING tr = { wrap ? DWRITE_TRIMMING_GRANULARITY_WORD : DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    tf.f->SetTrimming(&tr, tf.sign);
    RB->SetColor(Col(c, a));
    R->DrawText(s.c_str(), (UINT32)s.size(), tf.f, D2D1::RectF(x, y, x + w, y + h), RB, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static float TextW(const std::wstring& s, float size, int weight, const wchar_t* family = FONT_UI) {
    TextFmt tf = GetFmt(size, weight, family);
    if (!tf.f || s.empty()) return 0;
    tf.f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout* lay = nullptr;
    float w = 0;
    if (SUCCEEDED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), tf.f, 100000.f, 1000.f, &lay))) {
        DWRITE_TEXT_METRICS m;
        if (SUCCEEDED(lay->GetMetrics(&m))) w = m.widthIncludingTrailingWhitespace;
        lay->Release();
    }
    return w;
}

// ── 공통 부품 ────────────────────────────────────────────────────────
static void AddHot(float x, float y, float w, float h, int id) { g_hots.push_back({ x, y, w, h, id }); }

static int HitTest(float x, float y) {
    for (int i = (int)g_hots.size() - 1; i >= 0; i--) {
        auto& h = g_hots[(size_t)i];
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return h.id;
    }
    return 0;
}

static void Card(float x, float y, float w, float h) {
    FillRR(x, y, w, h, 14, C_CARD);
    StrokeRR(x, y, w, h, 14, 0xFFFFFF, 1.f, 0.045f);
}

static void Toggle(float x, float y, bool on, bool hover, bool enabled = true) {
    UINT32 tc = !enabled ? C_CARD3 : on ? (hover ? C_GREENHI : C_GREEN) : (hover ? 0x3A3A3A : 0x2E2E2E);
    FillRR(x, y, 44, 24, 12, tc);
    float kx = on ? x + 32 : x + 12;
    FillCircle(kx, y + 12, 9, on ? 0x0B0B0B : (enabled ? 0x9A9A9A : 0x555555));
}

static void Chip(float x, float y, const std::wstring& t, UINT32 fg, UINT32 bg, float bgA = 1.f, float h = 22, float size = 11.f) {
    float w = TextW(t, size, 700) + 18;
    FillRR(x, y, w, h, h / 2, bg, bgA);
    Text(t, x, y, w, h, size, 700, fg, 1);
}

static float ChipW(const std::wstring& t, float size = 11.f) { return TextW(t, size, 700) + 18; }

static void Button(float x, float y, float w, float h, const std::wstring& t, int id, bool primary, bool enabled = true, float size = 13.f) {
    bool hv = enabled && g_hover == id;
    bool pr = enabled && g_down == id && hv;
    UINT32 bg = !enabled ? 0x1A1A1A : primary ? (pr ? C_GREENDK : hv ? C_GREENHI : C_GREEN) : (pr ? 0x202020 : hv ? 0x2C2C2C : C_CARD2);
    UINT32 fg = !enabled ? 0x555555 : primary ? C_INK : (hv ? C_TEXT : 0xDADADA);
    FillRR(x, y, w, h, h / 2, bg);
    if (!primary && enabled) StrokeRR(x, y, w, h, h / 2, 0xFFFFFF, 1.f, hv ? 0.10f : 0.06f);
    Text(t, x, y, w, h, size, 700, fg, 1);
    if (enabled) AddHot(x, y, w, h, id);
}

// 아이콘 (선 그림)
static void IconBolt(float cx, float cy, float s, UINT32 c, float a = 1.f) {
    D2D1_POINT_2F p[7] = {
        { cx + 0.10f * s, cy - 0.40f * s }, { cx - 0.22f * s, cy + 0.05f * s }, { cx - 0.03f * s, cy + 0.05f * s },
        { cx - 0.12f * s, cy + 0.40f * s }, { cx + 0.24f * s, cy - 0.08f * s }, { cx + 0.04f * s, cy - 0.08f * s },
        { cx + 0.16f * s, cy - 0.40f * s } };
    FillPoly(p, 7, c, a);
}

static void IconGrid(float cx, float cy, float s, UINT32 c) {
    float g = s * 0.12f, w = (s - g) / 2;
    float x0 = cx - s / 2, y0 = cy - s / 2;
    FillRR(x0, y0, w, w, 2, c); FillRR(x0 + w + g, y0, w, w * 0.7f, 2, c);
    FillRR(x0, y0 + w + g, w, w * 0.7f, 2, c); FillRR(x0 + w + g, y0 + w * 0.7f + g, w, w * 1.0f, 2, c);
}

static void IconChart(float cx, float cy, float s, UINT32 c) {
    float x0 = cx - s / 2, y0 = cy - s / 2;
    D2D1_POINT_2F p[5] = { { x0, y0 + s * 0.78f }, { x0 + s * 0.3f, y0 + s * 0.42f }, { x0 + s * 0.55f, y0 + s * 0.62f },
                           { x0 + s * 0.8f, y0 + s * 0.18f }, { x0 + s, y0 + s * 0.3f } };
    StrokePoly(p, 5, c, 2.f, false);
    Line(x0, y0 + s, x0 + s, y0 + s, c, 2.f);
}

static void IconSliders(float cx, float cy, float s, UINT32 c) {
    float x0 = cx - s / 2, y0 = cy - s / 2;
    float ys[3] = { 0.15f, 0.5f, 0.85f }, ks[3] = { 0.68f, 0.3f, 0.58f };
    for (int i = 0; i < 3; i++) {
        float y = y0 + s * ys[i];
        Line(x0, y, x0 + s, y, c, 2.f);
        FillCircle(x0 + s * ks[i], y, s * 0.13f, C_SIDE);
        StrokeCircle(x0 + s * ks[i], y, s * 0.12f, c, 2.f);
    }
}

static void IconCheck(float cx, float cy, float s, UINT32 c, float w = 2.2f) {
    D2D1_POINT_2F p[3] = { { cx - s * 0.32f, cy + s * 0.02f }, { cx - s * 0.08f, cy + s * 0.26f }, { cx + s * 0.34f, cy - s * 0.22f } };
    StrokePoly(p, 3, c, w, false);
}
