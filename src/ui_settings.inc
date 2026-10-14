// =====================================================================
//  화면: 설정 + 팝업 (부스트 확인 / 업데이트 / 기록)
// =====================================================================
static void ToggleRow(float x, float y, float w, const wchar_t* title, const std::wstring& desc, bool on, int id, bool enabled = true) {
    bool hv = enabled && g_hover == id;
    if (hv) FillRR(x - 12, y - 4, w + 24, 60, 10, C_CARD2);
    Text(title, x, y + 2, w - 70, 24, 14, 700, enabled ? C_TEXT : C_FAINT);
    Text(desc, x, y + 26, w - 70, 22, 11.5f, 400, C_FAINT);
    Toggle(x + w - 44, y + 12, on, hv, enabled);
    if (enabled) AddHot(x - 12, y - 4, w + 24, 60, id);
}

static void DrawSettings() {
    // ── 왼쪽: 일반
    float x = 244, y = 24, w = 450, h = 672;
    Card(x, y, w, h);
    Text(L"일반", x + 24, y + 18, 200, 30, 17, 700, C_TEXT);
    float ix = x + 24, iw = w - 48;
    ToggleRow(ix, y + 64, iw, L"윈도우 시작 시 실행",
              g_isAdmin ? L"로그인하면 트레이에서 시작 (측정 권한 포함)" : L"로그인하면 트레이에서 조용히 시작", g_startupOn, H_SET_STARTUP);
    ToggleRow(ix, y + 128, iw, L"게임 끝나면 자동 복원", L"정리한 앱과 전원 모드를 원래대로", g_set.autoRestore, H_SET_AUTORESTORE);
    ToggleRow(ix, y + 192, iw, L"안 꺼지는 앱 강제 종료", L"트레이에 숨는 앱까지 종료", g_set.forceKill, H_SET_FORCE);
    ToggleRow(ix, y + 256, iw, L"부스트 전에 확인", L"정리할 앱 목록을 한 번 보여줘요", g_set.confirmBoost, H_SET_CONFIRM);

    Text(L"감시 게임", ix, y + 330, 200, 22, 13, 700, C_TEXT);
    FillRR(ix - 4, y + 356, iw + 8, 42, 10, C_CARD2);
    StrokeRR(ix - 4, y + 356, iw + 8, 42, 10, g_editFocus ? C_GREEN : 0xFFFFFF, 1.f, g_editFocus ? 0.9f : 0.07f);
    Text(L"이 게임이 꺼지면 부스트를 자동으로 풀어요. 실행 파일 이름을 쉼표로 구분 (.exe 빼고)",
         ix, y + 404, iw, 36, 11, 400, C_FAINT, 0, 0, true);

    Line(x + 16, y + 456, x + w - 16, y + 456, 0xFFFFFF, 1.f, 0.05f);
    // 업데이트
    Text(std::wstring(L"버전 ") + APP_VER, ix, y + 472, 120, 34, 13.5f, 700, C_TEXT);
    std::wstring us; UINT32 uc = C_DIM;
    switch (g_updState) {
    case UPD_CHECKING: us = L"확인 중..."; break;
    case UPD_LATEST: us = L"최신 버전"; uc = C_GREEN; break;
    case UPD_AVAILABLE: us = L"새 버전 " + g_updVer; uc = C_GREEN; break;
    case UPD_FAILED: us = L"확인 실패"; uc = C_WARN; break;
    case UPD_DOWNLOADING: us = L"받는 중 " + std::to_wstring(g_updProgress) + L"%"; uc = C_GREEN; break;
    }
    Text(us, ix + 90, y + 472, 160, 34, 12, 400, uc);
    bool avail = g_updState == UPD_AVAILABLE;
    Button(ix + iw - 124, y + 474, 124, 32, avail ? L"업데이트" : L"업데이트 확인", H_UPD_CHECK, avail,
           g_updState != UPD_CHECKING && g_updState != UPD_DOWNLOADING, 12.5f);
    Text(L"기록·설정 폴더", ix, y + 522, 200, 34, 13.5f, 700, C_TEXT);
    Button(ix + iw - 124, y + 524, 124, 32, L"폴더 열기", H_OPEN_FOLDER, false, true, 12.5f);
    Text(L"작업 기록", ix, y + 572, 200, 34, 13.5f, 700, C_TEXT);
    Button(ix + iw - 124, y + 574, 124, 32, L"기록 보기", H_OPEN_LOG, false, true, 12.5f);
    Text(L"프레임 측정: Intel PresentMon (MIT 라이선스)", ix, y + 628, iw, 20, 11, 400, C_FAINT);

    // ── 오른쪽: 오버레이 · 측정
    x = 710; w = 446;
    Card(x, y, w, h);
    Text(L"측정 · 오버레이", x + 24, y + 18, 300, 30, 17, 700, C_TEXT);
    ix = x + 24; iw = w - 48;
    std::wstring md;
    if (!g_isAdmin) md = L"관리자 권한이 필요해요";
    else if (g_toolState == TOOL_DOWNLOADING) md = L"측정 도구 받는 중 " + std::to_wstring(g_toolProgress) + L"%";
    else if (g_toolState != TOOL_READY) md = L"측정 도구(1MB)를 처음 한 번 받아야 해요";
    else md = L"전체 화면 게임과 감시 게임을 자동으로 측정";
    ToggleRow(ix, y + 64, iw, L"프레임 자동 측정", md, g_set.monitor, H_SET_MONITOR);
    if (g_set.monitor && !g_isAdmin) Button(ix + iw - 210, y + 76, 154, 30, L"관리자로 다시 시작", H_ADMIN, true, true, 12);
    else if (g_set.monitor && g_isAdmin && (g_toolState == TOOL_MISSING || g_toolState == TOOL_FAILED))
        Button(ix + iw - 140, y + 76, 84, 30, L"받기", H_TOOL, true, true, 12);
    ToggleRow(ix, y + 128, iw, L"게임 오버레이", L"게임 화면 위에 FPS 표시 (안티치트 안전)", g_set.overlay, H_SET_OVERLAY);

    Text(L"표시 내용", ix, y + 200, 200, 22, 13, 700, C_TEXT);
    const wchar_t* labels[3] = { L"간단히 · FPS", L"기본", L"자세히" };
    DrawSegmented(ix, y + 228, iw, 36, labels, 3, g_set.overlayMode, H_SET_MODE0);

    Text(L"위치", ix, y + 282, 100, 22, 13, 700, C_TEXT);
    Text(L"화면에서 원하는 칸을 누르세요", ix + 50, y + 282, iw - 50, 22, 11.5f, 400, C_FAINT);
    // 모니터 모양 위치 선택기
    float mx = ix, my = y + 312, mw = iw, mh = 216;
    FillRR(mx, my, mw, mh, 10, 0x0E0E0E);
    StrokeRR(mx, my, mw, mh, 10, 0xFFFFFF, 1.f, 0.10f);
    FillRR(mx + mw / 2 - 36, my + mh, 72, 8, 2, 0x1E1E1E);   // 받침
    float cw = mw / 3, ch = mh / 3;
    for (int i = 0; i < 9; i++) {
        float zx = mx + (i % 3) * cw, zy = my + (i / 3) * ch;
        bool hv = g_hover == H_POS0 + i;
        if (hv) FillRR(zx + 4, zy + 4, cw - 8, ch - 8, 8, C_GREEN, 0.08f);
        AddHot(zx, zy, cw, ch, H_POS0 + i);
    }
    float ow, oh; OverlaySizeBase(g_set.overlayMode, ow, oh);
    ow *= 0.42f; oh *= 0.42f;
    float ox, oy;
    if (g_set.overlayPos == POS_CUSTOM) { ox = mx + 8 + g_set.overlayX * (mw - 16 - ow); oy = my + 8 + g_set.overlayY * (mh - 16 - oh); }
    else {
        int c = g_set.overlayPos % 3, r = g_set.overlayPos / 3;
        ox = c == 0 ? mx + 8 : c == 1 ? mx + (mw - ow) / 2 : mx + mw - 8 - ow;
        oy = r == 0 ? my + 8 : r == 1 ? my + (mh - oh) / 2 : my + mh - 8 - oh;
    }
    FillRR(ox, oy, ow, oh, 4, 0x000000, 0.9f);
    StrokeRR(ox, oy, ow, oh, 4, C_GREEN, 1.2f);
    Text(L"144", ox + 4, oy, ow - 8, std::min(oh, 22.f), 11, 700, C_GREEN, 0, 1, false, FONT_NUM);

    Button(ix, y + 548, 150, 34, g_ovlMoving ? L"옮기기 완료" : L"직접 옮기기", H_OVL_MOVE, g_ovlMoving, true, 12.5f);
    Text(g_ovlMoving ? L"화면의 오버레이를 끌어서 옮기세요" : L"지금: " + PosName(g_set.overlayPos), ix + 164, y + 548, iw - 164, 34, 11.5f, 400, g_ovlMoving ? C_GREEN : C_DIM);
    Text(L"오버레이는 게임에 코드를 넣지 않는 바깥 창이에요. 일부 게임은 오버레이 때문에 Present Mode가 Composed로 바뀔 수 있으니 '자세히'에서 확인하세요.",
         ix, y + 598, iw, 54, 11, 400, C_FAINT, 0, 0, true);
}

