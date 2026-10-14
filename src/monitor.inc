// =====================================================================
//  프레임 측정 (Intel PresentMon, MIT) + 세션 기록
//  - 측정 도구는 처음 한 번만 공식 배포처에서 받고 SHA-256으로 확인
//  - 게임(감시 목록 또는 전체 화면 앱)이 앞에 있으면 자동으로 측정
// =====================================================================
static const wchar_t* PM_URL = L"https://github.com/GameTechDev/PresentMon/releases/download/v2.6.0/PresentMon-2.6.0-x64.exe";
static const char* PM_SHA256 = "b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af";
static const wchar_t* PM_SESSION = L"FrameBoosterPM";

enum { TOOL_MISSING = 0, TOOL_DOWNLOADING, TOOL_READY, TOOL_FAILED };
static int g_toolState = TOOL_MISSING;
static int g_toolProgress = 0;

static std::wstring ToolPath() {
    wchar_t env[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"FB_PRESENTMON", env, MAX_PATH) > 0) return env;   // 테스트용
    return g_toolDir + L"\\PresentMon-2.6.0-x64.exe";
}

static std::string Sha256Hex(const void* data, size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr;
    UCHAR out[32] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
            const UCHAR* p = (const UCHAR*)data;
            size_t left = size;
            while (left) {
                ULONG n = (ULONG)std::min<size_t>(left, 1 << 20);
                BCryptHashData(h, (PUCHAR)p, n, 0);
                p += n; left -= n;
            }
            if (BCryptFinishHash(h, out, 32, 0) == 0) {
                char b[3];
                for (int i = 0; i < 32; i++) { snprintf(b, 3, "%02x", out[i]); hex += b; }
            }
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return hex;
}

static bool ToolFileOk(const std::wstring& path) {
    wchar_t env[8] = {};
    if (GetEnvironmentVariableW(L"FB_PRESENTMON", env, 8) > 0) return FileExists(path);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(f, nullptr), rd = 0;
    std::string buf(sz, '\0');
    BOOL ok = sz && ReadFile(f, &buf[0], sz, &rd, nullptr) && rd == sz;
    CloseHandle(f);
    return ok && Sha256Hex(buf.data(), buf.size()) == PM_SHA256;
}

static void CheckTool() { g_toolState = ToolFileOk(ToolPath()) ? TOOL_READY : TOOL_MISSING; }

static DWORD WINAPI ToolDownloadThread(LPVOID) {
    std::string data;
    int prog = 0;
    int code = HttpGet(PM_URL, data, false, &prog, WM_APP + 19);
    bool ok = false;
    if (code == 200 && Sha256Hex(data.data(), data.size()) == PM_SHA256) {
        std::wstring path = ToolPath();
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wr = 0;
            ok = WriteFile(f, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
            CloseHandle(f);
        }
    }
    PostMessageW(g_hwnd, WM_APP_TOOL, ok ? 1 : 0, code);
    return 0;
}

static void StartToolDownload() {
    if (g_toolState == TOOL_DOWNLOADING || g_toolState == TOOL_READY) return;
    g_toolState = TOOL_DOWNLOADING; g_toolProgress = 0;
    HANDLE th = CreateThread(nullptr, 0, ToolDownloadThread, nullptr, 0, nullptr);
    if (th) CloseHandle(th); else g_toolState = TOOL_FAILED;
}

// ---------------------------------------------------------------------
//  통계
// ---------------------------------------------------------------------
struct FrameStats {
    float avgFps = 0, low1 = 0, low01 = 0, avgMs = 0, maxMs = 0, durSec = 0;
    int stutters = 0;
    size_t frames = 0;
};

