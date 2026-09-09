// DrawingSender.h
//
// Converts captured whiteboard strokes into a sequence of robot waypoints
// (pen-up travel + pen-down linear draw moves) and streams them to the arm
// on a background thread, waiting for each move to finish before sending
// the next one. Progress/completion is reported back to the UI thread via
// PostMessage so the worker never touches Win32 UI objects directly.
#pragma once

#include "Calibration.h"
#include "MyCobotSerial.h"
#include "Whiteboard.h"

#include <atomic>
#include <thread>
#include <windows.h>

namespace mycobot {

// Custom window messages posted to the notify HWND.
//   WM_APP_PROGRESS: wParam = points sent so far, lParam = total points
//   WM_APP_DONE:      wParam = 1 if completed normally, 0 if stopped/cancelled/error
constexpr UINT WM_APP_PROGRESS = WM_APP + 1;
constexpr UINT WM_APP_DONE     = WM_APP + 2;
constexpr UINT WM_APP_LOG      = WM_APP + 3; // lParam = new'd wide C-string; receiver must delete[]

struct DrawParams {
    int speedPct = 30;        // 1-100, kept conservative by default
    double zLiftMm = 20.0;    // pen-up height above the drawing surface
    double minSpacingMm = 3.0; // minimum distance between consecutive sent waypoints
    int travelSpeedPct = 40;  // speed used for pen-up travel moves between strokes
};

class DrawingSender {
public:
    ~DrawingSender();

    // Returns false immediately if a job is already running or inputs are invalid.
    bool Start(const std::vector<Stroke>& strokes, const Calibration& calib,
               MyCobotSerial& serial, const DrawParams& params, HWND notifyWnd);

    // Requests cancellation; also issues an immediate STOP on the serial link.
    // Safe to call from the UI thread while a job is running.
    void Cancel(MyCobotSerial& serial);

    bool IsRunning() const { return running_.load(); }

    void Join();

private:
    void Run(std::vector<Stroke> strokes, Calibration calib, MyCobotSerial* serial,
              DrawParams params, HWND notifyWnd);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
};

} // namespace mycobot
