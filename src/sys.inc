//  프로세스 정보
// ---------------------------------------------------------------------
static std::vector<ProcEntry> Snapshot() {
    std::vector<ProcEntry> v;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return v;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do { v.push_back({ pe.th32ProcessID, pe.th32ParentProcessID, StripExe(pe.szExeFile) }); }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return v;
}

static std::set<std::wstring> RunningNames() {
    std::set<std::wstring> s;
    for (auto& p : Snapshot()) s.insert(Lower(p.name));
    return s;
}

static bool IsSystemName(const std::wstring& n) {
    for (auto s : kSystemNames) if (IEq(n, s)) return true;
    return false;
}

// 콘솔(명령 창) 프로그램인지 - 이런 보조 프로그램을 혼자 다시 켜면 빈 터미널 창이 뜸
static bool IsConsoleExe(const std::wstring& path) {
    static std::map<std::wstring, bool> cache;
    auto it = cache.find(Lower(path));
    if (it != cache.end()) return it->second;
    bool con = false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        static BYTE b[4096];
        DWORD rd = 0;
        if (ReadFile(f, b, sizeof(b), &rd, nullptr) && rd >= 0x40 && b[0] == 'M' && b[1] == 'Z') {
            DWORD pe = 0; memcpy(&pe, b + 0x3C, 4);
            if (pe + 24 + 70 <= rd && !memcmp(b + pe, "PE\0\0", 4)) {
                WORD sub = 0; memcpy(&sub, b + pe + 24 + 68, 2);   // OptionalHeader.Subsystem (32/64비트 같은 위치)
                con = sub == IMAGE_SUBSYSTEM_WINDOWS_CUI;
            }
        }
        CloseHandle(f);
    }
    cache[Lower(path)] = con;
    return con;
}

// 게임 본체 · 런처 · 안티치트: 목록에 안 보이고, 끄거나 다시 켜지 않음
// (런처를 다시 켜면 게임이 또 실행됨 - 예: 배그 ExecPubg · TslGame_BE)
static bool IsGameComponent(const std::wstring& name, const std::wstring& path, const std::wstring& desc) {
    std::wstring n = Lower(name), d = Lower(desc);
    static const wchar_t* kAntiCheat[] = { L"battleye", L"easyanticheat", L"anti-cheat", L"anticheat", L"vanguard", L"xigncode", L"nprotect" };
    for (auto k : kAntiCheat) if (n.find(k) != std::wstring::npos || d.find(k) != std::wstring::npos) return true;
    size_t slash = path.find_last_of(L'\\');
    std::wstring dir = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    for (auto& g : g_set.games) {
        std::wstring lg = Lower(g);
        if (lg.empty()) continue;
        if (n.compare(0, lg.size(), lg) == 0) return true;                 // TslGame_BE 처럼 게임 이름으로 시작
        if (!dir.empty() && FileExists(dir + g + L".exe")) return true;    // 게임 exe와 같은 폴더 (게임 런처)
    }
    return false;
}

// 이 앱들이 띄운 프로그램은 "보조 프로그램"이 아님 (사용자가 직접 켠 앱)
static const wchar_t* kStarters[] = {
    L"explorer", L"svchost", L"sihost", L"userinit", L"services", L"wininit", L"winlogon", L"taskhostw",
    L"RuntimeBroker", L"cmd", L"powershell", L"pwsh", L"WindowsTerminal", L"OpenConsole", L"conhost", L"FrameBooster"
};

static bool IsRealWindow(HWND h) {
    if (!IsWindowVisible(h)) return false;
    if (GetWindow(h, GW_OWNER)) return false;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    if (GetWindowTextLengthW(h) == 0) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    return true;
}

