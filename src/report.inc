// =====================================================================
//  문제 보고 (유저가 버튼을 눌렀을 때만, 내용을 보여주고 동의하면 디스코드로 전송)
//  - 이름 · 컴퓨터 이름 · 사용자 폴더 경로는 빼고 보냄
// =====================================================================
#define WM_APP_REPORT (WM_APP + 20)

// 디스코드 웹훅 주소 (단순 문자열 검색에 안 걸리게 XOR로 저장)
static const unsigned char kRepKey = 0x5A;
static const unsigned char kRepUrl[] = {
    0x32, 0x2E, 0x2E, 0x2A, 0x29, 0x60, 0x75, 0x75, 0x3E, 0x33, 0x29, 0x39, 0x35, 0x28, 0x3E, 0x74,
    0x39, 0x35, 0x37, 0x75, 0x3B, 0x2A, 0x33, 0x75, 0x2D, 0x3F, 0x38, 0x32, 0x35, 0x35, 0x31, 0x29,
    0x75, 0x6B, 0x6F, 0x6F, 0x63, 0x63, 0x6B, 0x62, 0x68, 0x62, 0x69, 0x6C, 0x6D, 0x69, 0x6D, 0x6A,
    0x6C, 0x6C, 0x6C, 0x62, 0x75, 0x20, 0x05, 0x77, 0x17, 0x20, 0x2A, 0x3C, 0x15, 0x6E, 0x6A, 0x69,
    0x30, 0x0F, 0x1F, 0x77, 0x1E, 0x28, 0x29, 0x03, 0x11, 0x77, 0x2C, 0x02, 0x2B, 0x1C, 0x32, 0x22,
    0x1E, 0x6A, 0x2C, 0x14, 0x28, 0x32, 0x09, 0x29, 0x1E, 0x2F, 0x3B, 0x0D, 0x0B, 0x3B, 0x1C, 0x16,
    0x0A, 0x2F, 0x34, 0x14, 0x3D, 0x2A, 0x0E, 0x6B, 0x0C, 0x23, 0x3F, 0x1F, 0x3F, 0x63, 0x33, 0x02,
    0x35, 0x33, 0x1F, 0x10, 0x02, 0x36, 0x36, 0x33, 0x0E, 0x00
};

static std::wstring ReportUrl() {
    wchar_t env[1024] = {};
    if (GetEnvironmentVariableW(L"FB_REPORT_URL", env, 1024) > 0) return env;   // 테스트용
    std::string s;
    for (size_t i = 0; i + 1 < sizeof(kRepUrl); i++) s += (char)(kRepUrl[i] ^ kRepKey);
    return FromUtf8(s);
}
static bool ReportAvailable() { return !ReportUrl().empty(); }

enum { REP_IDLE = 0, REP_SENDING, REP_SENT, REP_FAILED };
static int g_repState = REP_IDLE;
static int g_repCode = 0;
static std::wstring g_repText;      // 보낼 내용 (미리보기와 같음)
static float g_repScroll = 0;
static DWORD g_repLastSent = 0;
static HWND g_repEdit;

// ── 개인 정보 지우기 ─────────────────────────────────────────────────
static void ReplaceAllI(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    if (from.size() < 2) return;
    std::wstring ls = Lower(s), lf = Lower(from);
    std::wstring out;
    size_t pos = 0;
    for (;;) {
        size_t f = ls.find(lf, pos);
        if (f == std::wstring::npos) { out += s.substr(pos); break; }
        out += s.substr(pos, f - pos) + to;
        pos = f + from.size();
    }
    s = out;
}
static std::wstring Anonymize(std::wstring s) {
    wchar_t b[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", b, MAX_PATH)) ReplaceAllI(s, b, L"%USERPROFILE%");
    if (GetEnvironmentVariableW(L"USERNAME", b, MAX_PATH)) ReplaceAllI(s, b, L"<user>");
    if (GetEnvironmentVariableW(L"COMPUTERNAME", b, MAX_PATH)) ReplaceAllI(s, b, L"<pc>");
    return s;
}

// ── 시스템 정보 ─────────────────────────────────────────────────────
static std::wstring RegStr(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD type = 0; std::vector<BYTE> d;
    if (!RegRead(root, path, name, type, d)) return L"";
    if (type == REG_DWORD && d.size() >= 4) return std::to_wstring(*(const DWORD*)d.data());
    return Trim(SzFromData(d));
}

static std::wstring WindowsVersion() {
    const wchar_t* k = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring build = RegStr(HKEY_LOCAL_MACHINE, k, L"CurrentBuild");
    std::wstring name = _wtoi(build.c_str()) >= 22000 ? L"Windows 11" : L"Windows 10";
    return name + L" " + RegStr(HKEY_LOCAL_MACHINE, k, L"DisplayVersion") + L" (빌드 " + build + L"." + RegStr(HKEY_LOCAL_MACHINE, k, L"UBR") + L")";
}

// 그래픽 카드 이름 + 드라이버 버전 (NVIDIA는 566.14 같은 형식도 같이)
static std::wstring GpuDetails() {
    std::wstring out;
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) return L"(알 수 없음)";
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            LARGE_INTEGER v = {};
            std::wstring ver;
            if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v))) {
                unsigned p1 = HIWORD(v.HighPart), p2 = LOWORD(v.HighPart), p3 = HIWORD(v.LowPart), p4 = LOWORD(v.LowPart);
                wchar_t b[96];
                swprintf(b, 96, L"%u.%u.%u.%u", p1, p2, p3, p4);
                ver = b;
                if (d.VendorId == 0x10DE) {   // NVIDIA: 마지막 5자리 = 드라이버 버전
                    unsigned n = (p3 % 10) * 10000 + p4;
                    swprintf(b, 96, L" (NVIDIA %u.%02u)", n / 100, n % 100);
                    ver += b;
                }
            }
            const wchar_t* vendor = d.VendorId == 0x10DE ? L"NVIDIA" : d.VendorId == 0x1002 ? L"AMD" : d.VendorId == 0x8086 ? L"Intel" : L"기타";
            out += L"  - " + Trim(d.Description) + L" [" + vendor + L"] " + std::to_wstring((unsigned)(d.DedicatedVideoMemory >> 20)) + L" MB · 드라이버 " + ver + L"\n";
        }
        a->Release();
    }
    f->Release();
    return out.empty() ? L"  (없음)\n" : out;
}

