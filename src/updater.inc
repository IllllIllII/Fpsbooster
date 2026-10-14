//  자동 업데이트 (GitHub 릴리스)
//  https://github.com/IllllIllII/Fpsbooster/releases 에 새 버전(tag 예: v2.2)과
//  FrameBooster.exe 를 올리면, 프로그램이 확인해서 스스로 교체하고 다시 켜짐
// ---------------------------------------------------------------------
static const wchar_t* UPDATE_API = L"https://api.github.com/repos/IllllIllII/Fpsbooster/releases/latest";

enum { UPD_IDLE = 0, UPD_CHECKING, UPD_LATEST, UPD_AVAILABLE, UPD_FAILED, UPD_DOWNLOADING, UPD_APPLYING };
static int g_updState = UPD_IDLE;
static std::wstring g_updVer, g_updNotes, g_updUrl, g_updError;
static int g_updProgress = 0;
static bool g_updOverlay = false;
static bool g_updNotified = false;

// http/https GET. 성공 시 HTTP 상태 코드, 연결 실패 시 0
static int HttpGet(const std::wstring& url, std::string& out, bool api, int* progress = nullptr, UINT progMsg = WM_APP_UPD_PROG) {
    out.clear();
    URL_COMPONENTS uc; ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    wchar_t extra[1024] = {};
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return 0;
    std::wstring fullPath = std::wstring(path) + extra;
    std::wstring ua = std::wstring(L"FrameBooster/") + APP_VER;
    HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 8000, 8000, 15000, 30000);
    int status = 0;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r) {
        const wchar_t* hdr = api ? L"Accept: application/vnd.github+json\r\n" : L"Accept: application/octet-stream\r\n";
        if (WinHttpSendRequest(r, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, sz = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            DWORD total = 0; sz = sizeof(total);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &total, &sz, WINHTTP_NO_HEADER_INDEX);
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0) break;
                size_t old = out.size();
                out.resize(old + avail);
                DWORD rd = 0;
                if (!WinHttpReadData(r, &out[old], avail, &rd)) { out.resize(old); status = 0; break; }
                out.resize(old + rd);
                if (progress && total > 0) {
                    int p = (int)(out.size() * 100 / total);
                    if (p != *progress) { *progress = p; PostMessageW(g_hwnd, progMsg, (WPARAM)p, 0); }
                }
                if (out.size() > 64u * 1024 * 1024) { status = 0; break; }   // 안전장치
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return status;
}

// JSON 문자열 값 읽기 ("key":"value") - \n, \", \\, \uXXXX 처리
static bool JsonStr(const std::string& j, const std::string& key, std::wstring& out, size_t from = 0, size_t* endPos = nullptr) {
    std::string k = "\"" + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return false;
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\r' || j[p] == '\n')) p++;
    if (p >= j.size() || j[p] != '"') return false;
    p++;
    std::string raw;
    std::wstring w;
    auto flush = [&]() { if (!raw.empty()) { w += FromUtf8(raw); raw.clear(); } };
    while (p < j.size() && j[p] != '"') {
        char ch = j[p];
        if (ch == '\\' && p + 1 < j.size()) {
            char e = j[p + 1];
            p += 2;
            switch (e) {
            case 'n': raw += '\n'; break;
            case 'r': break;
            case 't': raw += ' '; break;
            case 'u':
                if (p + 4 <= j.size()) {
                    flush();
                    w += (wchar_t)strtoul(j.substr(p, 4).c_str(), nullptr, 16);
                    p += 4;
                }
                break;
            default: raw += e; break;
            }
            continue;
        }
        raw += ch;
        p++;
    }
    flush();
    out = w;
    if (endPos) *endPos = p;
    return true;
}

// "v2.10.1" -> {2,10,1}
static std::vector<int> ParseVer(const std::wstring& v) {
    std::vector<int> n;
    int cur = -1;
    for (wchar_t c : v) {
        if (c >= L'0' && c <= L'9') cur = (cur < 0 ? 0 : cur * 10) + (c - L'0');
        else if (cur >= 0) { n.push_back(cur); cur = -1; }
    }
    if (cur >= 0) n.push_back(cur);
    return n;
}

