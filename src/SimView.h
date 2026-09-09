// SimView.h
//
// A dependency-free 3D simulation viewport (plain GDI, no OpenGL/DirectX)
// that mirrors what the arm is doing:
//
//   * the arm's skeleton, drawn from the joint angles read back from the
//     real robot (live sync) or solved by IK (offline simulation mode)
//   * the planned path from the whiteboard, so you can preview a job before
//     letting the arm touch the paper
//   * the trajectory actually executed so far, accumulated point by point
//   * the calibrated sheet of paper and the robot's base frame
//
// All state is owned by the UI thread: worker threads report poses via
// posted messages, and AppWindow calls these setters while handling them.
#pragma once

#include "Calibration.h"
#include "Kinematics.h"
#include "MyCobotSerial.h"
#include "PathPlan.h"

#include <string>
#include <vector>
#include <windows.h>

namespace mycobot {

class SimView {
public:
    static void RegisterClass(HINSTANCE hInst);
    HWND Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, int id);
    HWND hwnd() const { return hwnd_; }

    void SetPaper(const Calibration& calib);
    void SetPlannedPath(const std::vector<Waypoint>& plan);
    void SetArmAngles(const Angles& angles);
    void SetTcp(const Coords& tcp);
    void AppendTrace(const Coords& tcp, bool penDown);
    void ClearTrace();
    void SetStatusText(const std::wstring& text);
    void SetUnreachableMarkers(const std::vector<Vec3>& points);
    void ResetView();

    // Forwarded by the parent so the wheel zooms without needing focus first.
    void HandleWheel(int delta);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void Render(HDC hdc, const RECT& client);
    void Invalidate();

    struct TracePt {
        Vec3 p;
        bool penDown = false;
    };

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;

    // Scene state
    bool hasPaper_ = false;
    Vec3 paper_[4]{};
    std::vector<Waypoint> plan_;
    std::vector<TracePt> trace_;
    std::vector<Vec3> unreachable_;
    bool hasArm_ = false;
    Angles armAngles_{};
    bool hasTcp_ = false;
    Vec3 tcp_{};
    std::wstring status_;
    RobotModel model_ = kDefaultModel;

    // Camera (spherical orbit around target_)
    double azimuthDeg_ = -125.0;
    double elevationDeg_ = 24.0;
    double distance_ = 780.0;
    Vec3 target_{140.0, 0.0, 90.0};

    // Mouse interaction
    bool orbiting_ = false;
    bool panning_ = false;
    POINT lastMouse_{};
};

} // namespace mycobot