static FrameStats ComputeStats(const float* ft, size_t n) {
    FrameStats s;
    if (n < 2) return s;
    double sum = 0; float mx = 0;
    for (size_t i = 0; i < n; i++) { sum += ft[i]; mx = std::max(mx, ft[i]); }
    s.frames = n;
    s.durSec = (float)(sum / 1000.0);
    s.avgMs = (float)(sum / n);
    s.avgFps = (float)(n * 1000.0 / sum);
    s.maxMs = mx;
    std::vector<float> v(ft, ft + n);
    auto pct = [&](double p) {
        size_t k = std::min(n - 1, (size_t)(p * (n - 1)));
        std::nth_element(v.begin(), v.begin() + k, v.end());
        return v[k];
    };
    float med = pct(0.5);
    float p99 = pct(0.99);
    float p999 = pct(0.999);
    s.low1 = p99 > 0 ? 1000.f / p99 : 0;
    s.low01 = p999 > 0 ? 1000.f / p999 : 0;
    // 끊김: 중앙값의 2.5배 이상이면서 8ms 이상 튄 프레임
    for (size_t i = 0; i < n; i++) if (ft[i] > med * 2.5f && ft[i] - med > 8.f) s.stutters++;
    return s;
}

// ---------------------------------------------------------------------
//  세션 파일
// ---------------------------------------------------------------------
#pragma pack(push, 1)
struct FbsHeader {
    char magic[4];          // "FBS1"
    uint32_t version;
    uint64_t start;         // FILETIME
    uint32_t frames;
    float durSec, avgFps, low1, low01;
    uint8_t boosted, power, latKind, pad;   // latKind: 0 없음 1 PC 레이턴시(Reflex) 2 렌더 지연(추정)
    wchar_t exe[64];
    wchar_t mode[48];
};
#pragma pack(pop)

struct SessionInfo {
    std::wstring file, exe, mode;
    uint64_t start = 0;
    uint32_t frames = 0;
    float durSec = 0, avgFps = 0, low1 = 0, low01 = 0;
    bool boosted = false;
    int power = -1;
    int latKind = 0;
};

static std::vector<SessionInfo> g_sessions;   // 최신순

static bool ReadSessionHeader(const std::wstring& path, SessionInfo& si) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    FbsHeader h; DWORD rd = 0;
    bool ok = ReadFile(f, &h, sizeof(h), &rd, nullptr) && rd == sizeof(h) && !memcmp(h.magic, "FBS1", 4);
    CloseHandle(f);
    if (!ok) return false;
    h.exe[63] = 0; h.mode[47] = 0;
    si.file = path; si.exe = h.exe; si.mode = h.mode; si.start = h.start; si.frames = h.frames;
    si.durSec = h.durSec; si.avgFps = h.avgFps; si.low1 = h.low1; si.low01 = h.low01;
    si.boosted = h.boosted != 0; si.power = h.power == 255 ? -1 : h.power;
    si.latKind = h.version >= 2 ? h.latKind : 0;
    return true;
}

// 프레임타임 + (있으면) 프레임별 레이턴시 (0 = 값 없음)
static std::vector<float> LoadSessionFrames(const std::wstring& path, std::vector<float>* lat = nullptr) {
    std::vector<float> v;
    if (lat) lat->clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return v;
    FbsHeader h; DWORD rd = 0;
    if (ReadFile(f, &h, sizeof(h), &rd, nullptr) && rd == sizeof(h) && !memcmp(h.magic, "FBS1", 4) && h.frames < 20000000) {
        v.resize(h.frames);
        DWORD want = (DWORD)(h.frames * sizeof(float));
        if (!ReadFile(f, v.data(), want, &rd, nullptr) || rd != want) v.clear();
        else if (lat && h.version >= 2 && h.latKind) {
            lat->resize(h.frames);
            if (!ReadFile(f, lat->data(), want, &rd, nullptr) || rd != want) lat->clear();
        }
    }
    CloseHandle(f);
    return v;
}

static const wchar_t* LatencyName(int kind) { return kind == 1 ? L"PC 레이턴시" : L"렌더 지연"; }