static bool IsNewer(const std::wstring& remote, const std::wstring& local) {
    auto a = ParseVer(remote), b = ParseVer(local);
    size_t n = std::max(a.size(), b.size());
    a.resize(n, 0); b.resize(n, 0);
    return a > b;
}

static std::wstring UpdateApiUrl() {
    wchar_t env[1024] = {};
    if (GetEnvironmentVariableW(L"FB_UPDATE_API", env, 1024) > 0) return env;   // 테스트용
    return UPDATE_API;
}

static DWORD WINAPI UpdateCheckThread(LPVOID) {
    std::string body;
    int code = HttpGet(UpdateApiUrl(), body, true);
    int result = 0;   // 0 최신, 1 새 버전, 2 실패
    if (code == 404) result = 0;                  // 아직 릴리스 없음
    else if (code != 200) { result = 2; g_updError = code ? L"서버 응답 " + std::to_wstring(code) : L"인터넷 연결 확인"; }
    else {
        std::wstring tag, notes, url;
        JsonStr(body, "tag_name", tag);
        JsonStr(body, "body", notes);
        // 첨부 파일 중 FrameBooster.exe (없으면 첫 .exe)
        size_t pos = 0;
        std::wstring any;
        for (;;) {
            std::wstring u; size_t e = 0;
            if (!JsonStr(body, "browser_download_url", u, pos, &e)) break;
            pos = e;
            std::wstring lu = Lower(u);
            if (lu.size() > 4 && lu.compare(lu.size() - 4, 4, L".exe") == 0) {
                if (any.empty()) any = u;
                if (lu.find(L"/framebooster.exe") != std::wstring::npos) { url = u; break; }
            }
        }
        if (url.empty()) url = any;
        if (!tag.empty() && IsNewer(tag, APP_VER) && !url.empty()) {
            g_updVer = tag; g_updNotes = notes; g_updUrl = url;
            result = 1;
        }
    }
    PostMessageW(g_hwnd, WM_APP_UPD_CHECK, (WPARAM)result, 0);
    return 0;
}

static std::wstring ExeDir() { return g_exePath.substr(0, g_exePath.find_last_of(L'\\')); }
static std::wstring NewExePath() { return ExeDir() + L"\\FrameBooster.new.exe"; }
static std::wstring OldExePath() { return ExeDir() + L"\\FrameBooster.old.exe"; }

static DWORD WINAPI UpdateDownloadThread(LPVOID) {
    std::string data;
    int prog = 0;
    int code = HttpGet(g_updUrl, data, false, &prog);
    bool ok = false;
    if (code == 200 && data.size() > 100 * 1024 && data[0] == 'M' && data[1] == 'Z') {   // exe 파일인지 확인
        HANDLE f = CreateFileW(NewExePath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wr = 0;
            ok = WriteFile(f, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
            CloseHandle(f);
            if (!ok) DeleteFileW(NewExePath().c_str());
            else g_updError.clear();
        } else g_updError = L"폴더에 쓸 수 없어요";
    } else {
        g_updError = code == 200 ? L"받은 파일이 올바르지 않아요" : (code ? L"서버 응답 " + std::to_wstring(code) : L"인터넷 연결 확인");
    }
    PostMessageW(g_hwnd, WM_APP_UPD_DONE, ok ? 1 : 0, 0);
    return 0;
}

// 받은 새 exe로 교체하고 다시 실행
static bool ApplyUpdateAndRestart() {
    std::wstring cur = g_exePath, old = OldExePath(), nw = NewExePath();
    DeleteFileW(old.c_str());
    if (!MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) { g_updError = L"실행 파일을 바꿀 수 없어요"; return false; }
    if (!MoveFileExW(nw.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);   // 되돌리기
        g_updError = L"새 파일로 바꾸지 못했어요";
        return false;
    }
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = nullptr; }
    std::wstring dir = ExeDir();
    SHELLEXECUTEINFOW sei; ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpFile = cur.c_str();
    sei.lpParameters = L"--updated";
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        MoveFileExW(cur.c_str(), nw.c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);
        g_updError = L"새 버전을 실행하지 못했어요";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
