#include "AppWindow.h"
#include "Kinematics.h"
#include "ProtocolCode.h"

#include <algorithm>
#include <chrono>
#include <commctrl.h>
#include <cwchar>
#include <windowsx.h>

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
    ID_BTN_DEMO_CALIB,
    ID_ST_CALIB,

    ID_EDIT_ZLIFT,
    ID_EDIT_SPEED,
    ID_EDIT_SPACING,
    ID_CB_SIMULATE,
    ID_CB_TELEMETRY,

    ID_BTN_CLEAR,
    ID_BTN_UNDO,
    ID_BTN_SEND,
    ID_BTN_STOP,
    ID_BTN_CLEAR_TRACE,
    ID_PROGRESS,
    ID_ST_PROGRESS,

    ID_WHITEBOARD,
    ID_SIMVIEW,
    ID_EDIT_LOG,
};

constexpr int kRowH = 30;
constexpr int kMargin = 8;
constexpr int kLogH = 120;
constexpr int kMainTop = 148; // below the four control rows
constexpr double kBoardShare = 0.46;

std::wstring GetEditText(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring s(len, L'\0');
    if (len > 0) GetWindowTextW(h, s.data(), len + 1);
    return s;
}

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
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, x, y, w, h,
                            parent, nullptr, hInst, nullptr);
}

HWND MakeEdit(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h,
              int id) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w, h, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
}

HWND MakeButton(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h,
                 int id) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h,
                            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst,
                            nullptr);
}