static BOOL CALLBACK EnumWinPids(HWND h, LPARAM lp) {
    if (!IsRealWindow(h)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    ((std::set<DWORD>*)lp)->insert(pid);
    return TRUE;
}

static BOOL CALLBACK EnumCloseWins(HWND h, LPARAM lp) {
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (((std::set<DWORD>*)lp)->count(pid)) PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}

typedef LONG(NTAPI* PFN_NtQIP)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static std::wstring GetCmdLine(DWORD pid) {
    static PFN_NtQIP fn = (PFN_NtQIP)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (!fn) return L"";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    std::wstring r;
    ULONG len = 0;
    fn(h, 60 /* ProcessCommandLineInformation */, nullptr, 0, &len);
    if (len > 0 && len < 1024 * 1024) {
        std::vector<BYTE> buf(len);
        if (fn(h, 60, buf.data(), len, &len) >= 0) {
            struct US { USHORT Length, MaximumLength; PWSTR Buffer; };
            US* us = (US*)buf.data();
            if (us->Buffer && us->Length) r.assign(us->Buffer, us->Length / sizeof(wchar_t));
        }
    }
    CloseHandle(h);
    return r;
}

static std::wstring ArgsFromCmd(std::wstring cmd) {
    cmd = Trim(cmd);
    if (cmd.empty()) return L"";
    if (cmd[0] == L'"') {
        size_t e = cmd.find(L'"', 1);
        if (e == std::wstring::npos) return L"";
        return Trim(cmd.substr(e + 1));
    }
    std::wstring lc = Lower(cmd);
    size_t i = lc.find(L".exe");
    if (i != std::wstring::npos) return Trim(cmd.substr(i + 4));
    size_t sp = cmd.find(L' ');
    return sp == std::wstring::npos ? L"" : Trim(cmd.substr(sp + 1));
}

static std::wstring FileDesc(const std::wstring& path) {
    static std::map<std::wstring, std::wstring> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    std::wstring r;
    DWORD dummy = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (sz) {
        std::vector<BYTE> b(sz);
        if (GetFileVersionInfoW(path.c_str(), 0, sz, b.data())) {
            struct LC { WORD l, c; }* lc = nullptr;
            UINT n = 0;
            std::vector<std::wstring> keys;
            if (VerQueryValueW(b.data(), L"\\VarFileInfo\\Translation", (void**)&lc, &n) && n >= sizeof(LC)) {
                wchar_t q[80]; swprintf(q, 80, L"\\StringFileInfo\\%04x%04x\\FileDescription", lc->l, lc->c);
                keys.push_back(q);
            }
            keys.push_back(L"\\StringFileInfo\\040904b0\\FileDescription");
            keys.push_back(L"\\StringFileInfo\\041204b0\\FileDescription");
            for (auto& k : keys) {
                wchar_t* v = nullptr; UINT vl = 0;
                if (VerQueryValueW(b.data(), k.c_str(), (void**)&v, &vl) && v && vl > 1) { r = Trim(v); if (!r.empty()) break; }
            }
        }
    }
    cache[path] = r;
    return r;
}

static std::vector<AppInfo> GetAppList() {
    DWORD me = GetCurrentProcessId(), sess = 0;
    ProcessIdToSessionId(me, &sess);
    wchar_t wd[MAX_PATH]; GetWindowsDirectoryW(wd, MAX_PATH);
    std::wstring win = Lower(wd);
    if (!win.empty() && win.back() != L'\\') win += L'\\';

    std::set<DWORD> winPids;
    EnumWindows(EnumWinPids, (LPARAM)&winPids);

    struct P { DWORD pid, ppid; std::wstring name, path; SIZE_T mem; };
    std::map<std::wstring, std::vector<P>> groups;
    auto snap = Snapshot();
    std::map<DWORD, std::wstring> nameOf;
    for (auto& pr : snap) nameOf[pr.pid] = pr.name;

    for (auto& pr : snap) {
        if (pr.pid <= 4 || pr.pid == me) continue;
        DWORD s = 0;
        if (!ProcessIdToSessionId(pr.pid, &s) || s != sess) continue;
        if (IsSystemName(pr.name)) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pr.pid);
        if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pr.pid);
        if (!h) continue;
        wchar_t path[1024]; DWORD pl = 1024;
        BOOL ok = QueryFullProcessImageNameW(h, 0, path, &pl);
        PROCESS_MEMORY_COUNTERS pmc; ZeroMemory(&pmc, sizeof(pmc)); pmc.cb = sizeof(pmc);
        SIZE_T mem = GetProcessMemoryInfo(h, &pmc, sizeof(pmc)) ? pmc.WorkingSetSize : 0;
        CloseHandle(h);
        if (!ok) continue;
        if (Lower(path).compare(0, win.size(), win) == 0) continue;
        if (IsGameComponent(pr.name, path, FileDesc(path))) continue;
        groups[Lower(pr.name)].push_back({ pr.pid, pr.ppid, pr.name, path, mem });
    }

    std::vector<AppInfo> out;
    for (auto& kv : groups) {
        auto& grp = kv.second;
        std::set<DWORD> ids;
        for (auto& p : grp) ids.insert(p.pid);
        const P* root = &grp[0];
        for (auto& p : grp) if (!ids.count(p.ppid)) { root = &p; break; }   // 같은 이름 중 부모 프로세스
        AppInfo a;
        a.name = root->name;
        a.path = root->path;
        a.args = ArgsFromCmd(GetCmdLine(root->pid));
        a.count = (int)grp.size();
        unsigned long long sum = 0;
        for (auto& p : grp) { sum += p.mem; if (winPids.count(p.pid)) a.hasWindow = true; }
        a.memMB = (sum + 512 * 1024) / (1024 * 1024);
        a.display = FileDesc(a.path);
        if (a.display.empty()) a.display = a.name;
        // 다른 앱이 띄운 창 없는 프로그램 = 보조 프로그램 (원래 앱이 켜질 때 같이 켜짐)
        auto pn = nameOf.find(root->ppid);
        if (pn != nameOf.end() && !IEq(pn->second, a.name)) {
            bool starter = false;
            for (auto st : kStarters) if (IEq(pn->second, st)) starter = true;
            if (!starter) a.parent = pn->second;
        }
        a.helper = !a.hasWindow && (!a.parent.empty() || IsConsoleExe(a.path));
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const AppInfo& x, const AppInfo& y) {
        if (x.hasWindow != y.hasWindow) return x.hasWindow;
        return x.memMB > y.memMB;
    });
    return out;
}

static bool IsChecked(const AppInfo& a) { return !ContainsI(g_set.keep, a.name); }