static void LoadSessionIndex() {
    g_sessions.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_sessionDir + L"\\*.fbs").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        SessionInfo si;
        if (ReadSessionHeader(g_sessionDir + L"\\" + fd.cFileName, si)) g_sessions.push_back(si);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(g_sessions.begin(), g_sessions.end(), [](const SessionInfo& a, const SessionInfo& b) { return a.start > b.start; });
    // 앱 전체(실행 파일 + 측정 도구 + 설정·로그 + 기록) 100MB 이내: 기록은 남은 용량만큼 최신순으로 보관
    const unsigned long long total = 100ull * 1024 * 1024;
    unsigned long long used = FileSize(g_exePath) + FileSize(ToolPath()) + 1024 * 1024;   // 설정·로그 여유 1MB
    unsigned long long budget = total > used ? total - used : 0;
    unsigned long long sum = 0;
    size_t keep = 0;
    for (; keep < g_sessions.size() && keep < 60; keep++) {   // 최근 60개까지
        unsigned long long sz = FileSize(g_sessions[keep].file);
        if (sum + sz > budget) break;
        sum += sz;
    }
    while (g_sessions.size() > keep) { DeleteFileW(g_sessions.back().file.c_str()); g_sessions.pop_back(); }
}

static bool SaveSession(const std::wstring& exe, const std::wstring& mode, uint64_t start, bool boosted, int power, const std::vector<float>& ft,
                        int latKind, const std::vector<float>& lat) {
    if (lat.size() != ft.size()) latKind = 0;
    FrameStats st = ComputeStats(ft.data(), ft.size());
    FbsHeader h; ZeroMemory(&h, sizeof(h));
    memcpy(h.magic, "FBS1", 4);
    h.version = 2; h.start = start; h.latKind = (uint8_t)latKind; h.frames = (uint32_t)ft.size();
    h.durSec = st.durSec; h.avgFps = st.avgFps; h.low1 = st.low1; h.low01 = st.low01;
    h.boosted = boosted ? 1 : 0; h.power = power < 0 ? 255 : (uint8_t)power;
    wcsncpy(h.exe, exe.c_str(), 63);
    wcsncpy(h.mode, mode.c_str(), 47);
    FILETIME ft2; ft2.dwLowDateTime = (DWORD)start; ft2.dwHighDateTime = (DWORD)(start >> 32);
    SYSTEMTIME s; FILETIME lt; FileTimeToLocalFileTime(&ft2, &lt); FileTimeToSystemTime(&lt, &s);
    wchar_t name[128];
    swprintf(name, 128, L"%04d%02d%02d_%02d%02d%02d_", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, s.wSecond);
    std::wstring safe;
    for (wchar_t c : exe) safe += (iswalnum(c) || c == L'-' || c == L'_') ? c : L'_';
    std::wstring path = g_sessionDir + L"\\" + name + safe.substr(0, 40) + L".fbs";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(f, &h, sizeof(h), &wr, nullptr) && WriteFile(f, ft.data(), (DWORD)(ft.size() * sizeof(float)), &wr, nullptr);
    if (ok && latKind) ok = WriteFile(f, lat.data(), (DWORD)(lat.size() * sizeof(float)), &wr, nullptr);
    CloseHandle(f);
    return ok;
}

static std::wstring FmtSessionTime(uint64_t start, bool withDate = true) {
    FILETIME ft; ft.dwLowDateTime = (DWORD)start; ft.dwHighDateTime = (DWORD)(start >> 32);
    FILETIME lt; FileTimeToLocalFileTime(&ft, &lt);
    SYSTEMTIME s; FileTimeToSystemTime(&lt, &s);
    wchar_t b[64];
    if (withDate) swprintf(b, 64, L"%d월 %d일 %02d:%02d", s.wMonth, s.wDay, s.wHour, s.wMinute);
    else swprintf(b, 64, L"%02d:%02d", s.wHour, s.wMinute);
    return b;
}

static std::wstring FmtDuration(float sec) {
    int s = (int)(sec + 0.5f);
    wchar_t b[32];
    if (s >= 3600) swprintf(b, 32, L"%d시간 %d분", s / 3600, (s / 60) % 60);
    else if (s >= 60) swprintf(b, 32, L"%d분 %d초", s / 60, s % 60);
    else swprintf(b, 32, L"%d초", s);
    return b;
}