static std::wstring DisplayDetails() {
    std::wstring out;
    DISPLAY_DEVICEW dd; dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); i++) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        DEVMODEW dm; ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) continue;
        wchar_t b[128];
        swprintf(b, 128, L"  - %lux%lu @ %luHz%ls\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency,
                 (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? L" (주 모니터)" : L"");
        out += b;
        dd.cb = sizeof(dd);
    }
    return out.empty() ? L"  (알 수 없음)\n" : out;
}

static std::wstring LogTail(size_t maxLines) {
    auto lines = SplitLines(ReadAllText(g_logFile));
    size_t from = lines.size() > maxLines ? lines.size() - maxLines : 0;
    std::wstring out;
    for (size_t i = from; i < lines.size(); i++) if (!Trim(lines[i]).empty()) out += lines[i] + L"\n";
    return out.empty() ? L"(없음)\n" : out;
}

static std::wstring BuildReport() {
    std::wstring r;
    r += L"[Frame Booster 문제 보고]\n";
    r += std::wstring(L"앱 버전: ") + APP_VER + L"\n";
    r += L"윈도우: " + WindowsVersion() + L"\n";
    r += L"CPU: " + g_cpuName + L" · 논리 코어 " + std::to_wstring(g_cores) + (g_isX3D ? L" · X3D" : L"") + L"\n";
    r += L"RAM: " + std::to_wstring(g_ramGB) + L" GB\n";
    r += L"그래픽 카드:\n" + GpuDetails();
    r += L"모니터:\n" + DisplayDetails();
    r += std::wstring(L"권한: ") + (g_isAdmin ? L"관리자" : L"일반") + L" · 사양 판정: " + (g_lowEnd ? L"저사양" : L"고사양") + L"\n";
    r += L"전원 계획: " + g_currentPlanName + L" · 부스트 전원 모드: " + PowerModeName(EffectivePowerMode()) + L"\n";
    const wchar_t* tool = g_toolState == TOOL_READY ? L"준비됨" : g_toolState == TOOL_DOWNLOADING ? L"받는 중" : g_toolState == TOOL_FAILED ? L"받기 실패" : L"없음";
    r += std::wstring(L"측정: ") + (g_set.monitor ? L"켜짐" : L"꺼짐") + L" · 측정 도구 " + tool + L"\n";
    if (!g_monErr.empty()) r += L"마지막 측정 오류: " + g_monErr + L"\n";
    static const wchar_t* om[] = { L"간단히", L"기본", L"자세히" };
    r += std::wstring(L"오버레이: ") + (g_set.overlay ? om[std::max(0, std::min(2, g_set.overlayMode))] : L"꺼짐") + L"\n";
    r += std::wstring(L"게임 녹화 끄기: ") + (g_twDvr ? L"적용" : L"안 함") + L" · 게임 모드: " + (g_twGameMode ? L"켜짐" : L"꺼짐") + L"\n";
    r += std::wstring(L"자동 복원: ") + (g_set.autoRestore ? L"켜짐" : L"꺼짐") + L" · 강제 종료: " + (g_set.forceKill ? L"켜짐" : L"꺼짐")
       + L" · 메모리 정리: " + (g_set.memClean ? L"켜짐" : L"꺼짐") + L"\n";
    r += L"부스트 상태: " + std::wstring(g_active ? L"켜짐" : L"꺼짐") + L"\n";

    r += L"\n[최근 플레이 기록]\n";
    if (g_sessions.empty()) r += L"(없음)\n";
    for (size_t i = 0; i < g_sessions.size() && i < 8; i++) {
        auto& s = g_sessions[i];
        r += L"  - " + FmtSessionTime(s.start) + L" · " + s.exe + L" · " + FmtDuration(s.durSec)
           + L" · 평균 " + FmtF(s.avgFps, 0) + L" / 1% " + FmtF(s.low1, 0) + L" / 0.1% " + FmtF(s.low01, 0)
           + L" · " + (s.mode.empty() ? L"Present Mode ?" : s.mode)
           + (s.boosted ? L" · BOOST(" + std::wstring(PowerModeName(s.power >= 0 ? s.power : 0)) + L")" : L"")
           + (s.latKind ? std::wstring(L" · ") + LatencyName(s.latKind) + L" 기록" : L"") + L"\n";
    }
    r += L"\n[작업 기록 (최근 150줄)]\n" + LogTail(150);
    return Anonymize(r);
}

