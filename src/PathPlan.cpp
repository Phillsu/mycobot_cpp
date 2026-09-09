#include "PathPlan.h"

#include <algorithm>
#include <cmath>

namespace mycobot {

namespace {

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
    if (raw.size() > 1 && Dist(raw.back(), out.back()) > 0.01) out.push_back(raw.back());
    return out;
}

} // namespace

std::vector<Waypoint> BuildPlan(const std::vector<Stroke>& strokes, const Calibration& calib,
                                 const DrawParams& params) {
    std::vector<Waypoint> plan;
    if (!calib.IsComplete()) return plan;

    // mm-per-pixel scale (average of X/Y), used only to translate the user's
    // "minimum spacing in mm" preference into a pixel-space threshold.
    double mmPerPxX = std::fabs(calib.b.x - calib.a.x) / std::max(1, calib.canvasW);
    double mmPerPxY = std::fabs(calib.b.y - calib.a.y) / std::max(1, calib.canvasH);
    double mmPerPx = std::max(0.01, (mmPerPxX + mmPerPxY) / 2.0);
    double minSpacingPx = std::max(1.0, params.minSpacingMm / mmPerPx);

    for (const auto& raw : strokes) {
        Stroke rs = Resample(raw, minSpacingPx);
        if (rs.size() < 2) continue;
        // Travel above the stroke's start point, plant the pen, draw through
        // every resampled point, then lift again.
        plan.push_back({calib.MapPoint(rs.front().x, rs.front().y, params.zLiftMm), false});
        for (const auto& p : rs) plan.push_back({calib.MapPoint(p.x, p.y, 0.0), true});
        plan.push_back({calib.MapPoint(rs.back().x, rs.back().y, params.zLiftMm), false});
    }
    return plan;
}

} // namespace mycobot