// 같은 게임의 최근 "부스트 없음" / "부스트" 기록
static bool FindEffect(const std::wstring& exe, const SessionInfo** off, const SessionInfo** on) {
    *off = *on = nullptr;
    for (auto& s : g_sessions) {
        if (!IEq(s.exe, exe)) continue;
        if (s.boosted && !*on) *on = &s;
        if (!s.boosted && !*off) *off = &s;
    }
    return *off && *on;
}

// ---------------------------------------------------------------------
//  실시간 측정
// ---------------------------------------------------------------------
struct LiveFrame { double t; float ms; float gpu; float pcl; float est; };   // pcl/est: 0 = 값 없음

struct MonState {
    uint32_t gen = 0;                 // 측정 세대 (이전 측정 스레드와 섞이지 않게)
    bool running = false;
    DWORD pid = 0;
    std::wstring exe;
    HANDLE pmProc = nullptr;
    std::deque<LiveFrame> recent;     // 최근 30초
    std::wstring mode;                // Present Mode (가장 최근)
    bool tearing = false;
    int sync = -1;
    DWORD lastFrameTick = 0;
    bool gotHeader = false;
    bool stopReq = false;             // 이 프로그램이 측정을 멈춤 (대상 변경·종료)
};
static MonState g_mon;
static CRITICAL_SECTION g_monCs;
static uint32_t g_monGen = 0;
static DWORD g_monCandidate = 0;
static int g_monCandidateTicks = 0;
// 측정 도구가 스스로 멈춘 경우 (권한·보안 프로그램 등): 같은 게임에 매초 다시 띄우지 않게
static DWORD g_monFailPid = 0;
static int g_monFails = 0;
static DWORD g_monRetryAt = 0;
static std::wstring g_monErr;      // 마지막 오류 (화면 표시용)

// 측정 결과 (측정 스레드 → 화면 스레드)
struct PmResult { std::wstring exe, err; DWORD pid; bool saved, failed, exited; double sec; };

struct LiveStats {
    bool active = false;
    std::wstring exe, mode;
    float fps = 0, low1 = 0, low01 = 0, ms = 0, gpuBusy = 0;
    float lat = 0; int latKind = 0;   // 1 PC 레이턴시 (Reflex 게임) / 2 렌더 지연 (추정)
    bool tearing = false;
    std::vector<float> spark;   // 최근 프레임타임 (오버레이/타일 그래프)
};

static std::vector<std::wstring> SplitCsv(const std::string& line) {
    std::vector<std::wstring> out;
    std::string cur;
    bool q = false;
    for (char c : line) {
        if (c == '"') { q = !q; continue; }
        if (c == ',' && !q) { out.push_back(FromUtf8(cur)); cur.clear(); continue; }
        if (c != '\r' && c != '\n') cur += c;
    }
    out.push_back(FromUtf8(cur));
    return out;
}

struct PmJob {
    HANDLE read; DWORD pid; uint32_t gen; std::wstring errPath;
    std::wstring exe, mode; uint64_t start; bool boosted; int power;
    std::vector<float> frames, pcl, est;
    size_t boostFrames = 0;   // 부스트 중에 나온 프레임 수 (도중에 켜고 끈 경우 다수결로 기록)
};

static bool ProcessAlive(DWORD pid);

// PresentMon 오류 출력 (파일로 받으면 UTF-16) → 첫 줄
static std::wstring ReadToolError(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    char buf[4096]; DWORD rd = 0;
    ReadFile(h, buf, sizeof(buf), &rd, nullptr);
    CloseHandle(h);
    std::wstring t;
    if (rd >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) t.assign((const wchar_t*)(buf + 2), (rd - 2) / 2);
    else t = FromUtf8(std::string(buf, rd));
    std::wstring out;
    for (auto& l : SplitLines(t)) {   // 의미 있는 줄만 이어 붙이기
        std::wstring x = Trim(l);
        if (x.empty()) continue;
        if (!out.empty()) out += L" ";
        out += x;
        if (out.size() > 160) break;
    }
    return out.substr(0, 200);
}

