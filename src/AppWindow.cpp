#include "AppWindow.h"
#include "ProtocolCode.h"

#include <commctrl.h>
#include <algorithm>
#include <cwchar>

#pragma comment(lib, "comctl32.lib")

namespace mycobot {

namespace {
constexpr wchar_t kClassName[] = L"MyCobotAppWindowClass";

enum ControlId : int {
    ID_EDIT_PORT = 100,
    ID_EDIT_BAUD,
    ID_BTN_CONNECT,
    ID_BTN_DISCONNECT,
    ID_BTN_POWERON,
    ID_BTN_POWEROFF,
    ID_ST_CONN,

    ID_BTN_FREEDRAG,
    ID_BTN_RECORD_A,
    ID_BTN_RECORD_B,
    ID_ST_CALIB,

    ID_EDIT_ZLIFT,
    ID_EDIT_SPEED,
    ID_EDIT_SPACING,

    ID_WHITEBOARD,
    ID_BTN_CLEAR,
    ID_BTN_UNDO,
    ID_BTN_SEND,
    ID_BTN_STOP,
    ID_PROGRESS,
    ID_ST_PROGRESS,

    ID_EDIT_LOG,
};

constexpr int kRowH = 30;
constexpr int kMargin = 8;
constexpr int kSidePanelW = 140;
constexpr int kLogH = 110;

std::wstring GetEditText(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring s(len, L'\0');
    if (len > 0) GetWindowTextW(h, s.data(), len + 1);
    return s;
}

// Parses a double from an Edit control; returns false if not a valid number.
bool GetEditDouble(HWND h, double& out) {
    std::wstring s = GetEditText(h);
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    double v = wcstod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    out = v;
    return true;
}

HWND MakeStatic(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
                            nullptr, hInst, nullptr);
}

HWND MakeEdit(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h,
              int id) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w, h, parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
    return e;
}

HWND MakeButton(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h,
                 int id) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h,
                            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst,
                            nullptr);
}

HFONT g_uiFont = nullptr;

void ApplyFontToChildren(HWND parent, HFONT font) {
    EnumChildWindows(
        parent,
        [](HWND h, LPARAM lp) -> BOOL {
            SendMessageW(h, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(font));
}

} // namespace

void AppWindow::RegisterClass(HINSTANCE hInst) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    Whiteboard::RegisterClass(hInst);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &AppWindow::WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
}

