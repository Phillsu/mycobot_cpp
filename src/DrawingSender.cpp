#include "DrawingSender.h"
#include "ProtocolCode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace mycobot {

namespace {

void PostLog(HWND notifyWnd, const std::wstring& msg) {
    if (!notifyWnd) return;
    wchar_t* buf = new wchar_t[msg.size() + 1];
    wcscpy_s(buf, msg.size() + 1, msg.c_str());
    PostMessageW(notifyWnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(buf));
}

void PostPose(HWND notifyWnd, PoseUpdate::Source source, const Coords& tcp, bool penDown,
              const Angles* angles) {
    if (!notifyWnd) return;
    auto* update = new PoseUpdate();
    update->source = source;
    update->tcp = tcp;
    update->penDown = penDown;
    if (angles) {
        update->angles = *angles;
        update->hasAngles = true;
    }
    PostMessageW(notifyWnd, WM_APP_POSE, 0, reinterpret_cast<LPARAM>(update));
}

Vec3 PosOf(const Coords& c) { return {c.x, c.y, c.z}; }

Coords Lerp(const Coords& a, const Coords& b, double t) {
    Coords c;
    c.x = a.x + (b.x - a.x) * t;
    c.y = a.y + (b.y - a.y) * t;
    c.z = a.z + (b.z - a.z) * t;
    c.rx = a.rx + (b.rx - a.rx) * t;
    c.ry = a.ry + (b.ry - a.ry) * t;
    c.rz = a.rz + (b.rz - a.rz) * t;
    return c;
}

} // namespace

DrawingSender::~DrawingSender() {
    if (thread_.joinable()) {
        cancelRequested_ = true;
        thread_.join();
    }
}

void DrawingSender::Join() {
    if (thread_.joinable()) thread_.join();
}

bool DrawingSender::Start(const std::vector<Stroke>& strokes, const Calibration& calib,
                           MyCobotSerial& serial, const DrawParams& params, HWND notifyWnd) {
    if (running_.load()) return false;
    if (strokes.empty() || !calib.IsComplete()) return false;
    if (!params.simulateOnly && !serial.IsOpen()) return false;

    std::vector<Waypoint> plan = BuildPlan(strokes, calib, params);

    if (thread_.joinable()) thread_.join(); // reap a previous, already-finished thread
    cancelRequested_ = false;
    running_ = true;
    thread_ = std::thread(&DrawingSender::Run, this, std::move(plan), &serial, params, notifyWnd);
    return true;
}

void DrawingSender::Cancel(MyCobotSerial& serial) {
    cancelRequested_ = true;
    if (serial.IsOpen()) serial.Stop();
}

void DrawingSender::Run(std::vector<Waypoint> plan, MyCobotSerial* serial, DrawParams params,
                         HWND notifyWnd) {
    bool completedNormally = false;

    if (plan.empty()) {
        PostLog(notifyWnd, L"沒有可繪製的線條（筆畫過短已被忽略）。");
    } else if (params.simulateOnly) {
        PostLog(notifyWnd, L"開始模擬，共 " + std::to_wstring(plan.size()) + L" 個路徑點（不會送到機械臂）。");
        completedNormally = RunSimulated(plan, params, notifyWnd);
    } else {
        PostLog(notifyWnd, L"開始傳送，共 " + std::to_wstring(plan.size()) + L" 個路徑點...");
        completedNormally = RunLive(plan, serial, params, notifyWnd);
    }

    if (cancelRequested_.load()) {
        PostLog(notifyWnd, params.simulateOnly ? L"已停止模擬。" : L"已停止繪製。");
    } else if (completedNormally) {
        PostLog(notifyWnd, params.simulateOnly ? L"模擬完成。" : L"繪製完成。");
    }

    running_ = false;
    PostMessageW(notifyWnd, WM_APP_DONE, completedNormally ? 1 : 0, 0);
}