static DWORD WINAPI PmReaderThread(LPVOID p) {
    PmJob* job = (PmJob*)p;
    int cPid = -1, cMode = -1, cMs = -1, cT = -1, cGpu = -1, cTear = -1, cSync = -1, cPcl = -1, cBusy = -1, cUntil = -1;
    // 숫자 칸 읽기 ("NA"·빈칸은 -1)
    auto num = [](const std::vector<std::wstring>& f, int i) -> float {
        if (i < 0 || i >= (int)f.size() || f[i].empty() || !(iswdigit(f[i][0]) || f[i][0] == L'.' || f[i][0] == L'-')) return -1.f;
        return (float)_wtof(f[i].c_str());
    };
    std::string buf;
    char chunk[16384];
    DWORD rd = 0;
    while (ReadFile(job->read, chunk, sizeof(chunk), &rd, nullptr) && rd > 0) {
        buf.append(chunk, rd);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (line.empty()) continue;
            auto f = SplitCsv(line);
            if (cMs < 0) {   // 헤더 줄
                for (int i = 0; i < (int)f.size(); i++) {
                    std::wstring n = Lower(Trim(f[i]));
                    if (n == L"processid") cPid = i;
                    else if (n == L"presentmode") cMode = i;
                    else if (n == L"msbetweenpresents") cMs = i;
                    else if (n == L"cpustarttime" || n == L"timeinseconds") cT = i;
                    else if (n == L"msgpubusy") cGpu = i;
                    else if (n == L"allowstearing") cTear = i;
                    else if (n == L"syncinterval") cSync = i;
                    else if (n == L"mspclatency") cPcl = i;
                    else if (n == L"mscpubusy") cBusy = i;
                    else if (n == L"msuntildisplayed") cUntil = i;
                }
                if (cMs >= 0) { EnterCriticalSection(&g_monCs); if (g_mon.gen == job->gen) g_mon.gotHeader = true; LeaveCriticalSection(&g_monCs); }
                continue;
            }
            if ((int)f.size() <= cMs) continue;
            if (cPid >= 0 && (DWORD)_wtoi(f[cPid].c_str()) != job->pid) continue;
            float ms = (float)_wtof(f[cMs].c_str());
            if (!(ms > 0.05f && ms < 5000.f)) continue;
            double t = cT >= 0 && cT < (int)f.size() ? _wtof(f[cT].c_str()) : 0;
            float gpu = cGpu >= 0 && cGpu < (int)f.size() ? (float)_wtof(f[cGpu].c_str()) : 0;
            // PC 레이턴시: Reflex(PC Latency) 지원 게임만 값이 나옴
            float pcl = num(f, cPcl);
            if (!(pcl > 0.f && pcl < 1000.f)) pcl = 0;
            // 렌더 지연(추정): 프레임 시작 → 화면 표시 (CPU 작업 + Present 후 표시까지)
            float busy = num(f, cBusy), until = num(f, cUntil);
            float est = busy >= 0.f && until > 0.f && busy + until < 1000.f ? busy + until : 0.f;
            if (job->frames.size() < 4000000) {
                job->frames.push_back(ms); job->pcl.push_back(pcl); job->est.push_back(est);
                if (*(volatile bool*)&g_active) { job->boostFrames++; job->power = EffectivePowerMode(); }
            }
            if (cMode >= 0 && cMode < (int)f.size() && !f[cMode].empty() && f[cMode] != L"Unknown") job->mode = Trim(f[cMode]);
            EnterCriticalSection(&g_monCs);
            if (g_mon.gen == job->gen) {
                if (t <= 0) t = (g_mon.recent.empty() ? 0 : g_mon.recent.back().t) + ms / 1000.0;
                g_mon.recent.push_back({ t, ms, gpu, pcl, est });
                while (!g_mon.recent.empty() && g_mon.recent.front().t < t - 30.0) g_mon.recent.pop_front();
                g_mon.mode = job->mode;
                if (cTear >= 0 && cTear < (int)f.size()) g_mon.tearing = f[cTear] == L"1";
                if (cSync >= 0 && cSync < (int)f.size()) g_mon.sync = _wtoi(f[cSync].c_str());
                g_mon.lastFrameTick = GetTickCount();
            }
            LeaveCriticalSection(&g_monCs);
        }
    }
    CloseHandle(job->read);

    // 측정 끝: 누가 멈췄는지 확인 후 세션 저장
    bool exited = !ProcessAlive(job->pid);
    EnterCriticalSection(&g_monCs);
    bool stopped = g_mon.gen != job->gen || g_mon.stopReq;
    if (g_mon.gen == job->gen) { g_mon.running = false; g_mon.recent.clear(); }
    LeaveCriticalSection(&g_monCs);
    double total = 0;
    for (float v : job->frames) total += v;
    // 레이턴시 종류: PC 레이턴시 값이 충분히 있으면 그걸, 아니면 렌더 지연(추정)
    size_t nPcl = 0, nEst = 0;
    for (size_t i = 0; i < job->frames.size(); i++) { if (job->pcl[i] > 0) nPcl++; if (job->est[i] > 0) nEst++; }
    int latKind = nPcl * 20 >= job->frames.size() && nPcl > 0 ? 1 : nEst * 2 >= job->frames.size() && nEst > 0 ? 2 : 0;
    bool boosted = job->boostFrames * 2 >= job->frames.size() && job->boostFrames > 0;
    bool saved = total >= 20000.0 && SaveSession(job->exe, job->mode, job->start, boosted, boosted ? job->power : -1, job->frames,
                                                 latKind, latKind == 1 ? job->pcl : job->est);   // 20초 이상만
    PmResult* res = new PmResult();
    res->exe = job->exe; res->pid = job->pid; res->saved = saved; res->sec = total / 1000.0;
    res->exited = exited;
    res->failed = !stopped && !exited;   // 게임은 살아있는데 측정 도구가 스스로 끝남
    if (res->failed) {
        Sleep(300);   // 오류 출력이 파일에 다 써지도록
        res->err = ReadToolError(job->errPath);
    }
    if (!PostMessageW(g_hwnd, WM_APP_PM_DONE, 0, (LPARAM)res)) delete res;
    delete job;
    return 0;
}