// ---------------------------------------------------------------------
//  팝업
// ---------------------------------------------------------------------
static void ModalBackdrop() {
    FillRR(0, 0, BASE_W, BASE_H, 0, 0x000000, 0.66f);
    AddHot(0, 0, BASE_W, BASE_H, H_MODAL_BG);
}

static void DrawConfirmModal() {
    ModalBackdrop();
    size_t n = g_confirmTargets.size();
    size_t show = std::min<size_t>(n, 7);
    float lines = (float)show + (n > show ? 1 : 0);
    float w = 460, h = 250 + lines * 28;
    float x = (BASE_W - w) / 2, y = (BASE_H - h) / 2;
    FillRR(x, y, w, h, 16, C_CARD);
    StrokeRR(x, y, w, h, 16, 0xFFFFFF, 1.f, 0.08f);
    Text(n ? L"앱 " + std::to_wstring(n) + L"개를 정리하고 부스트할까요?" : std::wstring(L"부스트할까요?"), x + 28, y + 24, w - 56, 30, 18, 700, C_TEXT);
    Text(L"저장 안 한 작업이 있으면 먼저 저장하세요. 게임이 끝나면 다시 켜져요.", x + 28, y + 56, w - 56, 22, 12, 400, C_DIM);
    float lx = x + 20, ly = y + 92, lw = w - 40, lh = lines * 28 + 16;
    FillRR(lx, ly, lw, lh, 10, C_CARD2);
    for (size_t i = 0; i < show; i++) {
        float ry = ly + 8 + i * 28;
        FillCircle(lx + 18, ry + 14, 3, C_GREEN);
        Text(g_confirmTargets[i].display, lx + 30, ry, lw - 140, 28, 13, 400, C_TEXT);
        Text(FmtNum(g_confirmTargets[i].memMB) + L" MB", lx + lw - 110, ry, 94, 28, 12, 700, C_DIM, 2, 1, false, FONT_NUM);
    }
    if (n > show) Text(L"외 " + std::to_wstring(n - show) + L"개", lx + 30, ly + 8 + show * 28, 200, 28, 12.5f, 400, C_DIM);
    float cy = ly + lh + 14;
    bool nh = g_hover == H_CONF_NOASK;
    if (g_confirmNoAsk) { FillRR(x + 28, cy + 3, 18, 18, 5, C_GREEN); IconCheck(x + 37, cy + 12, 11, C_INK, 2.f); }
    else StrokeRR(x + 28, cy + 3, 18, 18, 5, nh ? C_DIM : 0x555555, 1.5f);
    Text(L"다음부터 묻지 않기", x + 54, cy, 200, 24, 12.5f, 400, nh ? C_TEXT : C_DIM);
    AddHot(x + 24, cy, 200, 24, H_CONF_NOASK);
    float by = y + h - 66;
    Button(x + 20, by, 206, 46, L"취소", H_CONF_CANCEL, false, true, 14);
    Button(x + 234, by, 206, 46, L"부스트 시작", H_CONF_OK, true, true, 14);
}

