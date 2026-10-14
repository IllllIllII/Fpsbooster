//  트레이
// ---------------------------------------------------------------------
static HICON LoadAppIcon(int size) {
    return (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, 0);
}

static void TrayAdd() {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadAppIcon(GetSystemMetrics(SM_CXSMICON));
    wcsncpy(g_nid.szTip, APP_NAME, ARRAYSIZE(g_nid.szTip) - 1);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void TrayTip(const std::wstring& t) {
    g_nid.uFlags = NIF_TIP;
    wcsncpy(g_nid.szTip, t.c_str(), ARRAYSIZE(g_nid.szTip) - 1);
    g_nid.szTip[ARRAYSIZE(g_nid.szTip) - 1] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void Balloon(const std::wstring& text) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfo, text.c_str(), ARRAYSIZE(n.szInfo) - 1);
    n.szInfo[ARRAYSIZE(n.szInfo) - 1] = 0;
    wcsncpy(n.szInfoTitle, APP_NAME, ARRAYSIZE(n.szInfoTitle) - 1);
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

// ---------------------------------------------------------------------