bool DrawingSender::RunLive(const std::vector<Waypoint>& plan, MyCobotSerial* serial,
                             const DrawParams& params, HWND notifyWnd) {
    int total = static_cast<int>(plan.size());
    int sent = 0;

    for (const auto& wp : plan) {
        if (cancelRequested_.load()) break;

        int speed = wp.penDown ? params.speedPct : params.travelSpeedPct;
        if (!serial->SendCoords(wp.pose, speed, proto::MODE_LINEAR)) {
            PostLog(notifyWnd, L"傳送座標失敗：" + serial->LastError());
            break;
        }
        PostPose(notifyWnd, PoseUpdate::Source::Command, wp.pose, wp.penDown, nullptr);

        // Wait for the move to finish (or cancellation / a generous timeout so
        // a lost reply can't hang the job forever).
        ULONGLONG deadline = GetTickCount64() + 8000;
        for (;;) {
            if (cancelRequested_.load()) break;
            TriState moving = serial->IsMoving();
            if (moving != TriState::True) break;
            if (GetTickCount64() >= deadline) {
                PostLog(notifyWnd, L"等待移動完成逾時，繼續下一點。");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        if (cancelRequested_.load()) break;

        ++sent;
        PostMessageW(notifyWnd, WM_APP_PROGRESS, static_cast<WPARAM>(sent),
                     static_cast<LPARAM>(total));
    }

    // Always try to leave the pen lifted, whether we finished, failed, or were
    // cancelled mid-stroke - never leave it pressed against the paper.
    if (!plan.empty() && serial->IsOpen()) {
        Coords lift = plan.back().pose;
        lift.z += params.zLiftMm;
        serial->SendCoords(lift, params.travelSpeedPct, proto::MODE_LINEAR);
        PostPose(notifyWnd, PoseUpdate::Source::Command, lift, false, nullptr);
    }

    return sent == total && !cancelRequested_.load();
}

bool DrawingSender::RunSimulated(const std::vector<Waypoint>& plan, const DrawParams& params,
                                  HWND notifyWnd) {
    const RobotModel& model = kDefaultModel;
    const Vec3 toolDown{0, 0, -1}; // a pen points straight down at the paper

    // A reasonable starting posture for the solver; every later point is
    // seeded from the previous solution so the arm moves continuously.
    Angles seed{0, -20, -80, -20, 90, 0};

    std::vector<Vec3> unreachable;
    int total = static_cast<int>(plan.size());
    int done = 0;

    constexpr double kFrameSeconds = 0.04; // 25 fps playback
    Coords current = plan.front().pose;

    for (size_t i = 0; i < plan.size(); ++i) {
        if (cancelRequested_.load()) break;
        const Waypoint& wp = plan[i];

        double mmPerSecond = 2.5 * (wp.penDown ? params.speedPct : params.travelSpeedPct);
        double distance = Length(PosOf(wp.pose) - PosOf(current));
        int steps = std::max(1, static_cast<int>(std::lround(distance / mmPerSecond / kFrameSeconds)));
        steps = std::min(steps, 400); // guard against a huge first travel move

        for (int s = 1; s <= steps; ++s) {
            if (cancelRequested_.load()) break;
            Coords pose = Lerp(current, wp.pose, static_cast<double>(s) / steps);

            Angles solved{};
            bool ok = SolveIK(model, PosOf(pose), toolDown, seed, solved);
            if (ok) {
                seed = solved;
                PostPose(notifyWnd, PoseUpdate::Source::Simulation, pose, wp.penDown, &solved);
            } else {
                PostPose(notifyWnd, PoseUpdate::Source::Simulation, pose, wp.penDown, nullptr);
                if (s == steps && unreachable.size() < 500) unreachable.push_back(PosOf(pose));
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(kFrameSeconds * 1000)));
        }

        current = wp.pose;
        ++done;
        PostMessageW(notifyWnd, WM_APP_PROGRESS, static_cast<WPARAM>(done),
                     static_cast<LPARAM>(total));
    }

    if (!unreachable.empty()) {
        PostLog(notifyWnd, L"警告：有 " + std::to_wstring(unreachable.size()) +
                                L" 個路徑點在模擬中解不出可達姿態（3D 視圖以紅色叉號標示），"
                                L"請檢查校正範圍是否超出機械臂可達區域。");
        auto* pts = new std::vector<Vec3>(std::move(unreachable));
        PostMessageW(notifyWnd, WM_APP_UNREACHABLE, 0, reinterpret_cast<LPARAM>(pts));
    }

    return done == total && !cancelRequested_.load();
}

} // namespace mycobot
