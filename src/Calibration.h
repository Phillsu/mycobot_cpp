// Calibration.h
//
// Maps whiteboard pixel coordinates to robot base-frame coordinates using a
// simple two-point (axis-aligned) calibration:
//   - Corner A = robot pose recorded while the pen tip was held at the
//     canvas's top-left corner (pixel 0,0).
//   - Corner B = robot pose recorded while the pen tip was held at the
//     canvas's bottom-right corner (pixel canvasW,canvasH).
//
// This assumes the paper's edges are roughly aligned with the robot's own
// X/Y base axes (place the paper accordingly). It deliberately does not try
// to solve a full rotation - that would need a 3rd point and more UI - and
// is documented as a known limitation in README.md.
#pragma once

#include "MyCobotSerial.h"
#include <algorithm>

namespace mycobot {

class Calibration {
public:
    bool hasA = false;
    bool hasB = false;
    Coords a{};
    Coords b{};
    int canvasW = 1;
    int canvasH = 1;

    bool IsComplete() const { return hasA && hasB; }

    void SetCanvasSize(int w, int h) {
        canvasW = (w > 0) ? w : 1;
        canvasH = (h > 0) ? h : 1;
    }

    // Pen-down Z is the average of the two recorded corner heights (assumes a
    // flat drawing surface). Orientation likewise comes from the average of
    // the two recorded poses.
    double PenDownZ() const { return (a.z + b.z) / 2.0; }
    double Rx() const { return (a.rx + b.rx) / 2.0; }
    double Ry() const { return (a.ry + b.ry) / 2.0; }
    double Rz() const { return (a.rz + b.rz) / 2.0; }

    // Maps a canvas pixel (px,py) to a robot XY (mm), holding Z/RX/RY/RZ fixed
    // at the calibrated pen-down pose plus a caller-supplied Z lift.
    Coords MapPoint(double px, double py, double zLiftMm) const {
        Coords c;
        double u = std::clamp(px / static_cast<double>(canvasW), 0.0, 1.0);
        double v = std::clamp(py / static_cast<double>(canvasH), 0.0, 1.0);
        c.x = a.x + (b.x - a.x) * u;
        c.y = a.y + (b.y - a.y) * v;
        c.z = PenDownZ() + zLiftMm;
        c.rx = Rx();
        c.ry = Ry();
        c.rz = Rz();
        return c;
    }
};

} // namespace mycobot