HWND MakeCheckbox(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h,
                   int id, bool checked) {
    HWND cb = CreateWindowExW(0, L"BUTTON", text,
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_VCENTER, x, y, w, h,
                               parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst,
                               nullptr);
    SendMessageW(cb, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return cb;
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
    SimView::RegisterClass(hInst);

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
    hwnd_ = CreateWindowExW(0, kClassName, L"myCobot 360 M5 白板繪圖控制 + 3D 模擬",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1320, 900, nullptr,
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
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 1060;
            mmi->ptMinTrackSize.y = 620;
            return 0;
        }
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_MOUSEWHEEL: {
            // Let the wheel zoom the 3D view without having to click it first.
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc;
            if (sim_.hwnd() && GetWindowRect(sim_.hwnd(), &rc) && PtInRect(&rc, pt)) {
                sim_.HandleWheel(GET_WHEEL_DELTA_WPARAM(wParam));
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
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
        case WM_APP_POSE: {
            auto* update = reinterpret_cast<PoseUpdate*>(lParam);
            OnPoseUpdate(update);
            delete update;
            return 0;
        }
        case WM_APP_UNREACHABLE: {
            auto* points = reinterpret_cast<std::vector<Vec3>*>(lParam);
            sim_.SetUnreachableMarkers(*points);
            delete points;
            return 0;
        }
        case WM_APP_STROKES_CHANGED:
            RefreshPlanPreview();
            UpdateButtonStates();
            return 0;
        case WM_APP_DONE: {
            sending_ = false;
            whiteboard_.SetInputEnabled(true);
            UpdateButtonStates();
            return 0;
        }
        case WM_CLOSE:
            if (sending_.load()) sender_.Cancel(serial_);
            sender_.Join();
            StopTelemetry();
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

    // Row 1: connection
    int y = kMargin;
    MakeStatic(hwnd, hInst, L"COM Port:", 8, y, 62, kRowH);
    edPort_ = MakeEdit(hwnd, hInst, L"COM5", 72, y, 60, kRowH, ID_EDIT_PORT);
    MakeStatic(hwnd, hInst, L"Baud:", 138, y, 40, kRowH);
    edBaud_ = MakeEdit(hwnd, hInst, L"115200", 180, y, 62, kRowH, ID_EDIT_BAUD);
    btnConnect_ = MakeButton(hwnd, hInst, L"連線", 248, y, 70, kRowH, ID_BTN_CONNECT);
    btnDisconnect_ = MakeButton(hwnd, hInst, L"斷線", 322, y, 70, kRowH, ID_BTN_DISCONNECT);
    btnPowerOn_ = MakeButton(hwnd, hInst, L"開啟馬達", 396, y, 84, kRowH, ID_BTN_POWERON);
    btnPowerOff_ = MakeButton(hwnd, hInst, L"關閉馬達", 484, y, 84, kRowH, ID_BTN_POWEROFF);
    stConn_ = MakeStatic(hwnd, hInst, L"狀態：未連線", 576, y, 300, kRowH);

    // Row 2: calibration
    y += kRowH + 4;
    btnFreeDrag_ = MakeButton(hwnd, hInst, L"進入自由拖曳模式", 8, y, 150, kRowH, ID_BTN_FREEDRAG);
    btnRecordA_ = MakeButton(hwnd, hInst, L"記錄左上角 A", 162, y, 120, kRowH, ID_BTN_RECORD_A);
    btnRecordB_ = MakeButton(hwnd, hInst, L"記錄右下角 B", 286, y, 120, kRowH, ID_BTN_RECORD_B);
    btnDemoCalib_ =
        MakeButton(hwnd, hInst, L"載入示範校正（僅模擬）", 410, y, 170, kRowH, ID_BTN_DEMO_CALIB);
    stCalib_ = MakeStatic(hwnd, hInst, L"校正：尚未設定 A、B 兩點", 586, y, 460, kRowH);

    // Row 3: draw parameters + mode switches
    y += kRowH + 4;
    MakeStatic(hwnd, hInst, L"抬筆高度(mm):", 8, y, 100, kRowH);
    edZLift_ = MakeEdit(hwnd, hInst, L"20", 110, y, 50, kRowH, ID_EDIT_ZLIFT);
    MakeStatic(hwnd, hInst, L"繪製速度(1-100):", 168, y, 112, kRowH);
    edSpeed_ = MakeEdit(hwnd, hInst, L"30", 282, y, 50, kRowH, ID_EDIT_SPEED);
    MakeStatic(hwnd, hInst, L"點間距(mm):", 340, y, 90, kRowH);
    edSpacing_ = MakeEdit(hwnd, hInst, L"3", 432, y, 50, kRowH, ID_EDIT_SPACING);
    cbSimulate_ = MakeCheckbox(hwnd, hInst, L"模擬模式（不連機械臂）", 496, y, 190, kRowH,
                                ID_CB_SIMULATE, false);
    cbTelemetry_ = MakeCheckbox(hwnd, hInst, L"同步真實手臂姿態", 692, y, 160, kRowH,
                                 ID_CB_TELEMETRY, true);

    // Row 4: actions + progress
    y += kRowH + 4;
    btnClear_ = MakeButton(hwnd, hInst, L"清除畫布", 8, y, 100, kRowH, ID_BTN_CLEAR);
    btnUndo_ = MakeButton(hwnd, hInst, L"復原上一筆", 112, y, 100, kRowH, ID_BTN_UNDO);
    btnSend_ = MakeButton(hwnd, hInst, L"傳送至機械臂繪製", 216, y, 170, kRowH, ID_BTN_SEND);
    btnStop_ = MakeButton(hwnd, hInst, L"緊急停止 STOP", 390, y, 120, kRowH, ID_BTN_STOP);
    btnClearTrace_ = MakeButton(hwnd, hInst, L"清除軌跡", 514, y, 100, kRowH, ID_BTN_CLEAR_TRACE);
    progressBar_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 622, y + 5, 200,
                                    20, hwnd, reinterpret_cast<HMENU>(ID_PROGRESS), hInst, nullptr);
    stProgress_ = MakeStatic(hwnd, hInst, L"", 830, y, 140, kRowH);

    // Main area (real geometry assigned in Layout())
    whiteboard_.Create(hwnd, hInst, kMargin, kMainTop, 100, 100, ID_WHITEBOARD);
    sim_.Create(hwnd, hInst, kMargin, kMainTop, 100, 100, ID_SIMVIEW);

    edLog_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                  ES_AUTOVSCROLL,
                              0, 0, 100, 100, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_LOG)), hInst,
                              nullptr);

    if (g_uiFont) ApplyFontToChildren(hwnd, g_uiFont);

    Layout();
    UpdateButtonStates();
    RefreshPlanPreview();
    Log(L"程式已啟動。請先設定 COM Port 並按「連線」。");
    Log(L"沒有機械臂也可以試：按「載入示範校正（僅模擬）」→ 在左邊畫圖 → 勾選「模擬模式」→ 按下按鈕即可在右邊 3D 視圖看到手臂走一遍。");
}