static std::wstring CleanNotes(const std::wstring& n) {
    std::wstring out;
    for (auto& raw : SplitLines(n)) {
        std::wstring l = Trim(raw);
        while (!l.empty() && (l[0] == L'#' || l[0] == L'>')) l = Trim(l.substr(1));
        if (l.rfind(L"- ", 0) == 0 || l.rfind(L"* ", 0) == 0) l = L"• " + l.substr(2);
        std::wstring c;
        for (wchar_t ch : l) if (ch != L'*' && ch != L'`') c += ch;
        out += c + L"\n";
    }
    while (!out.empty() && out.back() == L'\n') out.pop_back();
    return out.empty() ? L"패치노트가 없어요." : out;
}

static void DrawUpdateModal() {
    ModalBackdrop();
    float w = 500, h = 450, x = (BASE_W - w) / 2, y = (BASE_H - h) / 2;
    FillRR(x, y, w, h, 16, C_CARD);
    StrokeRR(x, y, w, h, 16, 0xFFFFFF, 1.f, 0.08f);
    Chip(x + 28, y + 26, L"NEW", C_INK, C_GREEN, 1.f, 22, 10.5f);
    Text(L"새 버전 " + g_updVer, x + 28, y + 56, w - 56, 34, 21, 700, C_TEXT);
    Text(std::wstring(L"현재 ") + APP_VER + L" → " + g_updVer + L" · 설정과 기록은 그대로 유지돼요", x + 28, y + 92, w - 56, 22, 12, 400, C_DIM);
    float bx = x + 20, by = y + 126, bw = w - 40, bh = 220;
    FillRR(bx, by, bw, bh, 10, C_CARD2);
    PushClip(bx, by + 6, bw, bh - 12);
    Text(CleanNotes(g_updNotes), bx + 18, by + 12, bw - 36, bh - 24, 12.5f, 400, C_TEXT, 0, 0, true);
    PopClip();
    bool busy = g_updState == UPD_DOWNLOADING || g_updState == UPD_APPLYING;
    if (busy) {
        FillRR(x + 20, y + 358, w - 40, 6, 3, C_CARD3);
        FillRR(x + 20, y + 358, (w - 40) * g_updProgress / 100.f, 6, 3, C_GREEN);
    } else if (!g_updError.empty() && g_updState == UPD_AVAILABLE) {
        Text(L"업데이트 실패: " + g_updError, x + 20, y + 350, w - 40, 22, 11.5f, 400, C_ERR);
    }
    float yb = y + h - 66;
    Button(x + 20, yb, 226, 46, L"나중에", H_UPD_LATER, false, !busy, 14);
    std::wstring ok = g_updState == UPD_DOWNLOADING ? L"받는 중 " + std::to_wstring(g_updProgress) + L"%"
                    : g_updState == UPD_APPLYING ? std::wstring(L"다시 시작하는 중...") : std::wstring(L"지금 업데이트");
    Button(x + 254, yb, 226, 46, ok, H_UPD_OK, true, !busy && !g_busy, 14);
}