static void TrimSelf() {
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// 남아있는 앱들의 작업 메모리를 비워 게임이 쓸 RAM 확보 (게임 프로세스는 제외)
static void CleanMemory(const std::vector<std::wstring>& skip, const std::function<void(int, const std::wstring&)>& log) {
    MEMORYSTATUSEX a; a.dwLength = sizeof(a); GlobalMemoryStatusEx(&a);
    DWORD me = GetCurrentProcessId();
    int n = 0;
    for (auto& p : Snapshot()) {
        if (p.pid <= 4 || p.pid == me || ContainsI(skip, p.name)) continue;
        HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
        if (!h) continue;
        if (EmptyWorkingSet(h)) n++;
        CloseHandle(h);
    }
    Sleep(300);
    MEMORYSTATUSEX b; b.dwLength = sizeof(b); GlobalMemoryStatusEx(&b);
    long long freed = ((long long)b.ullAvailPhys - (long long)a.ullAvailPhys) / (1024 * 1024);
    log(L_OK, L"메모리 정리: 프로세스 " + std::to_wstring(n) + L"개 · 약 " + FmtNum((unsigned long long)std::max(0LL, freed)) + L" MB 확보");
}

// ---------------------------------------------------------------------
//  윈도우 최적화 (레지스트리) - 적용 전 원래 값을 백업, 언제든 되돌리기
// ---------------------------------------------------------------------
struct RegVal {
    HKEY root; std::wstring path, name;
    DWORD type;                     // REG_DWORD / REG_SZ
    DWORD dw; std::wstring sz;
    bool defExists; DWORD defDw; std::wstring defSz;   // 백업이 없을 때 쓸 윈도우 기본값
};

struct TweakDef {
    std::wstring id; const wchar_t* title; const wchar_t* desc;
    bool admin, reboot, recommended;
    std::vector<RegVal> vals;
};

static RegVal RDW(HKEY r, const std::wstring& p, const std::wstring& n, DWORD v, bool de = false, DWORD dv = 0) {
    return { r, p, n, REG_DWORD, v, L"", de, dv, L"" };
}
static RegVal RSZ(HKEY r, const std::wstring& p, const std::wstring& n, const std::wstring& v, bool de = false, const std::wstring& dv = L"") {
    return { r, p, n, REG_SZ, 0, v, de, 0, dv };
}

static bool RegRead(HKEY root, const std::wstring& path, const std::wstring& name, DWORD& type, std::vector<BYTE>& data) {
    HKEY k;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    DWORD sz = 0;
    LONG r = RegQueryValueExW(k, name.c_str(), nullptr, &type, nullptr, &sz);
    if (r == ERROR_SUCCESS) {
        data.assign(sz, 0);
        r = RegQueryValueExW(k, name.c_str(), nullptr, &type, data.data(), &sz);
        data.resize(sz);
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static bool RegWrite(HKEY root, const std::wstring& path, const std::wstring& name, DWORD type, const BYTE* data, DWORD size) {
    HKEY k;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name.c_str(), 0, type, data, size);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static bool RegDelete(HKEY root, const std::wstring& path, const std::wstring& name) {
    HKEY k;
    LONG r = RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &k);
    if (r == ERROR_FILE_NOT_FOUND) return true;
    if (r != ERROR_SUCCESS) return false;
    r = RegDeleteValueW(k, name.c_str());
    RegCloseKey(k);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

static std::wstring SzFromData(const std::vector<BYTE>& d) {
    std::wstring s((const wchar_t*)d.data(), d.size() / sizeof(wchar_t));
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

static bool WriteVal(HKEY root, const std::wstring& path, const std::wstring& name, DWORD type, DWORD dw, const std::wstring& sz) {
    if (type == REG_DWORD) return RegWrite(root, path, name, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
    return RegWrite(root, path, name, REG_SZ, (const BYTE*)sz.c_str(), (DWORD)((sz.size() + 1) * sizeof(wchar_t)));
}

static bool ValIsSet(const RegVal& v) {
    DWORD type = 0; std::vector<BYTE> d;
    if (!RegRead(v.root, v.path, v.name, type, d)) return false;
    if (v.type == REG_DWORD) return type == REG_DWORD && d.size() >= 4 && *(const DWORD*)d.data() == v.dw;
    return (type == REG_SZ || type == REG_EXPAND_SZ) && SzFromData(d) == v.sz;
}

// 인터넷이 연결된 네트워크 어댑터 (네이글 알고리즘 설정용)
static std::vector<std::wstring> ActiveNetInterfaces() {
    std::vector<std::wstring> out;
    const std::wstring base = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return out;
    for (DWORD i = 0;; i++) {
        wchar_t sub[256]; DWORD sl = 256;
        if (RegEnumKeyExW(k, i, sub, &sl, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        std::wstring path = base + L"\\" + sub;
        bool active = false;
        for (const wchar_t* vn : { L"DhcpIPAddress", L"IPAddress" }) {
            DWORD type = 0; std::vector<BYTE> d;
            if (!active && RegRead(HKEY_LOCAL_MACHINE, path, vn, type, d)) {
                std::wstring ip = SzFromData(d);
                size_t z = ip.find(L'\0');
                if (z != std::wstring::npos) ip.resize(z);
                if (!ip.empty() && ip != L"0.0.0.0") active = true;
            }
        }
        if (active) out.push_back(path);
    }
    RegCloseKey(k);
    return out;
}

static std::vector<TweakDef> BuildTweaks() {
    const HKEY CU = HKEY_CURRENT_USER, LM = HKEY_LOCAL_MACHINE;
    const std::wstring gcs = L"System\\GameConfigStore";
    const std::wstring mm = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
    const std::wstring games = mm + L"\\Tasks\\Games";
    std::vector<TweakDef> t;
    t.push_back({ L"gamemode", L"게임 모드 켜기", L"윈도우가 게임에 CPU·GPU를 먼저 배정", false, false, true,
        { RDW(CU, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1),
          RDW(CU, L"Software\\Microsoft\\GameBar", L"AllowAutoGameMode", 1) } });
    t.push_back({ L"gamedvr", L"게임 녹화(Game DVR) 끄기", L"백그라운드 녹화로 생기는 프레임 손실 방지", false, false, true,
        { RDW(CU, gcs, L"GameDVR_Enabled", 0, true, 1),
          RDW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0) } });
    t.push_back({ L"mouse", L"마우스 가속 끄기", L"마우스 움직임을 1:1로 (에임 일관성)", false, false, true,
        { RSZ(CU, L"Control Panel\\Mouse", L"MouseSpeed", L"0", true, L"1"),
          RSZ(CU, L"Control Panel\\Mouse", L"MouseThreshold1", L"0", true, L"6"),
          RSZ(CU, L"Control Panel\\Mouse", L"MouseThreshold2", L"0", true, L"10") } });
    t.push_back({ L"transparency", L"투명 효과 끄기", L"작업 표시줄·창의 반투명 효과 제거", false, false, true,
        { RDW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0, true, 1) } });
    t.push_back({ L"animation", L"창 애니메이션 끄기", L"창을 열고 닫을 때 애니메이션 제거", false, false, true,
        { RSZ(CU, L"Control Panel\\Desktop\\WindowMetrics", L"MinAnimate", L"0", true, L"1") } });
    t.push_back({ L"bgapps", L"백그라운드 앱 끄기", L"스토어 앱이 뒤에서 실행되지 않게", false, false, true,
        { RDW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", 1),
          RDW(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Search", L"BackgroundAppGlobalToggle", 0) } });
    t.push_back({ L"fso", L"전체 화면 최적화 끄기", L"일부 게임의 입력 지연 개선 (게임마다 다름)", false, false, false,
        { RDW(CU, gcs, L"GameDVR_FSEBehaviorMode", 2), RDW(CU, gcs, L"GameDVR_HonorUserFSEBehaviorMode", 1),
          RDW(CU, gcs, L"GameDVR_FSEBehavior", 2), RDW(CU, gcs, L"GameDVR_DXGIHonorFSEWindowsCompatible", 1) } });
    t.push_back({ L"mmcss", L"게임 작업 우선순위 높이기", L"멀티미디어 스케줄러가 게임을 먼저 처리", true, false, true,
        { RDW(LM, mm, L"SystemResponsiveness", 10, true, 20),
          RDW(LM, games, L"GPU Priority", 8, true, 8), RDW(LM, games, L"Priority", 6, true, 2),
          RSZ(LM, games, L"Scheduling Category", L"High", true, L"Medium"), RSZ(LM, games, L"SFIO Priority", L"High", true, L"Normal") } });
    t.push_back({ L"netthrottle", L"네트워크 제한 해제", L"게임 중 네트워크 처리량 제한 끄기", true, false, true,
        { RDW(LM, mm, L"NetworkThrottlingIndex", 0xFFFFFFFF, true, 10) } });
    TweakDef nagle = { L"nagle", L"네이글 알고리즘 끄기", L"온라인 게임 패킷을 모으지 않고 바로 전송", true, true, true, {} };
    for (auto& p : ActiveNetInterfaces()) {
        nagle.vals.push_back(RDW(LM, p, L"TcpAckFrequency", 1));
        nagle.vals.push_back(RDW(LM, p, L"TCPNoDelay", 1));
    }
    t.push_back(nagle);
    t.push_back({ L"priosep", L"실행 중인 창 우선 처리", L"앞에 띄운 게임에 CPU 시간을 더 배정", true, false, true,
        { RDW(LM, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", L"Win32PrioritySeparation", 0x26, true, 2) } });
    t.push_back({ L"powerthrottle", L"전원 제한(Power Throttling) 끄기", L"백그라운드 CPU 절전 제한 해제", true, true, true,
        { RDW(LM, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling", L"PowerThrottlingOff", 1) } });
    t.push_back({ L"hags", L"하드웨어 가속 GPU 예약", L"지원 GPU(RTX 등)에서 지연 감소", true, true, false,
        { RDW(LM, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 2) } });
    return t;
}

// 윈도우 게임 모드 (설정 > 게임 > 게임 모드 와 같은 값). 값이 없으면 윈도우 기본 = 켜짐
static bool GameModeOn() {
    DWORD type = 0; std::vector<BYTE> d;
    if (!RegRead(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", type, d)) return true;
    return !(type == REG_DWORD && d.size() >= 4 && *(const DWORD*)d.data() == 0);
}
static bool SetGameMode(bool on) {
    return WriteVal(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", REG_DWORD, on ? 1 : 0, L"");
}

static bool IsTweakApplied(const TweakDef& t) {
    if (t.vals.empty()) return false;
    for (auto& v : t.vals) if (!ValIsSet(v)) return false;
    return true;
}

// ── 백업 파일: root \t path \t name \t exists \t type \t hex
struct BackupEntry { std::wstring root, path, name; bool exists; DWORD type; std::vector<BYTE> data; };

static std::wstring RootName(HKEY r) { return r == HKEY_LOCAL_MACHINE ? L"HKLM" : L"HKCU"; }
static std::wstring BackupKey(const std::wstring& root, const std::wstring& path, const std::wstring& name) {
    return root + L"|" + Lower(path) + L"|" + Lower(name);
}

static std::map<std::wstring, BackupEntry> LoadBackups() {
    std::map<std::wstring, BackupEntry> m;
    if (!FileExists(g_backupFile)) return m;
    for (auto& l : SplitLines(ReadAllText(g_backupFile))) {
        std::vector<std::wstring> f;
        size_t st = 0;
        for (;;) {
            size_t e = l.find(L'\t', st);
            f.push_back(l.substr(st, e == std::wstring::npos ? std::wstring::npos : e - st));
            if (e == std::wstring::npos) break;
            st = e + 1;
        }
        if (f.size() < 6) continue;
        BackupEntry b{ f[0], f[1], f[2], f[3] == L"1", (DWORD)wcstoul(f[4].c_str(), nullptr, 10), {} };
        for (size_t i = 0; i + 1 < f[5].size(); i += 2) b.data.push_back((BYTE)wcstoul(f[5].substr(i, 2).c_str(), nullptr, 16));
        m[BackupKey(b.root, b.path, b.name)] = b;
    }
    return m;
}

static void SaveBackups(const std::map<std::wstring, BackupEntry>& m) {
    if (m.empty()) { DeleteFileW(g_backupFile.c_str()); return; }
    std::wstring t;
    for (auto& kv : m) {
        auto& b = kv.second;
        std::wstring hex;
        wchar_t h[4];
        for (BYTE c : b.data) { swprintf(h, 4, L"%02X", c); hex += h; }
        t += b.root + L"\t" + b.path + L"\t" + b.name + L"\t" + (b.exists ? L"1" : L"0") + L"\t" + std::to_wstring(b.type) + L"\t" + hex + L"\r\n";
    }
    WriteAllText(g_backupFile, t);
}

static int ApplyTweak(const TweakDef& t, std::map<std::wstring, BackupEntry>& bk) {
    int fail = 0;
    for (auto& v : t.vals) {
        std::wstring key = BackupKey(RootName(v.root), v.path, v.name);
        if (!bk.count(key)) {   // 처음 바꿀 때만 원래 값 저장
            BackupEntry e{ RootName(v.root), v.path, v.name, false, 0, {} };
            e.exists = RegRead(v.root, v.path, v.name, e.type, e.data);
            bk[key] = e;
        }
        if (!WriteVal(v.root, v.path, v.name, v.type, v.dw, v.sz)) fail++;
    }
    return fail;
}

static const wchar_t* kLegacyIds[] = { L"mouse", L"transparency", L"animation", L"bgapps", L"fso", L"mmcss",
                                      L"netthrottle", L"nagle", L"priosep", L"powerthrottle", L"hags" };
static bool IsLegacyTweak(const std::wstring& id) {
    for (auto l : kLegacyIds) if (id == l) return true;
    return false;
}

static int RevertTweak(const TweakDef& t, std::map<std::wstring, BackupEntry>& bk) {
    int fail = 0;
    for (auto& v : t.vals) {
        auto it = bk.find(BackupKey(RootName(v.root), v.path, v.name));
        bool ok;
        if (it != bk.end()) {
            auto& b = it->second;
            ok = b.exists ? RegWrite(v.root, v.path, v.name, b.type, b.data.empty() ? nullptr : b.data.data(), (DWORD)b.data.size())
                          : RegDelete(v.root, v.path, v.name);
            if (ok) bk.erase(it);
        } else if (IsLegacyTweak(t.id)) {
            ok = true;   // 예전 버전 항목은 백업이 있는 값만 되돌림 (사용자가 직접 바꾼 설정은 건드리지 않음)
        } else {
            ok = v.defExists ? WriteVal(v.root, v.path, v.name, v.type, v.defDw, v.defSz) : RegDelete(v.root, v.path, v.name);
        }
        if (!ok) fail++;
    }
    return fail;
}

// 마우스·애니메이션·투명 효과는 재로그인 없이 바로 반영
static void ApplyLiveSettings(const std::vector<std::wstring>& apply, const std::vector<std::wstring>& revert) {
    auto has = [&](const wchar_t* id) { return ContainsI(apply, id) || ContainsI(revert, id); };
    DWORD type = 0; std::vector<BYTE> d;
    auto readInt = [&](const wchar_t* path, const wchar_t* n, int def) {
        if (RegRead(HKEY_CURRENT_USER, path, n, type, d)) return _wtoi(SzFromData(d).c_str());
        return def;
    };
    if (has(L"mouse")) {
    int m[3] = { readInt(L"Control Panel\\Mouse", L"MouseThreshold1", 6),
                 readInt(L"Control Panel\\Mouse", L"MouseThreshold2", 10),
                 readInt(L"Control Panel\\Mouse", L"MouseSpeed", 1) };
    SystemParametersInfoW(SPI_SETMOUSE, 0, m, SPIF_SENDCHANGE);
    }
    if (has(L"animation")) {
    ANIMATIONINFO ai = { sizeof(ai), readInt(L"Control Panel\\Desktop\\WindowMetrics", L"MinAnimate", 1) };
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, SPIF_SENDCHANGE);
    }
    if (!has(L"transparency")) return;
    DWORD_PTR res = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 500, &res);
}

// adminPart: true = 관리자 항목만, false = 사용자 항목만
static int RunTweakBatch(const std::vector<std::wstring>& apply, const std::vector<std::wstring>& revert, bool adminPart,
                         const std::function<void(int, const std::wstring&)>& log) {
    auto defs = BuildTweaks();
    auto bk = LoadBackups();
    int fail = 0;
    for (auto& t : defs) {
        if (t.admin != adminPart) continue;
        bool a = ContainsI(apply, t.id), r = ContainsI(revert, t.id);
        if (!a && !r) continue;
        if (t.vals.empty()) { log(L_WARN, std::wstring(t.title) + L" - 이 PC에서는 해당 없음"); continue; }
        int f = a ? ApplyTweak(t, bk) : RevertTweak(t, bk);
        fail += f;
        log(f ? L_ERR : L_OK, std::wstring(f ? L"× " : L"✓ ") + t.title + (a ? L" 적용" : L" 되돌림") + (f ? L" 실패" : L""));
    }
    SaveBackups(bk);
    return fail;
}

static bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e = {}; DWORD sz = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &sz);
    CloseHandle(tok);
    return ok && e.TokenIsElevated;
}

static std::wstring JoinIds(const std::vector<std::wstring>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size(); i++) { if (i) s += L","; s += v[i]; }
    return s;
}

static std::vector<std::wstring> SplitIds(const std::wstring& s) {
    std::vector<std::wstring> v;
    size_t st = 0;
    while (st <= s.size()) {
        size_t e = s.find(L',', st);
        if (e == std::wstring::npos) e = s.size();
        std::wstring id = Trim(s.substr(st, e - st));
        if (!id.empty()) v.push_back(id);
        st = e + 1;
    }
    return v;
}

// ---------------------------------------------------------------------
//  전원 모드 - X3D CPU는 균형 조정(AMD 권장), 그 외는 고성능
// ---------------------------------------------------------------------
static const GUID kPowerBalanced = { 0x381b4222, 0xf694, 0x41f0, { 0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e } };
static const GUID kPowerHigh     = { 0x8c5e7fda, 0xe8bf, 0x4a96, { 0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c } };
static const GUID kPowerUltimate = { 0xe9a42b02, 0xd5df, 0x448d, { 0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61 } };

static std::wstring g_cpuName;
static bool g_isX3D = false;

static void DetectCpu() {
    DWORD type = 0; std::vector<BYTE> d;
    if (RegRead(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", type, d))
        g_cpuName = Trim(SzFromData(d));
    g_isX3D = Lower(g_cpuName).find(L"x3d") != std::wstring::npos;
}

static bool GetActivePower(GUID& g) {
    GUID* p = nullptr;
    if (PowerGetActiveScheme(nullptr, &p) != ERROR_SUCCESS || !p) return false;
    g = *p;
    LocalFree(p);
    return true;
}

static std::wstring PowerName(const GUID& g) {
    if (IsEqualGUID(g, kPowerBalanced)) return L"균형 조정";
    if (IsEqualGUID(g, kPowerHigh)) return L"고성능";
    if (IsEqualGUID(g, kPowerUltimate)) return L"최고의 성능";
    wchar_t buf[256] = {}; DWORD sz = sizeof(buf);
    if (PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, (UCHAR*)buf, &sz) == ERROR_SUCCESS && buf[0]) return buf;
    return L"사용자 지정";
}


// 전원 계획 목록 (보이는 것만)
static std::vector<GUID> PowerSchemes() {
    std::vector<GUID> v;
    for (ULONG i = 0;; i++) {
        GUID g; DWORD sz = sizeof(g);
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, i, (UCHAR*)&g, &sz) != ERROR_SUCCESS) break;
        v.push_back(g);
    }
    return v;
}

static int EffectivePowerMode() {
    if (g_set.powerMode >= PWR_BALANCED && g_set.powerMode <= PWR_ULTIMATE) return g_set.powerMode;
    return g_isX3D ? PWR_BALANCED : PWR_HIGH;
}

static const wchar_t* PowerModeName(int m) {
    return m == PWR_BALANCED ? L"균형 조정" : m == PWR_HIGH ? L"고성능" : L"최고의 성능";
}

// 해당 단계의 전원 계획 찾기 (없으면 관리자 권한일 때 만들기)
static bool FindPowerScheme(int mode, GUID& out, bool create) {
    const GUID& base = mode == PWR_BALANCED ? kPowerBalanced : mode == PWR_HIGH ? kPowerHigh : kPowerUltimate;
    auto list = PowerSchemes();
    for (auto& g : list) if (IsEqualGUID(g, base)) { out = g; return true; }
    for (auto& g : list) {   // 이름으로 찾기 (복제된 계획)
        wchar_t buf[256] = {}; DWORD sz = sizeof(buf);
        if (PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, (UCHAR*)buf, &sz) != ERROR_SUCCESS) continue;
        std::wstring n = Lower(buf);
        if (mode == PWR_ULTIMATE && (n.find(L"ultimate") != std::wstring::npos || n.find(L"최고의 성능") != std::wstring::npos)) { out = g; return true; }
        if (mode == PWR_HIGH && (n.find(L"high performance") != std::wstring::npos || n == L"고성능")) { out = g; return true; }
    }
    if (!create || mode == PWR_BALANCED) return false;
    GUID* dup = nullptr;
    if (PowerDuplicateScheme(nullptr, &base, &dup) == ERROR_SUCCESS && dup) { out = *dup; LocalFree(dup); return true; }
    return false;
}

static int PowerModeOf(const GUID& g) {
    if (IsEqualGUID(g, kPowerBalanced)) return PWR_BALANCED;
    if (IsEqualGUID(g, kPowerHigh)) return PWR_HIGH;
    if (IsEqualGUID(g, kPowerUltimate)) return PWR_ULTIMATE;
    GUID x;
    if (FindPowerScheme(PWR_ULTIMATE, x, false) && IsEqualGUID(x, g)) return PWR_ULTIMATE;
    if (FindPowerScheme(PWR_HIGH, x, false) && IsEqualGUID(x, g)) return PWR_HIGH;
    return -1;
}

// 부스트 시작: 선택한 전원 모드로 바꾸고 원래 모드를 파일에 기억
static void BoostPower(int mode, const std::function<void(int, const std::wstring&)>& log) {
    if (mode < 0) return;
    GUID cur;
    if (!GetActivePower(cur)) { log(L_WARN, L"전원 모드를 읽지 못했어요"); return; }
    GUID target;
    if (!FindPowerScheme(mode, target, true)) {
        log(L_WARN, std::wstring(PowerModeName(mode)) + L" 전원 계획이 없어요 (관리자 권한으로 실행하면 만들 수 있어요)");
        return;
    }
    if (IsEqualGUID(cur, target)) { log(L_INFO, L"전원 모드: 이미 " + std::wstring(PowerModeName(mode))); return; }
    if (PowerSetActiveScheme(nullptr, &target) == ERROR_SUCCESS) {
        if (!FileExists(g_powerFile)) {
            wchar_t s[64]; StringFromGUID2(cur, s, 64);
            WriteAllText(g_powerFile, s);
        }
        log(L_OK, L"전원 모드: " + PowerName(cur) + L" → " + PowerModeName(mode));
    } else {
        log(L_WARN, L"전원 모드를 바꾸지 못했어요");
    }
}

// 부스트 끝: 원래 전원 모드로
static void RestorePower(const std::function<void(int, const std::wstring&)>& log) {
    if (!FileExists(g_powerFile)) return;
    std::wstring s = Trim(ReadAllText(g_powerFile));
    GUID g;
    if (SUCCEEDED(CLSIDFromString(s.c_str(), &g)) && PowerSetActiveScheme(nullptr, &g) == ERROR_SUCCESS)
        log(L_OK, L"전원 모드: " + PowerName(g) + L"(으)로 복원");
    else
        log(L_WARN, L"전원 모드를 되돌리지 못했어요");
    DeleteFileW(g_powerFile.c_str());
}

// ---------------------------------------------------------------------
//  앱 종료 / 복원 (작업 스레드)
// ---------------------------------------------------------------------
struct BoostJob { std::vector<AppInfo> targets; bool force; bool autoRestore; int powerMode; bool memClean; std::vector<std::wstring> games; };

static DWORD WINAPI BoostThread(LPVOID p) {
    BoostJob* job = (BoostJob*)p;
    auto& targets = job->targets;
    {   // 목록을 띄운 뒤 이미 꺼진 앱은 빼기 (안 그러면 게임 끝나고 엉뚱하게 켜짐)
        auto run0 = RunningNames();
        targets.erase(std::remove_if(targets.begin(), targets.end(),
                      [&](const AppInfo& a) { return !run0.count(Lower(a.name)); }), targets.end());
    }
    {   // 보조 프로그램은 띄운 앱도 같이 끌 때만 끔 (띄운 앱이 계속 켜져 있으면 그 앱이 망가질 수 있음)
        std::set<std::wstring> tn;
        for (auto& a : targets) tn.insert(Lower(a.name));
        targets.erase(std::remove_if(targets.begin(), targets.end(),
                      [&](const AppInfo& a) { return a.helper && !a.parent.empty() && !tn.count(Lower(a.parent)); }), targets.end());
    }
    if (!targets.empty()) LogFromThread(L_INFO, L"부스트 시작: 앱 " + std::to_wstring(targets.size()) + L"개 종료");
    else LogFromThread(L_INFO, L"부스트 시작 (끌 앱 없음)");

    WriteAllText(g_boostFile, L"1");
    std::set<std::wstring> names;
    for (auto& a : targets) names.insert(Lower(a.name));

    // 1단계: 정상 종료 요청 (창 닫기)
    std::set<DWORD> pids;
    for (auto& pr : Snapshot()) if (names.count(Lower(pr.name))) pids.insert(pr.pid);
    EnumWindows(EnumCloseWins, (LPARAM)&pids);
    if (!names.empty()) LogFromThread(L_INFO, L"정상 종료 요청 보냄 - 최대 5초 대기");

    std::set<std::wstring> left;
    for (int t = 0; t < 5000 && !names.empty(); t += 300) {
        Sleep(300);
        auto run = RunningNames();
        left.clear();
        for (auto& n : names) if (run.count(n)) left.insert(n);
        if (left.empty()) break;
    }

    // 2단계: 트레이로 숨거나 응답 없는 앱 강제 종료
    if (!left.empty()) {
        if (job->force) {
            LogFromThread(L_WARN, L"아직 안 꺼진 앱 " + std::to_wstring(left.size()) + L"개 강제 종료");
            for (auto& pr : Snapshot()) {
                if (!left.count(Lower(pr.name))) continue;
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pr.pid);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
            Sleep(800);
        } else {
            LogFromThread(L_WARN, L"아직 안 꺼진 앱 " + std::to_wstring(left.size()) + L"개 (정상 종료만 설정됨)");
        }
    }

    // 3단계: 결과 확인 + 복원 목록 저장
    auto run = RunningNames();
    auto saved = LoadState();
    int ok = 0, fail = 0;
    for (auto& a : targets) {
        if (run.count(Lower(a.name))) {
            LogFromThread(L_ERR, L"× " + a.display + L" - 종료 실패 (관리자 권한 앱일 수 있음)");
            fail++;
        } else {
            LogFromThread(L_OK, L"✓ " + a.display + (a.helper ? L" (보조 프로그램 - 다시 켜지 않음)" : L""));
            ok++;
            bool dup = false;
            for (auto& s : saved) if (IEq(s.name, a.name)) dup = true;
            if (!dup && !a.helper) saved.push_back({ a.name, a.display, a.path, a.args });
        }
    }
    SaveState(saved);
    if (!targets.empty()) LogFromThread(L_OK, L"완료: " + std::to_wstring(ok) + L"개 종료 · " + std::to_wstring(fail) + L"개 실패 · 복원 대기 " + std::to_wstring(saved.size()) + L"개");
    auto tlog = [](int l, const std::wstring& m) { LogFromThread(l, m); };
    if (job->memClean) CleanMemory(job->games, tlog);
    BoostPower(job->powerMode, tlog);
    if (job->autoRestore) LogFromThread(L_INFO, L"게임 감시 시작 - 게임이 꺼지면 자동으로 다시 켭니다");
    PostMessageW(g_hwnd, WM_APP_DONE, 1, ok);
    delete job;
    return 0;
}

// 관리자 권한으로 실행 중이어도, 다시 켜는 앱은 일반 권한으로 (작업 표시줄=탐색기의 권한 사용)
static bool RunUnelevated(const std::wstring& exe, const std::wstring& args, const std::wstring& dir) {
    HWND shell = GetShellWindow();
    if (!shell) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &pid);
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return false;
    HANDLE tok = nullptr, prim = nullptr;
    bool ok = false;
    if (OpenProcessToken(hp, TOKEN_DUPLICATE, &tok) &&
        DuplicateTokenEx(tok, TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                         nullptr, SecurityImpersonation, TokenPrimary, &prim)) {
        STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        std::wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
        if (CreateProcessWithTokenW(prim, 0, exe.c_str(), &cmd[0], 0, nullptr, dir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            ok = true;
        }
    }
    if (prim) CloseHandle(prim);
    if (tok) CloseHandle(tok);
    CloseHandle(hp);
    return ok;
}

static int RestoreCore(const std::function<void(int, const std::wstring&)>& log) {
    auto list = LoadState();
    if (list.empty() && !FileExists(g_powerFile)) {
        bool wasOn = FileExists(g_boostFile);
        DeleteFileW(g_boostFile.c_str());
        if (wasOn) log(L_OK, L"부스트 해제");
        else log(L_WARN, L"복원할 앱이 없어요");
        return 0;
    }
    if (!list.empty()) log(L_INFO, L"복원 시작: 앱 " + std::to_wstring(list.size()) + L"개");
    auto run = RunningNames();
    int count = 0;
    for (auto& a : list) {
        if (run.count(Lower(a.name))) { log(L_INFO, L"- " + a.display + L" (이미 실행 중)"); continue; }
        if (!FileExists(a.path)) { log(L_ERR, L"× " + a.display + L" - 파일을 찾을 수 없음"); continue; }
        if (IsGameComponent(a.name, a.path, a.display)) { log(L_INFO, L"- " + a.display + L" (게임 런처 · 안티치트라 다시 켜지 않음)"); continue; }
        if (IsConsoleExe(a.path)) { log(L_INFO, L"- " + a.display + L" (보조 프로그램이라 다시 켜지 않음)"); continue; }
        std::wstring dir = a.path.substr(0, a.path.find_last_of(L'\\'));
        SHELLEXECUTEINFOW sei; ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"open";
        sei.lpFile = a.path.c_str();
        sei.lpParameters = a.args.empty() ? nullptr : a.args.c_str();
        sei.lpDirectory = dir.c_str();
        sei.nShow = SW_SHOWNORMAL;
        static bool elevated = IsElevated();
        if (elevated && RunUnelevated(a.path, a.args, dir)) { log(L_OK, L"✓ " + a.display); count++; }
        else if (ShellExecuteExW(&sei)) { log(L_OK, L"✓ " + a.display); count++; }
        else log(L_ERR, L"× " + a.display + L" - 실행 실패 (오류 " + std::to_wstring(GetLastError()) + L")");
        Sleep(400);
    }
    if (!list.empty()) {
        SaveState({});
        log(L_OK, L"복원 완료: " + std::to_wstring(count) + L"개 실행");
    }
    RestorePower(log);
    DeleteFileW(g_boostFile.c_str());
    return count;
}

static DWORD WINAPI RestoreThread(LPVOID p) {
    WPARAM kind = (WPARAM)(INT_PTR)p;   // 2 = 수동, 3 = 게임 종료 자동
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int n = RestoreCore([](int l, const std::wstring& m) { LogFromThread(l, m); });
    CoUninitialize();
    PostMessageW(g_hwnd, WM_APP_DONE, kind, n);
    return 0;
}

struct TweakJob { std::vector<std::wstring> apply, revert; };

static DWORD WINAPI TweakThread(LPVOID p) {
    TweakJob* job = (TweakJob*)p;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    auto log = [](int l, const std::wstring& m) { LogFromThread(l, m); };
    LogFromThread(L_INFO, L"최적화: 적용 " + std::to_wstring(job->apply.size()) + L"개 · 되돌리기 " + std::to_wstring(job->revert.size()) + L"개");
    int fail = RunTweakBatch(job->apply, job->revert, false, log);   // 사용자 항목

    bool needAdmin = false;
    for (auto& t : BuildTweaks())
        if (t.admin && (ContainsI(job->apply, t.id) || ContainsI(job->revert, t.id))) needAdmin = true;
    if (needAdmin) {
        if (IsElevated()) {
            fail += RunTweakBatch(job->apply, job->revert, true, log);
        } else {
            // 관리자 항목은 관리자 권한으로 자기 자신을 한 번 더 실행해서 처리 (UAC 한 번)
            std::wstring args = L"--tweaks \"apply=" + JoinIds(job->apply) + L";revert=" + JoinIds(job->revert) + L"\" --data \"" + g_dataDir + L"\"";
            SHELLEXECUTEINFOW sei; ZeroMemory(&sei, sizeof(sei));
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            sei.hwnd = g_hwnd;
            sei.lpVerb = L"runas";
            sei.lpFile = g_exePath.c_str();
            sei.lpParameters = args.c_str();
            sei.nShow = SW_HIDE;
            LogFromThread(L_INFO, L"관리자 권한 요청 중...");
            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 120000);
                DWORD code = 1;
                GetExitCodeProcess(sei.hProcess, &code);
                CloseHandle(sei.hProcess);
                if (code == 0) LogFromThread(L_OK, L"관리자 항목 처리 완료");
                else { LogFromThread(L_ERR, L"관리자 항목 일부 실패 (" + std::to_wstring(code) + L"개)"); fail += (int)code; }
            } else {
                DWORD e = GetLastError();
                LogFromThread(L_WARN, e == ERROR_CANCELLED ? L"관리자 권한을 허용하지 않아 관리자 항목은 건너뛰었어요"
                                                           : L"관리자 권한 요청 실패 (오류 " + std::to_wstring(e) + L")");
                fail++;
            }
        }
    }
    ApplyLiveSettings(job->apply, job->revert);
    CoUninitialize();
    PostMessageW(g_hwnd, WM_APP_DONE, 4, fail);
    delete job;
    return 0;
}

// ---------------------------------------------------------------------


// =====================================================================
//  PC 정보 (저사양 / 고사양 판단)
// =====================================================================
static std::wstring g_gpuName;
static unsigned g_vramGB = 0, g_ramGB = 0, g_cores = 0;
static bool g_lowEnd = false;

static void DetectPC() {
    DetectCpu();
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) g_ramGB = (unsigned)((ms.ullTotalPhys + (512ull << 20)) >> 30);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    g_cores = si.dwNumberOfProcessors;
    IDXGIFactory1* f = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) {
        IDXGIAdapter1* a = nullptr;
        SIZE_T best = 0;
        for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory >= best) {
                best = d.DedicatedVideoMemory;
                g_gpuName = Trim(d.Description);
            }
            a->Release();
        }
        f->Release();
        g_vramGB = (unsigned)((best + (512ull << 20)) >> 30);
    }
    // 저사양: RAM 16GB 미만, 논리 코어 8개 미만, 또는 그래픽 메모리 6GB 미만
    g_lowEnd = g_ramGB < 15 || g_cores < 8 || (g_vramGB > 0 && g_vramGB < 6);
}

// =====================================================================
//  관리자 권한 / 윈도우 시작 시 실행
// =====================================================================
static bool RunHidden(const std::wstring& cmdline, DWORD timeoutMs = 15000) {
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    std::wstring c = cmdline;
    if (!CreateProcessW(nullptr, &c[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code == 0;
}

static std::wstring SysExe(const wchar_t* name) {
    wchar_t d[MAX_PATH]; GetSystemDirectoryW(d, MAX_PATH);
    return std::wstring(d) + L"\\" + name;
}

static bool IsTaskStartup() { return RunHidden(L"\"" + SysExe(L"schtasks.exe") + L"\" /Query /TN " + TASK_NAME); }

static bool IsRunKeyStartup() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    bool r = RegQueryValueExW(k, RUN_VALUE, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return r;
}

static bool IsStartupOn() { return IsRunKeyStartup() || IsTaskStartup(); }

static void SetRunKey(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + g_exePath + L"\" --tray";
        RegSetValueExW(k, RUN_VALUE, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, RUN_VALUE);
    RegCloseKey(k);
}

// 관리자 권한이면 작업 스케줄러(가장 높은 권한, 로그인 시)로 등록 -> 매번 UAC 없이 측정 가능
static std::wstring XmlEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'&') o += L"&amp;"; else if (c == L'<') o += L"&lt;"; else if (c == L'>') o += L"&gt;";
        else if (c == L'"') o += L"&quot;"; else if (c == L'\'') o += L"&apos;"; else o += c;
    }
    return o;
}

static void SetStartup(bool on) {
    if (!on) {
        SetRunKey(false);
        if (IsTaskStartup()) RunHidden(L"\"" + SysExe(L"schtasks.exe") + L"\" /Delete /F /TN " + TASK_NAME);   // 권한 없으면 실패할 수 있음
        return;
    }
    if (g_isAdmin) {
        wchar_t userBuf[256] = {}; DWORD ul = 256; GetUserNameW(userBuf, &ul);
        std::wstring user = XmlEscape(userBuf);
        std::wstring xml =
            L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
            L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
            L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + user + L"</UserId><Delay>PT5S</Delay></LogonTrigger></Triggers>\r\n"
            L"<Principals><Principal id=\"Author\"><UserId>" + user + L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
            L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
            L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>6</Priority></Settings>\r\n"
            L"<Actions Context=\"Author\"><Exec><Command>" + XmlEscape(g_exePath) + L"</Command><Arguments>--tray</Arguments></Exec></Actions>\r\n"
            L"</Task>\r\n";
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring path = std::wstring(tmp) + L"FrameBooster_task.xml";
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        bool ok = false;
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wr; WORD bom = 0xFEFF;
            WriteFile(f, &bom, 2, &wr, nullptr);
            WriteFile(f, xml.data(), (DWORD)(xml.size() * 2), &wr, nullptr);
            CloseHandle(f);
            ok = RunHidden(L"\"" + SysExe(L"schtasks.exe") + L"\" /Create /F /TN " + TASK_NAME + L" /XML \"" + path + L"\"");
            DeleteFileW(path.c_str());
        }
        if (ok) { SetRunKey(false); return; }
    }
    SetRunKey(true);
}

// 관리자 권한으로 다시 시작 (현재 창은 닫힘)
static bool RestartAsAdmin() {
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = nullptr; }
    std::wstring dir = g_exePath.substr(0, g_exePath.find_last_of(L'\\'));
    SHELLEXECUTEINFOW sei; ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.hwnd = g_hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = g_exePath.c_str();
    sei.lpParameters = L"--updated";   // 이전 창이 닫힐 때까지 기다렸다가 시작
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) return true;
    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\FrameBooster_SingleInstance");
    return false;
}
