// DrawingSender.h
//
// Executes a drawing plan on a background thread, in one of two modes:
//
//   * live       - streams waypoints to the real arm over the serial link,
//                  waiting for each move to complete before sending the next
//   * simulation - touches no hardware at all and instead plays the path back
//                  in real time through the 3D view, solving IK for each
//                  interpolated pose so you can preview a job (and spot
//                  unreachable points) before the pen ever touches paper
//
// Progress, log lines and poses are reported to the UI thread via PostMessage,
// so the worker never touches Win32 UI objects directly.
#pragma once

#include "AppMessages.h"
#include "Calibration.h"
#include "Kinematics.h"
#include "MyCobotSerial.h"
#include "PathPlan.h"
#include "Whiteboard.h"

#include <atomic>
#include <thread>
#include <vector>
#include <windows.h>

namespace mycobot {

// Carried by WM_APP_POSE. The receiver owns the allocation.
struct PoseUpdate {
    enum class Source {
        Telemetry,  // measured on the real arm by the telemetry poller
        Command,    // the pose we just commanded the real arm to reach
        Simulation, // produced by the offline simulator
    };
    Source source = Source::Telemetry;
    bool hasAngles = false;
    Angles angles{};
    bool hasTcp = true;
    Coords tcp{};
    bool penDown = false;
};

class DrawingSender {
public:
    ~DrawingSender();

    // Returns false immediately if a job is already running or the inputs are
    // unusable. In simulation mode the serial link may be closed.
    bool Start(const std::vector<Stroke>& strokes, const Calibration& calib,
               MyCobotSerial& serial, const DrawParams& params, HWND notifyWnd);

    // Requests cancellation; also issues an immediate STOP on the real arm.
    // Safe to call from the UI thread while a job is running.
    void Cancel(MyCobotSerial& serial);

    bool IsRunning() const { return running_.load(); }
    void Join();

private:
    void Run(std::vector<Waypoint> plan, MyCobotSerial* serial, DrawParams params, HWND notifyWnd);
    bool RunLive(const std::vector<Waypoint>& plan, MyCobotSerial* serial, const DrawParams& params,
                 HWND notifyWnd);
    bool RunSimulated(const std::vector<Waypoint>& plan, const DrawParams& params, HWND notifyWnd);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
};

} // namespace mycobot
