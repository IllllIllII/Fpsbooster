// =====================================================================
//  유틸 (문자열 / 파일)
// =====================================================================
//  문자열 / 파일 유틸
// ---------------------------------------------------------------------
static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
static bool IEq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

static std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) a++;
    while (b > a && iswspace(s[b - 1])) b--;
    return s.substr(a, b - a);
}

static std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring ReadAllText(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    DWORD sz = GetFileSize(h, nullptr), rd = 0;
    std::string s(sz, '\0');
    if (sz) ReadFile(h, &s[0], sz, &rd, nullptr);
    CloseHandle(h);
    s.resize(rd);
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s.erase(0, 3);
    return FromUtf8(s);
}

static bool WriteAllText(const std::wstring& p, const std::wstring& text) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string s = ToUtf8(text);
    DWORD wr = 0;
    BOOL ok = s.empty() ? TRUE : WriteFile(h, s.data(), (DWORD)s.size(), &wr, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}

static void AppendText(const std::wstring& p, const std::wstring& text) {
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string s = ToUtf8(text);
    DWORD wr = 0;
    WriteFile(h, s.data(), (DWORD)s.size(), &wr, nullptr);
    CloseHandle(h);
}

static std::vector<std::wstring> SplitLines(const std::wstring& t) {
    std::vector<std::wstring> out;
    size_t st = 0;
    while (st <= t.size()) {
        size_t e = t.find(L'\n', st);
        if (e == std::wstring::npos) e = t.size();
        std::wstring line = t.substr(st, e - st);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        out.push_back(line);
        st = e + 1;
    }
    return out;
}

static std::wstring Clean(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    return r;
}

static std::wstring FmtNum(unsigned long long v) {
    std::wstring s = std::to_wstring(v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, L",");
    return s;
}

static std::wstring StripExe(const std::wstring& n) {
    if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".exe") == 0) return n.substr(0, n.size() - 4);
    return n;
}

static bool ContainsI(const std::vector<std::wstring>& v, const std::wstring& s) {
    for (auto& x : v) if (IEq(x, s)) return true;
    return false;
}

// ---------------------------------------------------------------------

static std::wstring FmtF(double v, int dec) {
    wchar_t b[64];
    swprintf(b, 64, dec == 0 ? L"%.0f" : dec == 1 ? L"%.1f" : L"%.2f", v);
    return b;
}