void AppWindow::Layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right, h = rc.bottom;

    int bottom = h - kLogH - kMargin;
    int areaH = std::max(120, bottom - kMainTop);
    int avail = std::max(300, w - kMargin * 3);
    int boardW = static_cast<int>(avail * kBoardShare);
    int simW = avail - boardW;

    MoveWindow(whiteboard_.hwnd(), kMargin, kMainTop, boardW, areaH, TRUE);
    calib_.SetCanvasSize(boardW, areaH);
    MoveWindow(sim_.hwnd(), kMargin * 2 + boardW, kMainTop, simW, areaH, TRUE);
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
    SetWindowTextW(stConn_, serial_.IsOpen() ? L"狀態：已連線" : L"狀態：未連線");

    if (calib_.IsComplete()) {
        wchar_t buf[256];
        swprintf_s(buf, L"校正%s：A=(%.1f, %.1f) B=(%.1f, %.1f) Z=%.1f",
                    calibIsDemo_ ? L"（示範值）" : L"", calib_.a.x, calib_.a.y, calib_.b.x,
                    calib_.b.y, calib_.PenDownZ());
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
    bool busy = sending_.load();
    bool simMode = IsChecked(cbSimulate_);

    EnableWindow(btnConnect_, !connected);
    EnableWindow(btnDisconnect_, connected);
    EnableWindow(btnPowerOn_, connected && !busy);
    EnableWindow(btnPowerOff_, connected && !busy && !freeDrag_);
    EnableWindow(btnFreeDrag_, connected && !busy);
    EnableWindow(btnRecordA_, connected && !busy);
    EnableWindow(btnRecordB_, connected && !busy);
    EnableWindow(btnDemoCalib_, !busy);
    EnableWindow(btnClear_, !busy);
    EnableWindow(btnUndo_, !busy);
    EnableWindow(btnClearTrace_, !busy);
    EnableWindow(cbSimulate_, !busy);
    EnableWindow(cbTelemetry_, !busy);
    EnableWindow(btnStop_, connected || busy);

    bool canSend = !busy && calib_.IsComplete() && !whiteboard_.Empty() &&
                   (simMode || (connected && !freeDrag_));
    EnableWindow(btnSend_, canSend);
    SetWindowTextW(btnSend_, simMode ? L"在 3D 模擬中預覽" : L"傳送至機械臂繪製");
    SetWindowTextW(btnFreeDrag_, freeDrag_ ? L"鎖定並離開拖曳模式" : L"進入自由拖曳模式");

    if (busy) {
        sim_.SetStatusText(simMode ? L"模擬中（未送到機械臂）" : L"繪製中：即時同步");
    } else if (connected) {
        sim_.SetStatusText(telemetryRunning_.load() ? L"即時同步真實手臂" : L"已連線（未同步姿態）");
    } else {
        sim_.SetStatusText(L"3D 模擬視圖（未連線）");
    }

    RefreshStatusLabels();
}

void AppWindow::RefreshPlanPreview() {
    DrawParams params;
    ReadDrawParams(params, /*quiet=*/true);
    sim_.SetPaper(calib_);
    sim_.SetPlannedPath(BuildPlan(whiteboard_.GetStrokes(), calib_, params));
}

bool AppWindow::IsChecked(HWND checkbox) const {
    return checkbox && SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool AppWindow::ReadDrawParams(DrawParams& out, bool quiet) {
    bool ok = true;
    double v = 0;

    if (GetEditDouble(edZLift_, v) && v >= 0 && v <= 200) {
        out.zLiftMm = v;
    } else {
        ok = false;
        if (!quiet) Log(L"抬筆高度需為 0~200 的數字。");
    }
    if (GetEditDouble(edSpeed_, v) && v >= 1 && v <= 100) {
        out.speedPct = static_cast<int>(v);
    } else {
        ok = false;
        if (!quiet) Log(L"繪製速度需為 1~100 的數字。");
    }
    if (GetEditDouble(edSpacing_, v) && v >= 0.5 && v <= 50) {
        out.minSpacingMm = v;
    } else {
        ok = false;
        if (!quiet) Log(L"點間距需為 0.5~50 的數字。");
    }
    out.travelSpeedPct = std::min(100, out.speedPct + 10);
    return ok || quiet;
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
        case ID_BTN_DEMO_CALIB: DoLoadDemoCalibration(); break;
        case ID_BTN_CLEAR: whiteboard_.Clear(); break;
        case ID_BTN_UNDO: whiteboard_.UndoLastStroke(); break;
        case ID_BTN_CLEAR_TRACE: sim_.ClearTrace(); break;
        case ID_BTN_SEND: DoSend(); break;
        case ID_BTN_STOP: DoStop(); break;
        case ID_CB_SIMULATE: UpdateButtonStates(); break;
        case ID_CB_TELEMETRY: UpdateTelemetryState(); UpdateButtonStates(); break;
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
        UpdateTelemetryState();
    }
    UpdateButtonStates();
}

void AppWindow::DoDisconnect() {
    if (sending_.load()) sender_.Cancel(serial_);
    sender_.Join();
    StopTelemetry();
    serial_.Close();
    freeDrag_ = false;
    Log(L"已斷線。");
    UpdateButtonStates();
}

void AppWindow::DoPowerOn() {
    Log(serial_.PowerOn() ? L"已送出「開啟馬達」指令。"
                           : L"開啟馬達失敗：" + serial_.LastError());
    UpdateButtonStates();
}