static void StopPresentMonSession() {
    // 측정 세션 정리 (PresentMon 이 스스로 종료되게 함)
    std::wstring tool = ToolPath();
    if (FileExists(tool)) RunHidden(L"\"" + tool + L"\" --terminate_existing_session --session_name " + PM_SESSION, 5000);
}

static void StopMonitor() {
    EnterCriticalSection(&g_monCs);
    bool running = g_mon.running;
    HANDLE proc = g_mon.pmProc;
    g_mon.pmProc = nullptr;
    g_mon.stopReq = true;
    LeaveCriticalSection(&g_monCs);
    if (running) StopPresentMonSession();
    if (proc) {
        if (WaitForSingleObject(proc, 1500) == WAIT_TIMEOUT) TerminateProcess(proc, 0);
        CloseHandle(proc);
    }
}

static bool StartMonitor(DWORD pid, const std::wstring& exe) {
    StopMonitor();
    std::wstring tool = ToolPath();
    if (!FileExists(tool)) return false;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 16)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring errPath = g_toolDir + L"\\pm_error.txt";   // 측정 도구 오류 메시지 (실패 원인 기록용)
    HANDLE nul = CreateFileW(errPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nul == INVALID_HANDLE_VALUE) nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = nul; si.hStdInput = nullptr;
    PROCESS_INFORMATION pi;
    std::wstring cmd = L"\"" + tool + L"\" --process_id " + std::to_wstring(pid) +
        L" --output_stdout --no_console_stats --no_track_input --track_pc_latency --session_name " + PM_SESSION +
        L" --stop_existing_session --terminate_on_proc_exit";
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); return false; }
    CloseHandle(pi.hThread);
    FILETIME now; GetSystemTimeAsFileTime(&now);
    EnterCriticalSection(&g_monCs);
    g_mon = MonState();
    g_mon.gen = ++g_monGen;
    g_mon.running = true; g_mon.pid = pid; g_mon.exe = exe; g_mon.pmProc = pi.hProcess;
    g_mon.lastFrameTick = GetTickCount();
    PmJob* job = new PmJob();
    job->read = rd; job->pid = pid; job->gen = g_mon.gen; job->exe = exe; job->errPath = errPath;
    job->start = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    job->boosted = g_active;
    job->power = g_active ? EffectivePowerMode() : -1;
    LeaveCriticalSection(&g_monCs);
    HANDLE th = CreateThread(nullptr, 0, PmReaderThread, job, 0, nullptr);
    if (th) CloseHandle(th);
    AddLog(L_INFO, L"측정 시작: " + exe);
    return true;
}

