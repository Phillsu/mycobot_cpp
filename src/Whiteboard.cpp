#include "Whiteboard.h"
#include <windowsx.h>

namespace mycobot {

namespace {
constexpr wchar_t kClassName[] = L"MyCobotWhiteboardClass";

Whiteboard* GetThis(HWND hwnd) {
    return reinterpret_cast<Whiteboard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
} // namespace

void Whiteboard::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &Whiteboard::WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

HWND Whiteboard::Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, int id) {
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE,
                             x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             hInst, nullptr);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return hwnd_;
}

void Whiteboard::Clear() {
    strokes_.clear();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void Whiteboard::UndoLastStroke() {
    if (!strokes_.empty()) strokes_.pop_back();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void Whiteboard::PaintTo(HDC hdc, RECT client) {
    // Double buffer to avoid flicker while drawing.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

    FillRect(mem, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    HPEN pen = CreatePen(PS_SOLID, 3, RGB(20, 20, 20));
    HPEN oldPen = static_cast<HPEN>(SelectObject(mem, pen));
    for (const auto& stroke : strokes_) {
        if (stroke.size() < 2) continue;
        MoveToEx(mem, stroke[0].x, stroke[0].y, nullptr);
        for (size_t i = 1; i < stroke.size(); ++i) {
            LineTo(mem, stroke[i].x, stroke[i].y);
        }
    }
    SelectObject(mem, oldPen);
    DeleteObject(pen);

    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT Whiteboard::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!inputEnabled_) return 0;
            SetCapture(hwnd);
            drawing_ = true;
            strokes_.emplace_back();
            strokes_.back().push_back(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!drawing_ || !inputEnabled_) return 0;
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            strokes_.back().push_back(p);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (drawing_) {
                drawing_ = false;
                ReleaseCapture();
                // Drop degenerate single-point "strokes" (a click, not a drag).
                if (!strokes_.empty() && strokes_.back().size() < 2) strokes_.pop_back();
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            PaintTo(hdc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // avoid flicker; WM_PAINT repaints the whole client area
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK Whiteboard::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Whiteboard* self = GetThis(hwnd);
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(hwnd, msg, wParam, lParam);
}

} // namespace mycobot
