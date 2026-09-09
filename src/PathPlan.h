// PathPlan.h
//
// Turns whiteboard strokes into an ordered list of robot waypoints
// (pen-up travel + pen-down draw moves). Shared by DrawingSender (which
// executes the plan) and the simulation view (which previews it), so both
// always show exactly the same path.
#pragma once

#include "Calibration.h"
#include "MyCobotSerial.h"
#include "Whiteboard.h"

#include <vector>

namespace mycobot {

struct DrawParams {
    int speedPct = 30;         // 1-100, kept conservative by default
    double zLiftMm = 20.0;     // pen-up height above the drawing surface
    double minSpacingMm = 3.0; // minimum distance between consecutive waypoints
    int travelSpeedPct = 40;   // speed used for pen-up travel moves
    bool simulateOnly = false; // run in the 3D view only, never touch the serial link
};

struct Waypoint {
    Coords pose;
    bool penDown = false;
};

// Empty if the calibration is incomplete or every stroke was too short.
std::vector<Waypoint> BuildPlan(const std::vector<Stroke>& strokes, const Calibration& calib,
                                 const DrawParams& params);

} // namespace mycobot
