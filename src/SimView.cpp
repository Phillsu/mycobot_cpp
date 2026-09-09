#include "SimView.h"

#include <algorithm>
#include <cmath>
#include <windowsx.h>

namespace mycobot {

namespace {

constexpr wchar_t kClassName[] = L"MyCobotSimViewClass";
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr size_t kMaxTracePoints = 40000;

SimView* GetThis(HWND hwnd) {
    return reinterpret_cast<SimView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

Vec3 ToVec(const Coords& c) { return {c.x, c.y, c.z}; }

// A simple look-at perspective camera.
struct Camera {
    Vec3 eye, right, up, fwd;
    double focal = 1.0;
    int cx = 0, cy = 0;
};

Camera BuildCamera(double azimuthDeg, double elevationDeg, double distance, const Vec3& target,
                   int w, int h) {
    Camera cam;
    double az = azimuthDeg * kDeg2Rad;
    double el = elevationDeg * kDeg2Rad;
    Vec3 dir{std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)};
    cam.eye = target + dir * distance;
    cam.fwd = Normalize(target - cam.eye);
    Vec3 worldUp{0, 0, 1};
    cam.right = Cross(cam.fwd, worldUp);
    if (Length(cam.right) < 1e-6) cam.right = {1, 0, 0};
    cam.right = Normalize(cam.right);
    cam.up = Cross(cam.right, cam.fwd);
    cam.focal = (std::min(w, h) * 0.5) / std::tan(22.5 * kDeg2Rad); // 45 degree vertical FOV
    cam.cx = w / 2;
    cam.cy = h / 2;
    return cam;
}

bool Project(const Camera& cam, const Vec3& p, POINT& out) {
    Vec3 v = p - cam.eye;
    double z = Dot(v, cam.fwd);
    if (z < 1.0) return false; // behind (or too close to) the camera
    double x = Dot(v, cam.right);
    double y = Dot(v, cam.up);
    out.x = cam.cx + static_cast<int>(cam.focal * x / z);
    out.y = cam.cy - static_cast<int>(cam.focal * y / z);
    return true;
}

void Line3D(HDC hdc, const Camera& cam, const Vec3& a, const Vec3& b) {
    POINT pa, pb;
    if (!Project(cam, a, pa) || !Project(cam, b, pb)) return;
    MoveToEx(hdc, pa.x, pa.y, nullptr);
    LineTo(hdc, pb.x, pb.y);
}

void Dot3D(HDC hdc, const Camera& cam, const Vec3& p, int radius) {
    POINT pt;
    if (!Project(cam, p, pt)) return;
    Ellipse(hdc, pt.x - radius, pt.y - radius, pt.x + radius, pt.y + radius);
}

void Cross3D(HDC hdc, const Camera& cam, const Vec3& p, int size) {
    POINT pt;
    if (!Project(cam, p, pt)) return;
    MoveToEx(hdc, pt.x - size, pt.y - size, nullptr);
    LineTo(hdc, pt.x + size + 1, pt.y + size + 1);
    MoveToEx(hdc, pt.x - size, pt.y + size, nullptr);
    LineTo(hdc, pt.x + size + 1, pt.y - size - 1);
}

} // namespace

void SimView::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &SimView::WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

HWND SimView::Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, int id) {
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE, x, y, w, h,
                             parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst,
                             nullptr);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    font_ = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
    return hwnd_;
}

void SimView::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void SimView::SetPaper(const Calibration& calib) {
    bool had = hasPaper_;
    Vec3 previousCornerA = paper_[0];
    Vec3 previousCornerC = paper_[2];

    hasPaper_ = calib.IsComplete();
    if (hasPaper_) {
        double z = calib.PenDownZ();
        paper_[0] = {calib.a.x, calib.a.y, z};
        paper_[1] = {calib.b.x, calib.a.y, z};
        paper_[2] = {calib.b.x, calib.b.y, z};
        paper_[3] = {calib.a.x, calib.b.y, z};

        // Re-frame only when the sheet actually moves, so redrawing strokes
        // doesn't throw away a view the user has orbited or zoomed.
        bool moved = !had || Length(paper_[0] - previousCornerA) > 0.1 ||
                     Length(paper_[2] - previousCornerC) > 0.1;
        if (moved) {
            Vec3 centre{(calib.a.x + calib.b.x) / 2.0, (calib.a.y + calib.b.y) / 2.0, z};
            target_ = centre * 0.5; // frame the base and the sheet together
            distance_ = std::clamp(2.6 * Length(centre), 420.0, 1600.0);
        }
    }
    Invalidate();
}

void SimView::SetPlannedPath(const std::vector<Waypoint>& plan) {
    plan_ = plan;
    Invalidate();
}

void SimView::SetArmAngles(const Angles& angles) {
    armAngles_ = angles;
    hasArm_ = true;
    Invalidate();
}

void SimView::SetTcp(const Coords& tcp) {
    tcp_ = ToVec(tcp);
    hasTcp_ = true;
    Invalidate();
}