static LiveStats GetLiveStats(int sparkCount = 0) {
    LiveStats ls;
    EnterCriticalSection(&g_monCs);
    ls.active = g_mon.running;
    ls.exe = g_mon.exe;
    ls.mode = g_mon.mode;
    ls.tearing = g_mon.tearing;
    if (!g_mon.recent.empty()) {
        double tEnd = g_mon.recent.back().t;
        int n1 = 0; double sum1 = 0, gpu1 = 0;
        int nHalf = 0; double sumHalf = 0;
        double sumP = 0, sumE = 0; int nP = 0, nE = 0; bool pclRecent = false;
        std::vector<float> all;
        all.reserve(g_mon.recent.size());
        for (auto it = g_mon.recent.rbegin(); it != g_mon.recent.rend(); ++it) {
            all.push_back(it->ms);
            if (it->t >= tEnd - 1.0) { n1++; sum1 += it->ms; gpu1 += it->gpu; }
            if (it->t >= tEnd - 0.5) { nHalf++; sumHalf += it->ms; }
            if (it->t >= tEnd - 3.0 && it->pcl > 0) pclRecent = true;
            if (it->t >= tEnd - 1.0) {
                if (it->pcl > 0) { sumP += it->pcl; nP++; }
                if (it->est > 0) { sumE += it->est; nE++; }
            }
        }
        // 마지막 프레임 후 1.5초 넘게 새 프레임이 없으면 0 FPS (멈춤/최소화)
        bool stale = GetTickCount() - g_mon.lastFrameTick > 1500;
        ls.fps = stale || sum1 <= 0 ? 0 : (float)(n1 * 1000.0 / sum1);
        ls.ms = stale || !nHalf ? 0 : (float)(sumHalf / nHalf);   // 새 프레임이 없으면 지난 값 대신 "--"
        ls.gpuBusy = n1 && sum1 > 0 ? (float)std::min(100.0, gpu1 / sum1 * 100.0) : 0;
        FrameStats st = ComputeStats(all.data(), all.size());
        if (!stale) { ls.low1 = st.low1; ls.low01 = st.low01; }
        if (!stale) {
            if (pclRecent) { ls.latKind = 1; ls.lat = nP ? (float)(sumP / nP) : 0; }
            else if (nE) { ls.latKind = 2; ls.lat = (float)(sumE / nE); }
        }
        if (sparkCount > 0) {
            int n = std::min<int>(sparkCount, (int)all.size());
            ls.spark.assign(all.rbegin() + (all.size() - n), all.rend());
        }
    }
    LeaveCriticalSection(&g_monCs);
    return ls;
}

// Present Mode 색 (좋음 / 보통 / 나쁨)
static int PresentModeGrade(const std::wstring& m) {
    if (m.find(L"Hardware") != std::wstring::npos) return 2;
    if (m.find(L"Composed: Flip") != std::wstring::npos) return 1;
    if (m.empty()) return -1;
    return 0;
}

