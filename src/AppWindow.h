// AppWindow.h
//
// Main application window: wires together the Whiteboard canvas, the
// MyCobotSerial link, the two-point free-drag calibration workflow, and the
// DrawingSender background job.
#pragma once

#include "Calibration.h"
#include "DrawingSender.h"
#include "MyCobotSerial.h"
#include "Whiteboard.h"

#include <windows.h>
#include <string>

namespace mycobot {

class AppWindow {
public:
    static void RegisterClass(HINSTANCE hInst);
    HWND Create(HINSTANCE hInst, int nCmdShow);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

    void OnCreate(HWND hwnd, HINSTANCE hInst);
    void OnSize(int w, int h);
    void OnCommand(int id, int notifyCode);
    void OnDestroy();

    void Layout();
    void Log(const std::wstring& msg);
    void RefreshStatusLabels();
    void UpdateButtonStates();

    // Reads and validates the small numeric parameter fields; returns false
    // (and logs why) if any of them are out of range.
    bool ReadDrawParams(DrawParams& out);

    void DoConnect();
    void DoDisconnect();
    void DoPowerOn();
    void DoPowerOff();
    void DoToggleFreeDrag();
    void DoRecordCorner(bool isA);
    void DoSend();
    void DoStop();

    HWND hwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;

    // Row 1: connection
    HWND edPort_ = nullptr, edBaud_ = nullptr;
    HWND btnConnect_ = nullptr, btnDisconnect_ = nullptr;
    HWND btnPowerOn_ = nullptr, btnPowerOff_ = nullptr;
    HWND stConn_ = nullptr;

    // Row 2: calibration
    HWND btnFreeDrag_ = nullptr, btnRecordA_ = nullptr, btnRecordB_ = nullptr;
    HWND stCalib_ = nullptr;

    // Row 3: draw params
    HWND edZLift_ = nullptr, edSpeed_ = nullptr, edSpacing_ = nullptr;

    // Row 4: canvas + side buttons
    Whiteboard whiteboard_;
    HWND btnClear_ = nullptr, btnUndo_ = nullptr, btnSend_ = nullptr, btnStop_ = nullptr;
    HWND progressBar_ = nullptr, stProgress_ = nullptr;

    // Bottom: log
    HWND edLog_ = nullptr;

    MyCobotSerial serial_;
    Calibration calib_;
    DrawingSender sender_;
    bool freeDrag_ = false;
    bool sending_ = false;
};

} // namespace mycobot