void SimView::AppendTrace(const Coords& tcp, bool penDown) {
    Vec3 p = ToVec(tcp);
    if (!trace_.empty() && Length(p - trace_.back().p) < 0.4 && trace_.back().penDown == penDown) {
        return; // no visible movement, so do not grow the buffer
    }
    if (trace_.size() >= kMaxTracePoints) trace_.erase(trace_.begin());
    trace_.push_back({p, penDown});
    Invalidate();
}

void SimView::ClearTrace() {
    trace_.clear();
    unreachable_.clear();
    Invalidate();
}

void SimView::SetStatusText(const std::wstring& text) {
    status_ = text;
    Invalidate();
}

void SimView::SetUnreachableMarkers(const std::vector<Vec3>& points) {
    unreachable_ = points;
    Invalidate();
}

void SimView::ResetView() {
    azimuthDeg_ = -125.0;
    elevationDeg_ = 24.0;
    distance_ = 780.0;
    Invalidate();
}

void SimView::HandleWheel(int delta) {
    distance_ *= std::pow(0.88, delta / 120.0);
    distance_ = std::clamp(distance_, 150.0, 3000.0);
    Invalidate();
}

void SimView::Render(HDC target, const RECT& client) {
    int w = client.right, h = client.bottom;

    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, w, h);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(hdc, bmp));

    RECT bg{0, 0, w, h};
    HBRUSH bgBrush = CreateSolidBrush(RGB(250, 250, 252));
    FillRect(hdc, &bg, bgBrush);
    DeleteObject(bgBrush);

    Camera cam = BuildCamera(azimuthDeg_, elevationDeg_, distance_, target_, w, h);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // --- the table plane (z = 0 in the robot base frame) ---
    {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(226, 228, 234));
        HGDIOBJ prev = SelectObject(hdc, pen);
        for (int v = -300; v <= 400; v += 50) {
            double d = static_cast<double>(v);
            Line3D(hdc, cam, Vec3{d, -350, 0}, Vec3{d, 350, 0});
            Line3D(hdc, cam, Vec3{-300, d, 0}, Vec3{400, d, 0});
        }
        SelectObject(hdc, prev);
        DeleteObject(pen);
    }

    // --- base frame axes: X red, Y green, Z blue ---
    struct AxisDef {
        Vec3 dir;
        COLORREF color;
    };
    const AxisDef axes[] = {{{1, 0, 0}, RGB(200, 60, 60)},
                            {{0, 1, 0}, RGB(60, 160, 60)},
                            {{0, 0, 1}, RGB(60, 90, 200)}};
    for (const auto& ax : axes) {
        HPEN pen = CreatePen(PS_SOLID, 2, ax.color);
        HGDIOBJ prev = SelectObject(hdc, pen);
        Line3D(hdc, cam, Vec3{0, 0, 0}, ax.dir * 90.0);
        SelectObject(hdc, prev);
        DeleteObject(pen);
    }

    // --- the calibrated sheet of paper ---
    if (hasPaper_) {
        POINT quad[4];
        bool ok = true;
        for (int i = 0; i < 4; ++i) ok = ok && Project(cam, paper_[i], quad[i]);
        if (ok) {
            HBRUSH fill = CreateSolidBrush(RGB(255, 252, 235));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(206, 196, 156));
            HGDIOBJ prevB = SelectObject(hdc, fill);
            HGDIOBJ prevP = SelectObject(hdc, pen);
            Polygon(hdc, quad, 4);
            SelectObject(hdc, prevB);
            SelectObject(hdc, prevP);
            DeleteObject(fill);
            DeleteObject(pen);
        }
    }

    // --- planned path: blue where the pen is down, dotted grey while travelling ---
    if (plan_.size() > 1) {
        HPEN drawPen = CreatePen(PS_SOLID, 1, RGB(90, 140, 230));
        HPEN travelPen = CreatePen(PS_DOT, 1, RGB(190, 195, 205));
        for (size_t i = 1; i < plan_.size(); ++i) {
            bool penDown = plan_[i].penDown && plan_[i - 1].penDown;
            HGDIOBJ prev = SelectObject(hdc, penDown ? drawPen : travelPen);
            Line3D(hdc, cam, ToVec(plan_[i - 1].pose), ToVec(plan_[i].pose));
            SelectObject(hdc, prev);
        }
        DeleteObject(drawPen);
        DeleteObject(travelPen);
    }

    // --- points the simulator could not find an arm posture for ---
    if (!unreachable_.empty()) {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(230, 60, 60));
        HGDIOBJ prev = SelectObject(hdc, pen);
        for (const auto& p : unreachable_) Cross3D(hdc, cam, p, 5);
        SelectObject(hdc, prev);
        DeleteObject(pen);
    }

    // --- the trajectory actually executed so far ---
    if (trace_.size() > 1) {
        HPEN drawPen = CreatePen(PS_SOLID, 2, RGB(215, 45, 45));
        HPEN travelPen = CreatePen(PS_SOLID, 1, RGB(170, 175, 185));
        for (size_t i = 1; i < trace_.size(); ++i) {
            bool penDown = trace_[i].penDown && trace_[i - 1].penDown;
            HGDIOBJ prev = SelectObject(hdc, penDown ? drawPen : travelPen);
            Line3D(hdc, cam, trace_[i - 1].p, trace_[i].p);
            SelectObject(hdc, prev);
        }
        DeleteObject(drawPen);
        DeleteObject(travelPen);
    }

    // --- the arm itself ---
    if (hasArm_) {
        auto joints = JointPositions(model_, armAngles_);
        HPEN linkPen = CreatePen(PS_SOLID, 4, RGB(70, 80, 95));
        HGDIOBJ prevPen = SelectObject(hdc, linkPen);
        for (size_t i = 1; i < joints.size(); ++i) Line3D(hdc, cam, joints[i - 1], joints[i]);
        SelectObject(hdc, prevPen);
        DeleteObject(linkPen);

        HBRUSH jointBrush = CreateSolidBrush(RGB(245, 170, 60));
        HPEN jointPen = CreatePen(PS_SOLID, 1, RGB(120, 90, 30));
        HGDIOBJ pb = SelectObject(hdc, jointBrush);
        HGDIOBJ pp = SelectObject(hdc, jointPen);
        for (const auto& j : joints) Dot3D(hdc, cam, j, 4);
        SelectObject(hdc, pb);
        SelectObject(hdc, pp);
        DeleteObject(jointBrush);
        DeleteObject(jointPen);
    }

    // --- current tool position ---
    if (hasTcp_) {
        HBRUSH brush = CreateSolidBrush(RGB(215, 45, 45));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 20, 20));
        HGDIOBJ pb = SelectObject(hdc, brush);
        HGDIOBJ pp = SelectObject(hdc, pen);
        Dot3D(hdc, cam, tcp_, 5);
        SelectObject(hdc, pb);
        SelectObject(hdc, pp);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    SelectObject(hdc, oldBrush);

    // --- overlay text ---
    HGDIOBJ prevFont = SelectObject(hdc, font_);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(70, 75, 85));
    std::wstring header = status_.empty() ? std::wstring(L"3D 模擬視圖") : status_;
    TextOutW(hdc, 8, 6, header.c_str(), static_cast<int>(header.size()));
    if (hasTcp_) {
        wchar_t buf[128];
        swprintf_s(buf, L"TCP  X %.1f  Y %.1f  Z %.1f mm", tcp_.x, tcp_.y, tcp_.z);
        TextOutW(hdc, 8, 24, buf, static_cast<int>(wcslen(buf)));
    }
    if (!hasPaper_) {
        SetTextColor(hdc, RGB(190, 120, 60));
        const wchar_t* warn = L"尚未校正：完成 A、B 兩點後才會顯示畫紙與規劃路徑";
        TextOutW(hdc, 8, 42, warn, static_cast<int>(wcslen(warn)));
    }
    SetTextColor(hdc, RGB(150, 155, 165));
    const wchar_t* hint = L"左鍵拖曳=旋轉　右鍵拖曳=平移　滾輪=縮放　雙擊=重設視角";
    TextOutW(hdc, 8, h - 20, hint, static_cast<int>(wcslen(hint)));
    SelectObject(hdc, prevFont);

    BitBlt(target, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

LRESULT SimView::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            SetCapture(hwnd);
            orbiting_ = true;
            lastMouse_ = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            return 0;
        case WM_RBUTTONDOWN:
            SetFocus(hwnd);
            SetCapture(hwnd);
            panning_ = true;
            lastMouse_ = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            return 0;
        case WM_MOUSEMOVE: {
            if (!orbiting_ && !panning_) return 0;
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int dx = p.x - lastMouse_.x;
            int dy = p.y - lastMouse_.y;
            lastMouse_ = p;
            if (orbiting_) {
                azimuthDeg_ -= dx * 0.45;
                elevationDeg_ = std::clamp(elevationDeg_ + dy * 0.45, -89.0, 89.0);
            } else {
                RECT rc;
                GetClientRect(hwnd, &rc);
                Camera cam =
                    BuildCamera(azimuthDeg_, elevationDeg_, distance_, target_, rc.right, rc.bottom);
                double scale = distance_ / std::max(1.0, cam.focal);
                target_ = target_ - cam.right * (dx * scale) + cam.up * (dy * scale);
            }
            Invalidate();
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            if (orbiting_ || panning_) {
                orbiting_ = panning_ = false;
                ReleaseCapture();
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            ResetView();
            return 0;
        case WM_MOUSEWHEEL:
            HandleWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            Render(hdc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (font_) {
                DeleteObject(font_);
                font_ = nullptr;
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK SimView::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SimView* self = GetThis(hwnd);
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(hwnd, msg, wParam, lParam);
}

} // namespace mycobot