static bool EnsureDir(const std::wstring& p) {
    if (CreateDirectoryW(p.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

// =====================================================================
//  설정 / 상태 파일
// =====================================================================
static void DefaultSettings() {
    g_set = Settings();
    for (auto s : kDefaultKeep) g_set.keep.push_back(s);
    for (auto s : kDefaultGames) { g_set.keep.push_back(s); g_set.games.push_back(s); }
}

static void LoadSettings() {
    if (!FileExists(g_settingsFile)) { DefaultSettings(); return; }
    g_set = Settings();
    int sec = 0;
    for (auto& raw : SplitLines(ReadAllText(g_settingsFile))) {
        std::wstring l = Trim(raw);
        if (l.empty()) continue;
        if (l == L"[keep]") { sec = 1; continue; }
        if (l == L"[games]") { sec = 2; continue; }
        if (sec == 0) {
            size_t eq = l.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring k = l.substr(0, eq), v = l.substr(eq + 1);
            bool b = v == L"1";
            if (k == L"autorestore") g_set.autoRestore = b;
            else if (k == L"forcekill") g_set.forceKill = b;
            else if (k == L"memclean") g_set.memClean = b;
            else if (k == L"confirmboost") g_set.confirmBoost = b;
            else if (k == L"powermode") g_set.powerMode = _wtoi(v.c_str());
            else if (k == L"monitor") g_set.monitor = b;
            else if (k == L"overlay") g_set.overlay = b;
            else if (k == L"overlaymode") g_set.overlayMode = std::max(0, std::min(2, _wtoi(v.c_str())));
            else if (k == L"overlaypos") g_set.overlayPos = std::max(0, std::min((int)POS_CUSTOM, _wtoi(v.c_str())));
            else if (k == L"overlayx") g_set.overlayX = (float)_wtof(v.c_str());
            else if (k == L"overlayy") g_set.overlayY = (float)_wtof(v.c_str());
        } else if (sec == 1) { if (!ContainsI(g_set.keep, l)) g_set.keep.push_back(l); }
        else { if (!ContainsI(g_set.games, l)) g_set.games.push_back(l); }
    }
}

static void SaveSettings() {
    auto B = [](bool b) { return std::wstring(b ? L"1" : L"0"); };
    std::wstring t;
    t += L"autorestore=" + B(g_set.autoRestore) + L"\r\n";
    t += L"forcekill=" + B(g_set.forceKill) + L"\r\n";
    t += L"memclean=" + B(g_set.memClean) + L"\r\n";
    t += L"confirmboost=" + B(g_set.confirmBoost) + L"\r\n";
    t += L"powermode=" + std::to_wstring(g_set.powerMode) + L"\r\n";
    t += L"monitor=" + B(g_set.monitor) + L"\r\n";
    t += L"overlay=" + B(g_set.overlay) + L"\r\n";
    t += L"overlaymode=" + std::to_wstring(g_set.overlayMode) + L"\r\n";
    t += L"overlaypos=" + std::to_wstring(g_set.overlayPos) + L"\r\n";
    t += L"overlayx=" + FmtF(g_set.overlayX, 2) + L"\r\noverlayy=" + FmtF(g_set.overlayY, 2) + L"\r\n";
    t += L"[keep]\r\n";
    for (auto& k : g_set.keep) t += k + L"\r\n";
    t += L"[games]\r\n";
    for (auto& g : g_set.games) t += g + L"\r\n";
    WriteAllText(g_settingsFile, t);
}

static std::vector<SavedApp> LoadState() {
    std::vector<SavedApp> v;
    if (!FileExists(g_stateFile)) return v;
    for (auto& l : SplitLines(ReadAllText(g_stateFile))) {
        if (l.empty()) continue;
        std::vector<std::wstring> f;
        size_t st = 0;
        for (;;) {
            size_t e = l.find(L'\t', st);
            f.push_back(l.substr(st, e == std::wstring::npos ? std::wstring::npos : e - st));
            if (e == std::wstring::npos) break;
            st = e + 1;
        }
        if (f.size() < 3) continue;
        SavedApp a; a.name = f[0]; a.display = f[1]; a.path = f[2]; if (f.size() > 3) a.args = f[3];
        v.push_back(a);
    }
    return v;
}

static void SaveState(const std::vector<SavedApp>& v) {
    if (v.empty()) { DeleteFileW(g_stateFile.c_str()); return; }
    std::wstring t;
    for (auto& a : v) t += Clean(a.name) + L"\t" + Clean(a.display) + L"\t" + Clean(a.path) + L"\t" + Clean(a.args) + L"\r\n";
    WriteAllText(g_stateFile, t);
}

static void ReloadPending() {
    g_pending = LoadState().size();
    g_active = g_pending > 0 || FileExists(g_powerFile) || FileExists(g_boostFile);
}

// =====================================================================
//  로그
// =====================================================================
static void AddLog(int level, const std::wstring& msg) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t ts[48];
    swprintf(ts, 48, L"%02d:%02d:%02d  ", st.wHour, st.wMinute, st.wSecond);
    std::wstring line = ts + msg;
    wchar_t ds[16];
    swprintf(ds, 16, L"%04d-%02d-%02d ", st.wYear, st.wMonth, st.wDay);
    AppendText(g_logFile, ds + line + L"\r\n");
    g_logs.push_back({ level, line });
    if (g_logs.size() > 300) g_logs.erase(g_logs.begin(), g_logs.begin() + 50);
}

// 파일 크기 (없으면 0)
static unsigned long long FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

// 작업 기록 파일이 512KB 넘으면 최근 256KB만 남기기
static void TrimLogFile() {
    if (FileSize(g_logFile) <= 512 * 1024) return;
    HANDLE h = CreateFileW(g_logFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    LARGE_INTEGER pos; pos.QuadPart = sz.QuadPart - 256 * 1024;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    std::string buf(256 * 1024, '\0'); DWORD rd = 0;
    ReadFile(h, &buf[0], (DWORD)buf.size(), &rd, nullptr);
    CloseHandle(h);
    buf.resize(rd);
    size_t nl = buf.find('\n');   // 줄 중간에서 자르지 않게
    if (nl != std::string::npos) buf.erase(0, nl + 1);
    h = CreateFileW(g_logFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0; WriteFile(h, buf.data(), (DWORD)buf.size(), &wr, nullptr);
    CloseHandle(h);
}

static void LogFromThread(int level, const std::wstring& msg) {
    PostMessageW(g_hwnd, WM_APP_LOG, (WPARAM)level, (LPARAM)new std::wstring(msg));
}

static void InitPaths() {
    wchar_t p[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
    g_exePath = p;
    std::wstring dir = g_exePath.substr(0, g_exePath.find_last_of(L'\\'));
    std::wstring test = dir + L"\\.fb_write_test";
    HANDLE h = CreateFileW(test.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        g_dataDir = dir;
    } else {
        wchar_t ad[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH);
        g_dataDir = (n ? std::wstring(ad) : dir) + L"\\FrameBooster";
        EnsureDir(g_dataDir);
    }
    g_settingsFile = g_dataDir + L"\\settings.ini";
    g_stateFile = g_dataDir + L"\\closed_apps.txt";
    g_logFile = g_dataDir + L"\\booster.log";
    g_powerFile = g_dataDir + L"\\power_prev.txt";
    g_boostFile = g_dataDir + L"\\boost_on.txt";   // 부스트 켜짐 표시 (끌 앱이 없어도 켜진 상태 유지)
    g_backupFile = g_dataDir + L"\\tweak_backup.txt";
    std::wstring data = g_dataDir + L"\\data";
    EnsureDir(data);
    SetFileAttributesW(data.c_str(), FILE_ATTRIBUTE_HIDDEN);
    g_sessionDir = data + L"\\sessions";
    g_toolDir = data + L"\\tools";
    EnsureDir(g_sessionDir);
    EnsureDir(g_toolDir);
}
