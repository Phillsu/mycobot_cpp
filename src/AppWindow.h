// AppWindow.h
//
// Main application window: wires together the Whiteboard canvas, the 3D
// simulation view, the MyCobotSerial link, the two-point free-drag
// calibration workflow, and the DrawingSender background job.
#pragma once

#include "AppMessages.h"
#include "Calibration.h"
#include "DrawingSender.h"
#include "MyCobotSerial.h"
#include "PathPlan.h"
#include "SimView.h"
#include "Whiteboard.h"

#include <atomic>
#include <string>
#include <thread>
#include <windows.h>

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
    void OnPoseUpdate(PoseUpdate* update);
    void OnDestroy();

    void Layout();
    void Log(const std::wstring& msg);
    void RefreshStatusLabels();
    void UpdateButtonStates();
    // Rebuilds the planned path shown in the 3D view from the current strokes.
    void RefreshPlanPreview();

    bool ReadDrawParams(DrawParams& out, bool quiet = false);
    bool IsChecked(HWND checkbox) const;

    void DoConnect();
    void DoDisconnect();
    void DoPowerOn();
    void DoPowerOff();
    void DoToggleFreeDrag();
    void DoRecordCorner(bool isA);
    void DoLoadDemoCalibration();
    void DoSend();
    void DoStop();

    // Background poller that mirrors the real arm's measured pose into the
    // 3D view. Only runs while connected and the sync checkbox is ticked.
    void StartTelemetry();
    void StopTelemetry();
    void UpdateTelemetryState();

    HWND hwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;

    // Row 1: connection
    HWND edPort_ = nullptr, edBaud_ = nullptr;
    HWND btnConnect_ = nullptr, btnDisconnect_ = nullptr;
    HWND btnPowerOn_ = nullptr, btnPowerOff_ = nullptr;
    HWND stConn_ = nullptr;

    // Row 2: calibration
    HWND btnFreeDrag_ = nullptr, btnRecordA_ = nullptr, btnRecordB_ = nullptr;
    HWND btnDemoCalib_ = nullptr;
    HWND stCalib_ = nullptr;

    // Row 3: draw parameters + mode switches
    HWND edZLift_ = nullptr, edSpeed_ = nullptr, edSpacing_ = nullptr;
    HWND cbSimulate_ = nullptr, cbTelemetry_ = nullptr;

    // Row 4: actions + progress
    HWND btnClear_ = nullptr, btnUndo_ = nullptr, btnSend_ = nullptr, btnStop_ = nullptr;
    HWND btnClearTrace_ = nullptr;
    HWND progressBar_ = nullptr, stProgress_ = nullptr;

    // Main area
    Whiteboard whiteboard_;
    SimView sim_;

    // Bottom: log
    HWND edLog_ = nullptr;

    MyCobotSerial serial_;
    Calibration calib_;
    DrawingSender sender_;
    bool freeDrag_ = false;
    bool calibIsDemo_ = false;
    std::atomic<bool> sending_{false};

    std::thread telemetryThread_;
    std::atomic<bool> telemetryRunning_{false};
};

} // namespace mycobot