HWND AppWindow::Create(HINSTANCE hInst, int nCmdShow) {
    hInst_ = hInst;
    hwnd_ = CreateWindowExW(0, kClassName, L"myCobot 360 M5 白板繪圖控制",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 980, 760, nullptr,
                             nullptr, hInst, this);
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    return hwnd_;
}

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* self;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(hwnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            OnCreate(hwnd, hInst_);
            return 0;
        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_APP_PROGRESS: {
            int sent = static_cast<int>(wParam);
            int total = static_cast<int>(lParam);
            if (progressBar_) {
                SendMessageW(progressBar_, PBM_SETRANGE32, 0, total);
                SendMessageW(progressBar_, PBM_SETPOS, sent, 0);
            }
            wchar_t buf[64];
            swprintf_s(buf, L"%d / %d 點", sent, total);
            SetWindowTextW(stProgress_, buf);
            return 0;
        }
        case WM_APP_LOG: {
            wchar_t* text = reinterpret_cast<wchar_t*>(lParam);
            Log(text);
            delete[] text;
            return 0;
        }
        case WM_APP_DONE: {
            sending_ = false;
            whiteboard_.SetInputEnabled(true);
            UpdateButtonStates();
            return 0;
        }
        case WM_CLOSE:
            if (sending_) sender_.Cancel(serial_);
            sender_.Join();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            OnDestroy();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void AppWindow::OnCreate(HWND hwnd, HINSTANCE hInst) {
    hwnd_ = hwnd;

    if (!g_uiFont) {
        g_uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
    }

    int x = kMargin, y = kMargin;

    // Row 1: connection
    MakeStatic(hwnd, hInst, L"COM Port:", x, y, 60, kRowH); x += 62;
    edPort_ = MakeEdit(hwnd, hInst, L"COM5", x, y, 60, kRowH, ID_EDIT_PORT); x += 66;
    MakeStatic(hwnd, hInst, L"Baud:", x, y, 40, kRowH); x += 42;
    edBaud_ = MakeEdit(hwnd, hInst, L"115200", x, y, 60, kRowH, ID_EDIT_BAUD); x += 66;
    btnConnect_ = MakeButton(hwnd, hInst, L"連線", x, y, 70, kRowH, ID_BTN_CONNECT); x += 74;
    btnDisconnect_ = MakeButton(hwnd, hInst, L"斷線", x, y, 70, kRowH, ID_BTN_DISCONNECT); x += 74;
    btnPowerOn_ = MakeButton(hwnd, hInst, L"開啟馬達", x, y, 80, kRowH, ID_BTN_POWERON); x += 84;
    btnPowerOff_ = MakeButton(hwnd, hInst, L"關閉馬達", x, y, 80, kRowH, ID_BTN_POWEROFF); x += 84;
    stConn_ = MakeStatic(hwnd, hInst, L"狀態：未連線", x, y, 260, kRowH);

    // Row 2: calibration
    x = kMargin; y += kRowH + 4;
    btnFreeDrag_ = MakeButton(hwnd, hInst, L"進入自由拖曳模式", x, y, 150, kRowH, ID_BTN_FREEDRAG);
    x += 154;
    btnRecordA_ = MakeButton(hwnd, hInst, L"記錄左上角 A", x, y, 120, kRowH, ID_BTN_RECORD_A);
    x += 124;
    btnRecordB_ = MakeButton(hwnd, hInst, L"記錄右下角 B", x, y, 120, kRowH, ID_BTN_RECORD_B);
    x += 124;
    stCalib_ = MakeStatic(hwnd, hInst, L"校正：尚未設定 A、B 兩點", x, y, 360, kRowH);

    // Row 3: draw parameters
    x = kMargin; y += kRowH + 4;
    MakeStatic(hwnd, hInst, L"抬筆高度(mm):", x, y, 100, kRowH); x += 102;
    edZLift_ = MakeEdit(hwnd, hInst, L"20", x, y, 50, kRowH, ID_EDIT_ZLIFT); x += 60;
    MakeStatic(hwnd, hInst, L"繪製速度(1-100):", x, y, 110, kRowH); x += 112;
    edSpeed_ = MakeEdit(hwnd, hInst, L"30", x, y, 50, kRowH, ID_EDIT_SPEED); x += 60;
    MakeStatic(hwnd, hInst, L"點間距(mm):", x, y, 90, kRowH); x += 92;
    edSpacing_ = MakeEdit(hwnd, hInst, L"3", x, y, 50, kRowH, ID_EDIT_SPACING);

    y += kRowH + 6;

    // Row 4: whiteboard canvas (real position/size set in Layout()) + side buttons
    whiteboard_.Create(hwnd, hInst, kMargin, y, 100, 100, ID_WHITEBOARD);
    btnClear_ = MakeButton(hwnd, hInst, L"清除畫布", 0, 0, kSidePanelW - 8, kRowH, ID_BTN_CLEAR);
    btnUndo_ = MakeButton(hwnd, hInst, L"復原上一筆", 0, 0, kSidePanelW - 8, kRowH, ID_BTN_UNDO);
    btnSend_ = MakeButton(hwnd, hInst, L"傳送至機械臂繪製", 0, 0, kSidePanelW - 8, kRowH, ID_BTN_SEND);
    btnStop_ = MakeButton(hwnd, hInst, L"緊急停止 STOP", 0, 0, kSidePanelW - 8, 44, ID_BTN_STOP);
    progressBar_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 0, 0,
                                    kSidePanelW - 8, 18, hwnd, reinterpret_cast<HMENU>(ID_PROGRESS),
                                    hInst, nullptr);
    stProgress_ = MakeStatic(hwnd, hInst, L"", 0, 0, kSidePanelW - 8, 18);

    // Bottom: log box
    edLog_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                  ES_AUTOVSCROLL,
                              0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_LOG)),
                              hInst, nullptr);

    if (g_uiFont) ApplyFontToChildren(hwnd, g_uiFont);

    // Make the STOP button visually stand out.
    // (BUTTON control colors are limited without owner-draw; a clear label is
    // deliberately used instead of colored owner-draw to keep this file simple
    // and robust across Windows theming.)

    UpdateButtonStates();
    Layout();
    Log(L"程式已啟動。請先設定 COM Port 並按「連線」。");
}

