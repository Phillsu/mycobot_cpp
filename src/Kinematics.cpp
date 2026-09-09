#include "Kinematics.h"

#include <algorithm>
#include <cstdint>

namespace mycobot {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;

Mat4 Identity() {
    Mat4 t{};
    for (int i = 0; i < 4; ++i) t.m[i][i] = 1.0;
    return t;
}

Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

// Standard DH transform: Rz(theta) * Tz(d) * Tx(a) * Rx(alpha). Angles in radians.
Mat4 DhTransform(double theta, double d, double a, double alpha) {
    double ct = std::cos(theta), st = std::sin(theta);
    double ca = std::cos(alpha), sa = std::sin(alpha);
    Mat4 t{};
    t.m[0][0] = ct; t.m[0][1] = -st * ca; t.m[0][2] = st * sa;  t.m[0][3] = a * ct;
    t.m[1][0] = st; t.m[1][1] = ct * ca;  t.m[1][2] = -ct * sa; t.m[1][3] = a * st;
    t.m[2][0] = 0;  t.m[2][1] = sa;       t.m[2][2] = ca;       t.m[2][3] = d;
    t.m[3][3] = 1;
    return t;
}

// The six DH rows for this arm family, with the usual +/-90 degree joint offsets.
struct DhRow { double thetaOffsetDeg, d, a, alphaDeg; };

std::array<DhRow, 6> DhTable(const RobotModel& mo) {
    return {{
        {0.0,   mo.d1, 0.0,   90.0},
        {-90.0, 0.0,   mo.a2, 0.0},
        {0.0,   0.0,   mo.a3, 0.0},
        {-90.0, mo.d4, 0.0,   90.0},
        {90.0,  mo.d5, 0.0,   -90.0},
        {0.0,   mo.d6, 0.0,   0.0},
    }};
}

Vec3 Origin(const Mat4& t) { return {t.m[0][3], t.m[1][3], t.m[2][3]}; }
Vec3 ZAxis(const Mat4& t) { return {t.m[0][2], t.m[1][2], t.m[2][2]}; }

// Solves A x = b for a small dense system by Gaussian elimination with
// partial pivoting. Returns false if the matrix is effectively singular.
bool SolveLinear6(double A[6][6], double b[6], double x[6]) {
    for (int col = 0; col < 6; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 6; ++r)
            if (std::fabs(A[r][col]) > std::fabs(A[pivot][col])) pivot = r;
        if (std::fabs(A[pivot][col]) < 1e-12) return false;
        if (pivot != col) {
            for (int c = 0; c < 6; ++c) std::swap(A[pivot][c], A[col][c]);
            std::swap(b[pivot], b[col]);
        }
        for (int r = col + 1; r < 6; ++r) {
            double f = A[r][col] / A[col][col];
            if (f == 0.0) continue;
            for (int c = col; c < 6; ++c) A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    for (int r = 5; r >= 0; --r) {
        double s = b[r];
        for (int c = r + 1; c < 6; ++c) s -= A[r][c] * x[c];
        x[r] = s / A[r][r];
    }
    return true;
}

// Task-space error: [position error (mm) x3, tool-axis error (scaled) x3].
// Orientation about the tool axis itself is deliberately left unconstrained.
void TaskError(const RobotModel& model, const Angles& anglesDeg, const Vec3& targetPos,
               const Vec3& toolDir, double e[6]) {
    Mat4 t = FlangePose(model, anglesDeg);
    Vec3 p = Origin(t);
    e[0] = targetPos.x - p.x;
    e[1] = targetPos.y - p.y;
    e[2] = targetPos.z - p.z;

    Vec3 z = ZAxis(t);
    Vec3 c = Cross(z, toolDir);
    if (Dot(z, toolDir) < 0.0 && Length(c) < 1e-6) {
        // Exactly antiparallel: the cross product vanishes, so nudge the
        // solver with an arbitrary perpendicular direction to break out.
        c = Normalize(Cross(z, std::fabs(z.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0}));
    }
    // Weight puts the (unitless) axis error on a comparable scale to mm.
    constexpr double kAxisWeight = 60.0;
    e[3] = c.x * kAxisWeight;
    e[4] = c.y * kAxisWeight;
    e[5] = c.z * kAxisWeight;
}

double ErrorNorm(const double e[6]) {
    double s = 0;
    for (int i = 0; i < 6; ++i) s += e[i] * e[i];
    return std::sqrt(s);
}

} // namespace

Mat4 FlangePose(const RobotModel& model, const Angles& anglesDeg) {
    auto table = DhTable(model);
    Mat4 t = Identity();
    for (int i = 0; i < 6; ++i) {
        const DhRow& row = table[i];
        t = Multiply(t, DhTransform((anglesDeg[i] + row.thetaOffsetDeg) * kDeg2Rad, row.d, row.a,
                                    row.alphaDeg * kDeg2Rad));
    }
    return t;
}

std::array<Vec3, 7> JointPositions(const RobotModel& model, const Angles& anglesDeg) {
    auto table = DhTable(model);
    std::array<Vec3, 7> pts{};
    Mat4 t = Identity();
    pts[0] = Origin(t);
    for (int i = 0; i < 6; ++i) {
        const DhRow& row = table[i];
        t = Multiply(t, DhTransform((anglesDeg[i] + row.thetaOffsetDeg) * kDeg2Rad, row.d, row.a,
                                    row.alphaDeg * kDeg2Rad));
        pts[i + 1] = Origin(t);
    }
    return pts;
}

Vec3 ToolAxis(const RobotModel& model, const Angles& anglesDeg) {
    return ZAxis(FlangePose(model, anglesDeg));
}

namespace {

// One damped-least-squares run from a single starting posture.
bool SolveFromSeed(const RobotModel& model, const Vec3& targetPos, const Vec3& dir,
                   const Angles& seedDeg, Angles& outDeg) {
    Angles q = seedDeg;

    constexpr int kMaxIterations = 120;
    constexpr double kStepDeg = 0.05;     // finite-difference step for the Jacobian
    constexpr double kLambda = 0.8;       // damping, in the same (degree) units as the step
    constexpr double kMaxStepDeg = 8.0;   // per-iteration joint move limit, keeps it stable
    constexpr double kPosTolMm = 0.3;
    constexpr double kAxisTol = 0.02 * 60.0;

    double e[6];
    for (int iter = 0; iter < kMaxIterations; ++iter) {
        TaskError(model, q, targetPos, dir, e);
        double posErr = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        double axisErr = std::sqrt(e[3] * e[3] + e[4] * e[4] + e[5] * e[5]);
        if (posErr < kPosTolMm && axisErr < kAxisTol) {
            outDeg = q;
            return true;
        }

        // Numerical Jacobian of the task error w.r.t. each joint angle.
        double J[6][6];
        for (int j = 0; j < 6; ++j) {
            Angles qp = q;
            qp[j] += kStepDeg;
            double ep[6];
            TaskError(model, qp, targetPos, dir, ep);
            for (int i = 0; i < 6; ++i) J[i][j] = (ep[i] - e[i]) / kStepDeg;
        }

        // Damped least squares: (J^T J + lambda^2 I) dq = J^T e
        double A[6][6];
        double b[6];
        for (int r = 0; r < 6; ++r) {
            for (int c = 0; c < 6; ++c) {
                double s = 0;
                for (int k = 0; k < 6; ++k) s += J[k][r] * J[k][c];
                A[r][c] = s + (r == c ? kLambda * kLambda : 0.0);
            }
            double s = 0;
            for (int k = 0; k < 6; ++k) s += J[k][r] * e[k];
            b[r] = s;
        }

        double dq[6]{};
        if (!SolveLinear6(A, b, dq)) return false;

        double maxStep = 0;
        for (int i = 0; i < 6; ++i) maxStep = std::max(maxStep, std::fabs(dq[i]));
        double scale = (maxStep > kMaxStepDeg) ? (kMaxStepDeg / maxStep) : 1.0;

        // J is the derivative of the ERROR (target - current) with respect to
        // the joint angles, so the step that drives the error to zero is
        // -dq, not +dq.
        for (int i = 0; i < 6; ++i) {
            q[i] = std::clamp(q[i] - dq[i] * scale, model.jointMinDeg, model.jointMaxDeg);
        }
    }

    TaskError(model, q, targetPos, dir, e);
    if (ErrorNorm(e) < 5.0) { // close enough to still be worth drawing
        outDeg = q;
        return true;
    }
    return false;
}

} // namespace

bool SolveIK(const RobotModel& model, const Vec3& targetPos, const Vec3& toolDir,
             const Angles& seedDeg, Angles& outDeg) {
    Vec3 dir = Normalize(toolDir);

    // Damped least squares only finds the solution "downhill" from where it
    // starts, so a single seed can fail on a target that is perfectly
    // reachable from a different arm configuration. Try the caller's seed
    // first (it is the previous point's solution while tracing a path, which
    // both converges fastest and keeps the arm moving continuously), then a
    // spread of fixed postures, then deterministic pseudo-random restarts.
    if (SolveFromSeed(model, targetPos, dir, seedDeg, outDeg)) return true;

    static const Angles kFallbackSeeds[] = {
        {0, -20, -80, -20, 90, 0},   {0, -45, -45, -45, 90, 0}, {0, -90, 60, -60, 90, 0},
        {0, 20, -100, 0, 90, 0},     {45, -30, -60, -30, 90, 0}, {-45, -30, -60, -30, 90, 0},
        {0, -60, -30, 0, 60, 0},     {90, -40, -50, -40, 90, 0}, {-90, -40, -50, -40, 90, 0},
        {0, 0, 0, 0, 0, 0},
    };
    for (const auto& seed : kFallbackSeeds) {
        if (SolveFromSeed(model, targetPos, dir, seed, outDeg)) return true;
    }

    uint32_t rng = 0x9E3779B9u;
    for (int attempt = 0; attempt < 12; ++attempt) {
        Angles seed{};
        for (int i = 0; i < 6; ++i) {
            rng = rng * 1664525u + 1013904223u;
            double t = static_cast<double>((rng >> 8) % 2001) / 2000.0; // 0..1
            seed[i] = model.jointMinDeg + t * (model.jointMaxDeg - model.jointMinDeg);
        }
        if (SolveFromSeed(model, targetPos, dir, seed, outDeg)) return true;
    }
    return false;
}

} // namespace mycobot
