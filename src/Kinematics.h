// Kinematics.h
//
// Forward/inverse kinematics for the 6-axis arm, used ONLY by the built-in
// 3D simulation view:
//
//   * Forward kinematics turns the joint angles read back from the real arm
//     (GET_ANGLES) into a drawable skeleton.
//   * Inverse kinematics is used only in offline "simulation mode" (no
//     hardware attached) to show a plausible arm posture along the planned
//     path, and to flag path points that appear to be out of reach.
//
// IMPORTANT: the link lengths below only affect how the simulated arm LOOKS.
// The trajectory itself is never derived from this model - in live mode it
// comes from the firmware's own GET_COORDS, and in simulation mode from the
// calibrated whiteboard mapping. If your arm's proportions look wrong on
// screen, edit kDefaultModel; nothing else in the program depends on it.
#pragma once

#include "MyCobotSerial.h"

#include <array>
#include <cmath>

namespace mycobot {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Normalize(const Vec3& a) {
    double l = Length(a);
    return (l > 1e-12) ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0, 0, 0};
}

// Row-major 4x4 homogeneous transform.
struct Mat4 {
    double m[4][4]{};
};

// Standard Denavit-Hartenberg link lengths (mm).
struct RobotModel {
    double d1, a2, a3, d4, d5, d6;
    double jointMinDeg = -165.0;
    double jointMaxDeg = 165.0;
};

// Published myCobot 280-class proportions.
inline constexpr RobotModel kModelMyCobot280{131.56, -110.4, -96.0, 63.4, 75.05, 45.6};
// Larger 320/360-class proportions (default - closest to a 350mm-reach arm).
inline constexpr RobotModel kModelMyCobot320{173.9, -135.0, -120.0, 88.78, 95.0, 65.5};

inline constexpr RobotModel kDefaultModel = kModelMyCobot320;

// Origins of each DH frame: [0] = base, [1..5] = joints, [6] = flange/TCP.
std::array<Vec3, 7> JointPositions(const RobotModel& model, const Angles& anglesDeg);

// Full flange pose (position + orientation) for the given joint angles.
Mat4 FlangePose(const RobotModel& model, const Angles& anglesDeg);

// The flange's own Z axis in base coordinates - i.e. which way the tool points.
Vec3 ToolAxis(const RobotModel& model, const Angles& anglesDeg);

// Damped-least-squares IK constraining the flange position and the direction
// the tool points in (orientation about the tool axis is left free, which
// avoids having to guess the firmware's Euler-angle convention).
// Returns false if it did not converge - treated as "point looks unreachable".
bool SolveIK(const RobotModel& model, const Vec3& targetPos, const Vec3& toolDir,
             const Angles& seedDeg, Angles& outDeg);

} // namespace mycobot