void AppWindow::Layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right, h = rc.bottom;

    int top = kMargin * 2 + kRowH * 3 + 4 * 2 + 6; // area below the three parameter rows
    int bottom = h - kLogH - kMargin;
    int boardW = std::max(100, w - kSidePanelW - kMargin * 3);
    int boardH = std::max(100, bottom - top);

    MoveWindow(whiteboard_.hwnd(), kMargin, top, boardW, boardH, TRUE);
    calib_.SetCanvasSize(boardW, boardH);

    int sx = kMargin * 2 + boardW;
    int sy = top;
    MoveWindow(btnClear_, sx, sy, kSidePanelW - 8, kRowH, TRUE); sy += kRowH + 6;
    MoveWindow(btnUndo_, sx, sy, kSidePanelW - 8, kRowH, TRUE); sy += kRowH + 6;
    MoveWindow(btnSend_, sx, sy, kSidePanelW - 8, kRowH, TRUE); sy += kRowH + 14;
    MoveWindow(btnStop_, sx, sy, kSidePanelW - 8, 44, TRUE); sy += 44 + 14;
    MoveWindow(progressBar_, sx, sy, kSidePanelW - 8, 18, TRUE); sy += 22;
    MoveWindow(stProgress_, sx, sy, kSidePanelW - 8, 18, TRUE);

    MoveWindow(edLog_, kMargin, h - kLogH, w - kMargin * 2, kLogH - kMargin, TRUE);
}

void AppWindow::OnSize(int, int) { Layout(); }

