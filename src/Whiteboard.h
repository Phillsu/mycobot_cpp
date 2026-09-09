// Whiteboard.h
//
// A simple MS-Paint-like drawing surface implemented as a custom Win32
// window class. Captures freehand mouse strokes as polylines in the
// control's own client-pixel coordinate space; that space is exactly what
// Calibration::MapPoint expects (canvas size == calibrated canvas size).
#pragma once

#include <windows.h>
#include <vector>

namespace mycobot {

using Stroke = std::vector<POINT>;

class Whiteboard {
public:
    static void RegisterClass(HINSTANCE hInst);

    HWND Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, int id);

    const std::vector<Stroke>& GetStrokes() const { return strokes_; }
    bool Empty() const { return strokes_.empty(); }

    void Clear();
    void UndoLastStroke();
    // Disabled while a drawing job is in flight so the user can't edit
    // strokes out from under the sender.
    void SetInputEnabled(bool enabled) { inputEnabled_ = enabled; }

    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

    void PaintTo(HDC hdc, RECT client);

    HWND hwnd_ = nullptr;
    std::vector<Stroke> strokes_;
    bool drawing_ = false;
    bool inputEnabled_ = true;
};

} // namespace mycobot