// ── 전송 ─────────────────────────────────────────────────────────────
static std::string JsonEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n"; else if (c == '\r') {}
        else if (c < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

struct ReportJob { std::wstring url, summary, text; };

static DWORD WINAPI ReportThread(LPVOID p) {
    ReportJob* job = (ReportJob*)p;
    int status = 0;
    URL_COMPONENTS uc; ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {}, extra[1024] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    if (WinHttpCrackUrl(job->url.c_str(), 0, 0, &uc)) {
        std::string boundary = "----FrameBoosterReport7d2f9a";
        std::string payload = "{\"username\":\"Frame Booster 보고\",\"allowed_mentions\":{\"parse\":[]},\"content\":\"" + JsonEsc(ToUtf8(job->summary)) + "\"}";
        std::string file = "\xEF\xBB\xBF" + ToUtf8(job->text);
        std::string body;
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"payload_json\"\r\nContent-Type: application/json\r\n\r\n" + payload + "\r\n";
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"files[0]\"; filename=\"report.txt\"\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n" + file + "\r\n";
        body += "--" + boundary + "--\r\n";
        std::wstring hdr = L"Content-Type: multipart/form-data; boundary=" + FromUtf8(boundary) + L"\r\n";
        std::wstring ua = std::wstring(L"FrameBooster/") + APP_VER;
        HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (s) {
            WinHttpSetTimeouts(s, 8000, 8000, 15000, 15000);
            HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
            std::wstring fullPath = std::wstring(path) + extra;
            HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
            if (r && WinHttpSendRequest(r, hdr.c_str(), (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
                WinHttpReceiveResponse(r, nullptr)) {
                DWORD code = 0, sz = sizeof(code);
                WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
                status = (int)code;
            }
            if (r) WinHttpCloseHandle(r);
            if (c) WinHttpCloseHandle(c);
            WinHttpCloseHandle(s);
        }
    }
    PostMessageW(g_hwnd, WM_APP_REPORT, (WPARAM)status, 0);
    delete job;
    return 0;
}

static std::wstring ReportMemo() {
    if (!g_repEdit) return L"";
    int len = GetWindowTextLengthW(g_repEdit);
    std::wstring t(len + 1, L'\0');
    GetWindowTextW(g_repEdit, &t[0], len + 1);
    t.resize(len);
    return Trim(t).substr(0, 500);
}

static void StartReportSend() {
    if (g_repState == REP_SENDING || !ReportAvailable()) return;
    if (g_repLastSent && GetTickCount() - g_repLastSent < 60000) { g_repState = REP_FAILED; g_repCode = -1; Redraw(); return; }
    std::wstring memo = Anonymize(ReportMemo());
    ReportJob* job = new ReportJob();
    job->url = ReportUrl();
    job->text = (memo.empty() ? L"" : L"[유저 메모]\n" + memo + L"\n\n") + g_repText;
    std::wstring gpu = g_gpuName.empty() ? L"GPU ?" : g_gpuName;
    job->summary = std::wstring(L"**문제 보고** · ") + APP_VER + L" · " + gpu + L" · " + g_cpuName + L" · " + WindowsVersion()
                 + (memo.empty() ? L"" : L"\n> " + memo.substr(0, 300));
    job->summary = Anonymize(job->summary).substr(0, 1800);
    g_repState = REP_SENDING;
    HANDLE th = CreateThread(nullptr, 0, ReportThread, job, 0, nullptr);
    if (th) CloseHandle(th); else { delete job; g_repState = REP_FAILED; g_repCode = 0; }
    Redraw();
}
