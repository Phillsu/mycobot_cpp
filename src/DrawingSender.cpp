#include "DrawingSender.h"
#include "ProtocolCode.h"

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

double Dist(const POINT& a, const POINT& b) {
    double dx = static_cast<double>(a.x - b.x);
    double dy = static_cast<double>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

// Reduces a raw mouse-move polyline down to points spaced >= minSpacingPx
// apart, always keeping the first and last point.
Stroke Resample(const Stroke& raw, double minSpacingPx) {
    Stroke out;
    if (raw.empty()) return out;
    out.push_back(raw.front());
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        if (Dist(raw[i], out.back()) >= minSpacingPx) out.push_back(raw[i]);
    }
    if (raw.size() > 1 && (out.empty() || Dist(raw.back(), out.back()) > 0.01)) {
        out.push_back(raw.back());
    }
    return out;
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
    if (strokes.empty() || !calib.IsComplete() || !serial.IsOpen()) return false;

    if (thread_.joinable()) thread_.join(); // reap a previous, already-finished thread
    cancelRequested_ = false;
    running_ = true;
    thread_ = std::thread(&DrawingSender::Run, this, strokes, calib, &serial, params, notifyWnd);
    return true;
}

void DrawingSender::Cancel(MyCobotSerial& serial) {
    cancelRequested_ = true;
    serial.Stop();
}

void DrawingSender::Run(std::vector<Stroke> strokes, Calibration calib, MyCobotSerial* serial,
                         DrawParams params, HWND notifyWnd) {
    struct Waypoint { Coords pose; bool penDown; };
    std::vector<Waypoint> plan;

    // mm-per-pixel scale (average of X/Y) used only to translate the user's
    // "minimum spacing in mm" preference into a pixel-space threshold for
    // resampling the raw mouse points.
    double mmPerPxX = std::fabs(calib.b.x - calib.a.x) / std::max(1, calib.canvasW);
    double mmPerPxY = std::fabs(calib.b.y - calib.a.y) / std::max(1, calib.canvasH);
    double mmPerPx = std::max(0.01, (mmPerPxX + mmPerPxY) / 2.0);
    double minSpacingPx = std::max(1.0, params.minSpacingMm / mmPerPx);

    for (const auto& raw : strokes) {
        Stroke rs = Resample(raw, minSpacingPx);
        if (rs.size() < 2) continue;
        // Travel to above the stroke's start point (pen up), then plant the
        // pen, draw through every resampled point, then lift again.
        plan.push_back({calib.MapPoint(rs.front().x, rs.front().y, params.zLiftMm), false});
        for (const auto& p : rs) plan.push_back({calib.MapPoint(p.x, p.y, 0.0), true});
        plan.push_back({calib.MapPoint(rs.back().x, rs.back().y, params.zLiftMm), false});
    }

    int total = static_cast<int>(plan.size());
    bool completedNormally = false;

    if (total == 0) {
        PostLog(notifyWnd, L"沒有可繪製的線條（筆畫過短已被忽略）。");
    } else {
        PostLog(notifyWnd, L"開始傳送，共 " + std::to_wstring(total) + L" 個路徑點...");
        int sent = 0;
        for (const auto& wp : plan) {
            if (cancelRequested_.load()) break;

            int speed = wp.penDown ? params.speedPct : params.travelSpeedPct;
            if (!serial->SendCoords(wp.pose, speed, proto::MODE_LINEAR)) {
                PostLog(notifyWnd, L"傳送座標失敗：" + serial->LastError());
                break;
            }

            // Wait for the move to finish (or cancellation / a generous timeout
            // so a lost reply can't hang the job forever).
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
        completedNormally = (sent == total) && !cancelRequested_.load();
    }

    // Always try to leave the pen lifted, whether we finished, failed, or
    // were cancelled mid-stroke - never leave it pressed on the paper.
    Coords liftPose = calib.MapPoint(calib.canvasW / 2.0, calib.canvasH / 2.0, params.zLiftMm);
    serial->SendCoords(liftPose, params.travelSpeedPct, proto::MODE_LINEAR);

    if (cancelRequested_.load()) {
        PostLog(notifyWnd, L"已停止繪製。");
    } else if (completedNormally) {
        PostLog(notifyWnd, L"繪製完成。");
    }

    running_ = false;
    PostMessageW(notifyWnd, WM_APP_DONE, completedNormally ? 1 : 0, 0);
}

} // namespace mycobot