// ---------------------------------------------------------------------
//  측정 대상 자동 감지 (1초마다, 가벼움)
// ---------------------------------------------------------------------
static bool ProcessAlive(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

static std::wstring ProcessExeName(DWORD pid, std::wstring* fullPath = nullptr) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t p[1024]; DWORD n = 1024;
    std::wstring r;
    if (QueryFullProcessImageNameW(h, 0, p, &n)) {
        if (fullPath) *fullPath = p;
        std::wstring s = p;
        r = StripExe(s.substr(s.find_last_of(L'\\') + 1));
    }
    CloseHandle(h);
    return r;
}

static DWORD FindGameCandidate(std::wstring& exeOut) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_hwnd || fg == g_ovl) return 0;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid || pid == GetCurrentProcessId()) return 0;
    std::wstring full;
    std::wstring exe = ProcessExeName(pid, &full);
    if (exe.empty() || IsSystemName(exe)) return 0;
    bool listed = ContainsI(g_set.games, exe);
    bool fullscreen = false;
    RECT wr;
    if (GetWindowRect(fg, &wr)) {
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi))
            fullscreen = wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
                         wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
    }
    if (fullscreen) {   // 전체 화면이어도 브라우저·영상 플레이어 등은 제외
        static const wchar_t* notGames[] = { L"chrome", L"msedge", L"firefox", L"whale", L"opera", L"brave", L"vlc",
                                             L"PotPlayerMini64", L"mpc-hc64", L"Code", L"devenv", L"POWERPNT", L"Discord" };
        for (auto n : notGames) if (IEq(exe, n)) fullscreen = false;
        wchar_t windir[MAX_PATH]; GetWindowsDirectoryW(windir, MAX_PATH);
        if (Lower(full).rfind(Lower(windir), 0) == 0) fullscreen = false;
    }
    if (!listed && !fullscreen) return 0;
    exeOut = exe;
    return pid;
}

// 게임이 꺼졌는데 측정 도구가 안 끝나면 직접 정리 (화면이 멈추지 않게 별도 스레드)
static volatile LONG g_monStopping = 0;
static DWORD WINAPI StopMonitorThread(LPVOID) {
    StopMonitor();
    InterlockedExchange(&g_monStopping, 0);
    return 0;
}

static bool CanMonitor() { return g_set.monitor && g_isAdmin && g_toolState == TOOL_READY; }

static void OnMonitorTimer() {
    if (!CanMonitor()) return;
    std::wstring exe;
    DWORD cand = FindGameCandidate(exe);
    EnterCriticalSection(&g_monCs);
    bool running = g_mon.running;
    DWORD cur = g_mon.pid;
    LeaveCriticalSection(&g_monCs);
    if (running && !ProcessAlive(cur)) {   // 게임 종료: 2초 안에 측정 도구가 안 끝나면 정리
        static DWORD deadPid = 0; static int deadTicks = 0;
        if (deadPid != cur) { deadPid = cur; deadTicks = 0; }
        if (++deadTicks >= 2 && InterlockedCompareExchange(&g_monStopping, 1, 0) == 0) {
            HANDLE th = CreateThread(nullptr, 0, StopMonitorThread, nullptr, 0, nullptr);
            if (th) CloseHandle(th); else InterlockedExchange(&g_monStopping, 0);
        }
        return;
    }
    if (running) {
        // 다른 게임이 3초 이상 앞에 있으면 대상 변경
        if (cand && cand != cur) {
            if (g_monCandidate == cand) g_monCandidateTicks++; else { g_monCandidate = cand; g_monCandidateTicks = 1; }
            if (g_monCandidateTicks >= 3) { g_monCandidateTicks = 0; StartMonitor(cand, exe); }
        } else g_monCandidateTicks = 0;
        return;
    }
    if (!running && cand && !g_monStopping) {
        if (cand == g_monFailPid && (g_monFails >= 3 || (int)(GetTickCount() - g_monRetryAt) < 0)) return;   // 실패 후 잠시 쉬기
        StartMonitor(cand, exe);
    }
}