void AppWindow::Log(const std::wstring& msg) {
    int len = GetWindowTextLengthW(edLog_);
    SendMessageW(edLog_, EM_SETSEL, len, len);
    std::wstring line = msg + L"\r\n";
    SendMessageW(edLog_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

void AppWindow::RefreshStatusLabels() {
    if (serial_.IsOpen()) {
        SetWindowTextW(stConn_, L"狀態：已連線");
    } else {
        SetWindowTextW(stConn_, L"狀態：未連線");
    }

    if (calib_.IsComplete()) {
        wchar_t buf[256];
        swprintf_s(buf,
                    L"校正：A=(%.1f, %.1f) B=(%.1f, %.1f) Z=%.1f",
                    calib_.a.x, calib_.a.y, calib_.b.x, calib_.b.y, calib_.PenDownZ());
        SetWindowTextW(stCalib_, buf);
    } else {
        std::wstring s = L"校正：尚未完成（";
        s += calib_.hasA ? L"已記錄 A" : L"未記錄 A";
        s += L"，";
        s += calib_.hasB ? L"已記錄 B" : L"未記錄 B";
        s += L"）";
        SetWindowTextW(stCalib_, s.c_str());
    }
}

void AppWindow::UpdateButtonStates() {
    bool connected = serial_.IsOpen();
    EnableWindow(btnConnect_, !connected);
    EnableWindow(btnDisconnect_, connected);
    EnableWindow(btnPowerOn_, connected && !sending_);
    EnableWindow(btnPowerOff_, connected && !sending_ && !freeDrag_);
    EnableWindow(btnFreeDrag_, connected && !sending_);
    EnableWindow(btnRecordA_, connected && !sending_);
    EnableWindow(btnRecordB_, connected && !sending_);
    EnableWindow(btnClear_, !sending_);
    EnableWindow(btnUndo_, !sending_);
    EnableWindow(btnSend_, connected && !sending_ && !freeDrag_ && calib_.IsComplete() &&
                                !whiteboard_.Empty());
    EnableWindow(btnStop_, connected);
    SetWindowTextW(btnFreeDrag_, freeDrag_ ? L"鎖定並離開拖曳模式" : L"進入自由拖曳模式");
    RefreshStatusLabels();
}

bool AppWindow::ReadDrawParams(DrawParams& out) {
    double z, speed, spacing;
    if (!GetEditDouble(edZLift_, z) || z < 0 || z > 200) {
        Log(L"抬筆高度需為 0~200 的數字。");
        return false;
    }
    if (!GetEditDouble(edSpeed_, speed) || speed < 1 || speed > 100) {
        Log(L"繪製速度需為 1~100 的數字。");
        return false;
    }
    if (!GetEditDouble(edSpacing_, spacing) || spacing < 0.5 || spacing > 50) {
        Log(L"點間距需為 0.5~50 的數字。");
        return false;
    }
    out.zLiftMm = z;
    out.speedPct = static_cast<int>(speed);
    out.minSpacingMm = spacing;
    out.travelSpeedPct = std::min(100, out.speedPct + 10);
    return true;
}

void AppWindow::OnCommand(int id, int notifyCode) {
    if (notifyCode != BN_CLICKED) return;
    switch (id) {
        case ID_BTN_CONNECT: DoConnect(); break;
        case ID_BTN_DISCONNECT: DoDisconnect(); break;
        case ID_BTN_POWERON: DoPowerOn(); break;
        case ID_BTN_POWEROFF: DoPowerOff(); break;
        case ID_BTN_FREEDRAG: DoToggleFreeDrag(); break;
        case ID_BTN_RECORD_A: DoRecordCorner(true); break;
        case ID_BTN_RECORD_B: DoRecordCorner(false); break;
        case ID_BTN_CLEAR: whiteboard_.Clear(); UpdateButtonStates(); break;
        case ID_BTN_UNDO: whiteboard_.UndoLastStroke(); UpdateButtonStates(); break;
        case ID_BTN_SEND: DoSend(); break;
        case ID_BTN_STOP: DoStop(); break;
    }
}

void AppWindow::DoConnect() {
    std::wstring port = GetEditText(edPort_);
    std::wstring baudStr = GetEditText(edBaud_);
    DWORD baud = static_cast<DWORD>(wcstoul(baudStr.c_str(), nullptr, 10));
    if (baud == 0) baud = 115200;

    std::wstring err = serial_.Open(port, baud);
    if (!err.empty()) {
        Log(L"連線失敗：" + err);
    } else {
        Log(L"已開啟 " + port + L"（" + std::to_wstring(baud) + L" bps）。");
        TriState connected = serial_.IsControllerConnected();
        if (connected == TriState::True) {
            Log(L"控制器回應正常。");
        } else {
            Log(L"警告：無法確認控制器回應，請確認連接埠與韌體是否正確。");
        }
    }
    UpdateButtonStates();
}

void AppWindow::DoDisconnect() {
    if (sending_) sender_.Cancel(serial_);
    sender_.Join();
    serial_.Close();
    freeDrag_ = false;
    Log(L"已斷線。");
    UpdateButtonStates();
}

void AppWindow::DoPowerOn() {
    if (serial_.PowerOn()) {
        Log(L"已送出「開啟馬達」指令。");
    } else {
        Log(L"開啟馬達失敗：" + serial_.LastError());
    }
    UpdateButtonStates();
}

void AppWindow::DoPowerOff() {
    if (serial_.PowerOff()) {
        Log(L"已送出「關閉馬達」指令。");
    } else {
        Log(L"關閉馬達失敗：" + serial_.LastError());
    }
    UpdateButtonStates();
}

void AppWindow::DoToggleFreeDrag() {
    if (!freeDrag_) {
        if (serial_.ReleaseAllServos()) {
            freeDrag_ = true;
            Log(L"已進入自由拖曳模式：可用手直接移動機械臂到畫紙角落。");
        } else {
            Log(L"進入自由拖曳模式失敗：" + serial_.LastError());
        }
    } else {
        auto angles = serial_.GetAngles();
        if (angles) {
            serial_.SendAngles(*angles, 20); // gently re-engage torque at the current pose
            freeDrag_ = false;
            Log(L"已鎖定目前姿態，離開自由拖曳模式。");
        } else {
            Log(L"讀取目前角度失敗，無法安全鎖定：" + serial_.LastError());
        }
    }
    UpdateButtonStates();
}

void AppWindow::DoRecordCorner(bool isA) {
    auto coords = serial_.GetCoords();
    if (!coords) {
        Log(L"讀取座標失敗：" + serial_.LastError());
        return;
    }
    if (isA) {
        calib_.a = *coords;
        calib_.hasA = true;
        Log(L"已記錄左上角 A。");
    } else {
        calib_.b = *coords;
        calib_.hasB = true;
        Log(L"已記錄右下角 B。");
    }
    UpdateButtonStates();
}

void AppWindow::DoSend() {
    DrawParams params;
    if (!ReadDrawParams(params)) return;
    if (!calib_.IsComplete()) {
        Log(L"請先完成校正（記錄 A、B 兩點）。");
        return;
    }
    if (whiteboard_.Empty()) {
        Log(L"畫布是空的，沒有東西可以送出。");
        return;
    }

    sending_ = true;
    whiteboard_.SetInputEnabled(false);
    UpdateButtonStates();
    SendMessageW(progressBar_, PBM_SETPOS, 0, 0);
    SetWindowTextW(stProgress_, L"");

    if (!sender_.Start(whiteboard_.GetStrokes(), calib_, serial_, params, hwnd_)) {
        Log(L"無法啟動繪製工作。");
        sending_ = false;
        whiteboard_.SetInputEnabled(true);
        UpdateButtonStates();
    }
}

void AppWindow::DoStop() {
    if (sending_) {
        sender_.Cancel(serial_);
        Log(L"已送出停止指令。");
    } else {
        serial_.Stop();
        Log(L"已送出停止指令（目前並非繪製中）。");
    }
}

void AppWindow::OnDestroy() {
    if (sending_) sender_.Cancel(serial_);
    sender_.Join();
    serial_.Close();
}

} // namespace mycobot