static const float LG_X = 210, LG_Y = 150, LG_W = 760, LG_H = 380;

static void DrawLogModal() {
    ModalBackdrop();
    float w = 800, h = 540, x = (BASE_W - w) / 2, y = (BASE_H - h) / 2;
    FillRR(x, y, w, h, 16, C_CARD);
    StrokeRR(x, y, w, h, 16, 0xFFFFFF, 1.f, 0.08f);
    Text(L"작업 기록", x + 28, y + 22, 300, 30, 18, 700, C_TEXT);
    Button(x + w - 200, y + 22, 80, 32, L"지우기", H_LOG_CLEAR, false, true, 12);
    Button(x + w - 108, y + 22, 80, 32, L"닫기", H_LOG_CLOSE, true, true, 12);
    float lx = x + 20, ly = y + 70, lw = w - 40, lh = h - 90;
    FillRR(lx, ly, lw, lh, 10, 0x0F0F0F);
    if (g_logs.empty()) { Text(L"아직 기록이 없어요", lx, ly, lw, lh, 13, 400, C_FAINT, 1); return; }
    float line = 24;
    float content = g_logs.size() * line + 16;
    float maxs = std::max(0.f, content - lh);
    g_logScroll = std::max(0.f, std::min(g_logScroll, maxs));
    PushClip(lx, ly + 4, lw, lh - 8);
    float bottom = ly + lh - 8 + g_logScroll;
    for (int i = (int)g_logs.size() - 1; i >= 0; i--) {
        float ry = bottom - (g_logs.size() - i) * line;
        if (ry + line < ly) break;
        if (ry > ly + lh) continue;
        auto& l = g_logs[(size_t)i];
        UINT32 c = l.level == L_OK ? C_GREEN : l.level == L_WARN ? C_WARN : l.level == L_ERR ? C_ERR : C_TEXT;
        std::wstring tm = l.text.size() > 10 ? l.text.substr(0, 8) : L"";
        std::wstring body = l.text.size() > 10 ? l.text.substr(10) : l.text;
        Text(tm, lx + 16, ry, 80, line, 11.5f, 400, C_FAINT, 0, 1, false, FONT_NUM);
        Text(body, lx + 92, ry, lw - 108, line, 12.5f, 400, c);
    }
    PopClip();
}