void AppWindow::DoPowerOff() {
    Log(serial_.PowerOff() ? L"已送出「關閉馬達」指令。"
                            : L"關閉馬達失敗：" + serial_.LastError());
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
    if (calibIsDemo_) {
        // Never mix a measured corner with a made-up one.
        calibIsDemo_ = false;
        calib_.hasA = calib_.hasB = false;
        Log(L"已清除示範校正值，請重新記錄 A、B 兩點。");
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
    RefreshPlanPreview();
    UpdateButtonStates();
}

void AppWindow::DoLoadDemoCalibration() {
    // A sheet roughly 120x120mm in front of the base, inside the area the
    // simulator's IK can reach with the pen pointing straight down.
    calib_.a = Coords{280.0, 60.0, 90.0, 180.0, 0.0, 0.0};  // canvas top-left
    calib_.b = Coords{160.0, -60.0, 90.0, 180.0, 0.0, 0.0}; // canvas bottom-right
    calib_.hasA = calib_.hasB = true;
    calibIsDemo_ = true;
    Log(L"已載入示範校正值（僅供 3D 模擬預覽使用）。要實際畫圖前，請務必用自由拖曳模式重新記錄 A、B 兩點。");
    RefreshPlanPreview();
    UpdateButtonStates();
}

void AppWindow::DoSend() {
    DrawParams params;
    if (!ReadDrawParams(params)) return;
    params.simulateOnly = IsChecked(cbSimulate_);

    if (!calib_.IsComplete()) {
        Log(L"請先完成校正（記錄 A、B 兩點，或載入示範校正值來試模擬）。");
        return;
    }
    if (whiteboard_.Empty()) {
        Log(L"畫布是空的，沒有東西可以送出。");
        return;
    }
    if (!params.simulateOnly) {
        if (!serial_.IsOpen()) {
            Log(L"尚未連線，無法傳送到機械臂。");
            return;
        }
        if (calibIsDemo_) {
            int answer = MessageBoxW(
                hwnd_,
                L"目前用的是示範校正值，不是量測你實際畫紙位置得到的。\n"
                L"直接送到真實機械臂可能讓筆壓到桌面或畫在錯誤的位置。\n\n仍要繼續嗎？",
                L"確認", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
            if (answer != IDYES) return;
        }
    }

    sim_.ClearTrace();
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
    if (sending_.load()) {
        sender_.Cancel(serial_);
        Log(L"已送出停止指令。");
    } else if (serial_.IsOpen()) {
        serial_.Stop();
        Log(L"已送出停止指令（目前並非繪製中）。");
    }
}

void AppWindow::StartTelemetry() {
    if (telemetryRunning_.load() || !serial_.IsOpen()) return;
    telemetryRunning_ = true;
    telemetryThread_ = std::thread([this] {
        while (telemetryRunning_.load()) {
            if (serial_.IsOpen()) {
                auto angles = serial_.GetAngles();
                auto coords = serial_.GetCoords();
                if (angles || coords) {
                    auto* update = new PoseUpdate();
                    update->source = PoseUpdate::Source::Telemetry;
                    if (angles) {
                        update->angles = *angles;
                        update->hasAngles = true;
                    }
                    update->hasTcp = coords.has_value();
                    if (coords) update->tcp = *coords;
                    PostMessageW(hwnd_, WM_APP_POSE, 0, reinterpret_cast<LPARAM>(update));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });
}

void AppWindow::StopTelemetry() {
    telemetryRunning_ = false;
    if (telemetryThread_.joinable()) telemetryThread_.join();
}

void AppWindow::UpdateTelemetryState() {
    if (serial_.IsOpen() && IsChecked(cbTelemetry_)) {
        StartTelemetry();
    } else {
        StopTelemetry();
    }
}

void AppWindow::OnPoseUpdate(PoseUpdate* update) {
    // While an offline simulation is playing, the simulated arm owns the view -
    // ignore whatever the (idle) real arm is reporting so the two don't fight.
    bool simJob = sending_.load() && IsChecked(cbSimulate_);
    if (simJob && update->source != PoseUpdate::Source::Simulation) return;

    if (update->hasAngles) sim_.SetArmAngles(update->angles);
    if (!update->hasTcp) return;

    bool penDown = update->penDown;
    if (update->source == PoseUpdate::Source::Telemetry) {
        // Infer pen contact from the measured height above the calibrated sheet.
        penDown = calib_.IsComplete() && update->tcp.z <= calib_.PenDownZ() + 2.0;
    }
    sim_.SetTcp(update->tcp);

    // Measured poses are the source of truth for the executed trajectory;
    // commanded poses only fill in when telemetry is switched off.
    bool telemetryOn = telemetryRunning_.load();
    if (update->source != PoseUpdate::Source::Command || !telemetryOn) {
        sim_.AppendTrace(update->tcp, penDown);
    }
}

void AppWindow::OnDestroy() {
    if (sending_.load()) sender_.Cancel(serial_);
    sender_.Join();
    StopTelemetry();
    serial_.Close();
}

} // namespace mycobot
